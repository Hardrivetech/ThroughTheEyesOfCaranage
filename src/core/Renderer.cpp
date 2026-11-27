#include "core/Renderer.hpp"
#include "core/Log.hpp"
#include "game/World.hpp"
#include "gfx/Mesh.hpp"
#include "gfx/Shader.hpp"

#include <glad/gl.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#include <stb_image.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace carnage::renderer {

namespace {

constexpr int         kWidth   = 1920;
constexpr int         kHeight  = 1080;
constexpr int         kGlMajor = 4;
constexpr int         kGlMinor = 6;
constexpr bool        kVsync   = false; // false = uncapped render loop
constexpr const char* kTitle   = "Through the Eyes of Carnage";
constexpr float       kBottom  = -0.2f; // visual underside of every block

constexpr int   kMsaaSamples = 4;
constexpr int   kShadowSize  = 4096;
constexpr float kShadowRadius = 50.0f;   // half-width of the sun ortho box (m)
constexpr float kShadowDepth  = 120.0f;  // half-depth along the sun direction (m)
constexpr int   kMaxLights    = 8;

const glm::vec3 kSunDir   = glm::normalize(glm::vec3(0.50f, 0.55f, 0.40f));
const glm::vec3 kSunColor = glm::vec3(4.2f, 3.0f, 2.1f);
constexpr float kHaze     = 0.0045f;
constexpr float kExposure = 1.0f;

GLFWwindow* g_window    = nullptr;
bool        g_glfwReady = false;
int         g_fbW = kWidth, g_fbH = kHeight;
float       g_aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);

// ---- vertex formats -----------------------------------------------------------

struct SceneVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec3 color;
    float     material;
};

void setSceneVertexFormat() {
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          reinterpret_cast<void*>(offsetof(SceneVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          reinterpret_cast<void*>(offsetof(SceneVertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          reinterpret_cast<void*>(offsetof(SceneVertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(SceneVertex),
                          reinterpret_cast<void*>(offsetof(SceneVertex, material)));
}

float materialId(Material m) { return static_cast<float>(static_cast<int>(m)); }

// ---- shaders ------------------------------------------------------------------

gfx::Shader g_scene, g_meshShader, g_shadow, g_sky, g_post, g_overlay, g_menuBg;

GLuint g_menuBgTex    = 0;
float  g_menuBgAspect = 16.0f / 9.0f;
bool   g_menuBgTried  = false;

// Loads an image file into an sRGB texture. Returns 0 (and creates nothing) if
// the file is missing, unreadable, or implausibly large.
GLuint loadTextureFile(const std::string& path, float& aspectOut, int* wOut = nullptr, int* hOut = nullptr) {
    constexpr int kMaxSide = 8192;  // refuse absurd sizes before allocating anything
    int w = 0, h = 0, n = 0;
    if (!stbi_info(path.c_str(), &w, &h, &n) || w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) return 0;
    stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!px) return 0;
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    glTextureStorage2D(tex, 1, GL_SRGB8_ALPHA8, w, h);
    glTextureSubImage2D(tex, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(px);
    aspectOut = static_cast<float>(w) / static_cast<float>(h);
    if (wOut) *wOut = w;
    if (hOut) *hOut = h;
    return tex;
}

void loadMenuBackground() {
    g_menuBgTried = true;
    for (const char* name : {"ui/menu_background.png", "ui/menu_background.jpg",
                             "ui/menu_background.jpeg", "ui/menu_background.bmp"}) {
        int w = 0, h = 0;
        g_menuBgTex = loadTextureFile(std::string(CARNAGE_ASSET_DIR) + "/" + name, g_menuBgAspect, &w, &h);
        if (!g_menuBgTex) continue;
        log::info("Menu background: {} ({}x{})", name, w, h);
        return;
    }
    log::info("No assets/ui/menu_background.png|jpg found; using procedural backdrop");
}

// The briefing backdrop: at most one image alive at a time.
GLuint      g_briefTex    = 0;
float       g_briefAspect = 16.0f / 9.0f;
std::string g_briefPath;  // what g_briefTex was loaded from ("" = nothing / plain background)

bool loadShaders() {
    bool ok = true;
    ok &= g_scene.load("scene.vert", "scene.frag");
    ok &= g_meshShader.load("mesh.vert", "scene.frag");
    ok &= g_shadow.load("shadow.vert", "shadow.frag");
    ok &= g_sky.load("fullscreen.vert", "sky.frag");
    ok &= g_post.load("fullscreen.vert", "post.frag");
    ok &= g_overlay.load("overlay.vert", "overlay.frag");
    ok &= g_menuBg.load("fullscreen.vert", "menu_bg.frag");
    return ok;
}

// ---- render targets -----------------------------------------------------------

GLuint g_msaaFbo = 0, g_msaaColor = 0, g_msaaDepth = 0;
GLuint g_resolveFbo = 0, g_resolveColor = 0;
GLuint g_shadowFbo = 0, g_shadowTex = 0;
GLuint g_emptyVao = 0;

void destroyTargets() {
    if (g_msaaFbo)      glDeleteFramebuffers(1, &g_msaaFbo);
    if (g_msaaColor)    glDeleteRenderbuffers(1, &g_msaaColor);
    if (g_msaaDepth)    glDeleteRenderbuffers(1, &g_msaaDepth);
    if (g_resolveFbo)   glDeleteFramebuffers(1, &g_resolveFbo);
    if (g_resolveColor) glDeleteTextures(1, &g_resolveColor);
    g_msaaFbo = g_msaaColor = g_msaaDepth = g_resolveFbo = g_resolveColor = 0;
}

bool createTargets(int w, int h) {
    destroyTargets();

    glCreateRenderbuffers(1, &g_msaaColor);
    glNamedRenderbufferStorageMultisample(g_msaaColor, kMsaaSamples, GL_RGBA16F, w, h);
    glCreateRenderbuffers(1, &g_msaaDepth);
    glNamedRenderbufferStorageMultisample(g_msaaDepth, kMsaaSamples, GL_DEPTH_COMPONENT32F, w, h);
    glCreateFramebuffers(1, &g_msaaFbo);
    glNamedFramebufferRenderbuffer(g_msaaFbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_msaaColor);
    glNamedFramebufferRenderbuffer(g_msaaFbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, g_msaaDepth);

    glCreateTextures(GL_TEXTURE_2D, 1, &g_resolveColor);
    glTextureStorage2D(g_resolveColor, 1, GL_RGBA16F, w, h);
    glTextureParameteri(g_resolveColor, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(g_resolveColor, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(g_resolveColor, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(g_resolveColor, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glCreateFramebuffers(1, &g_resolveFbo);
    glNamedFramebufferTexture(g_resolveFbo, GL_COLOR_ATTACHMENT0, g_resolveColor, 0);

    const bool ok =
        glCheckNamedFramebufferStatus(g_msaaFbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
        glCheckNamedFramebufferStatus(g_resolveFbo, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!ok) log::error("HDR framebuffer incomplete ({}x{})", w, h);
    return ok;
}

bool createShadowMap() {
    glCreateTextures(GL_TEXTURE_2D, 1, &g_shadowTex);
    glTextureStorage2D(g_shadowTex, 1, GL_DEPTH_COMPONENT32F, kShadowSize, kShadowSize);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTextureParameterfv(g_shadowTex, GL_TEXTURE_BORDER_COLOR, border);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTextureParameteri(g_shadowTex, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

    glCreateFramebuffers(1, &g_shadowFbo);
    glNamedFramebufferTexture(g_shadowFbo, GL_DEPTH_ATTACHMENT, g_shadowTex, 0);
    glNamedFramebufferDrawBuffer(g_shadowFbo, GL_NONE);
    glNamedFramebufferReadBuffer(g_shadowFbo, GL_NONE);
    if (glCheckNamedFramebufferStatus(g_shadowFbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("Shadow framebuffer incomplete");
        return false;
    }
    return true;
}

// ---- static level mesh --------------------------------------------------------

GLuint  g_vao = 0, g_vbo = 0, g_ebo = 0;
GLsizei g_indexCount = 0;

void destroyLevel() {
    if (g_ebo) glDeleteBuffers(1, &g_ebo);
    if (g_vbo) glDeleteBuffers(1, &g_vbo);
    if (g_vao) glDeleteVertexArrays(1, &g_vao);
    g_ebo = g_vbo = g_vao = 0;
    g_indexCount = 0;
}

// ---- dynamic boxes ------------------------------------------------------------

constexpr std::size_t kMaxBoxes = 8192;

GLuint g_dynVao = 0, g_dynVbo = 0, g_dynEbo = 0;
std::vector<SceneVertex> g_dynVerts;

struct BoxBatch {
    std::size_t total   = 0;
    std::size_t casters = 0;  // first `casters` boxes cast sun shadows
};

bool createDynamic() {
    std::vector<GLuint> idx;
    idx.reserve(kMaxBoxes * 36);
    for (std::size_t b = 0; b < kMaxBoxes; ++b)
        for (GLuint f = 0; f < 6; ++f) {
            const GLuint base = static_cast<GLuint>(b * 24) + f * 4;
            for (GLuint k : {0u, 1u, 2u, 2u, 3u, 0u}) idx.push_back(base + k);
        }

    glGenVertexArrays(1, &g_dynVao);
    glGenBuffers(1, &g_dynVbo);
    glGenBuffers(1, &g_dynEbo);
    glBindVertexArray(g_dynVao);
    glBindBuffer(GL_ARRAY_BUFFER, g_dynVbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kMaxBoxes * 24 * sizeof(SceneVertex)),
                 nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_dynEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(GLuint)),
                 idx.data(), GL_STATIC_DRAW);
    setSceneVertexFormat();
    glBindVertexArray(0);
    g_dynVerts.reserve(kMaxBoxes * 24);
    return true;
}

void appendBox(const DrawBox& b) {
    const Material  mat = b.emissive ? Material::Emissive : b.material;
    const float     m   = materialId(mat);
    const glm::vec3 h   = b.half;
    const float     he[3] = {h.x, h.y, h.z};

    for (int a = 0; a < 3; ++a) {
        const int u = (a + 1) % 3, v = (a + 2) % 3;
        for (const float s : {-1.0f, 1.0f}) {
            const glm::vec3 n      = b.axes[a] * s;
            const glm::vec3 centre = b.center + n * he[a];
            const glm::vec3 du     = b.axes[u] * he[u];
            const glm::vec3 dv     = b.axes[v] * he[v];
            g_dynVerts.push_back({centre - du - dv, n, b.color, m});
            g_dynVerts.push_back({centre + du - dv, n, b.color, m});
            g_dynVerts.push_back({centre + du + dv, n, b.color, m});
            g_dynVerts.push_back({centre - du + dv, n, b.color, m});
        }
    }
}

bool castsShadow(const DrawBox& b) {
    return !b.emissive && b.material != Material::Emissive &&
           std::max({b.half.x, b.half.y, b.half.z}) >= 0.05f;
}

// Uploads boxes, shadow casters first. Returns counts.
BoxBatch uploadBoxes(std::span<const DrawBox> boxes) {
    BoxBatch batch;
    const std::size_t count = std::min(boxes.size(), kMaxBoxes);
    if (count == 0) return batch;

    g_dynVerts.clear();
    for (std::size_t i = 0; i < count; ++i)
        if (castsShadow(boxes[i])) appendBox(boxes[i]);
    batch.casters = g_dynVerts.size() / 24;
    for (std::size_t i = 0; i < count; ++i)
        if (!castsShadow(boxes[i])) appendBox(boxes[i]);
    batch.total = count;

    glBindBuffer(GL_ARRAY_BUFFER, g_dynVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(g_dynVerts.size() * sizeof(SceneVertex)),
                    g_dynVerts.data());
    return batch;
}

void drawBoxRange(std::size_t boxCount) {
    if (boxCount == 0) return;
    glBindVertexArray(g_dynVao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(boxCount * 36), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

// ---- props --------------------------------------------------------------------

struct Prop {
    int       mesh;
    glm::mat4 model;
    glm::vec3 color;
    Material  material;
};

std::vector<gfx::GpuMesh> g_meshes;
std::vector<Prop>         g_props;

// ---- overlay ------------------------------------------------------------------

struct OverlayVertex {
    glm::vec2 pos;
    glm::vec4 col;
};

constexpr std::size_t kMaxOverlayRects = 16384;

GLuint g_ovVao = 0, g_ovVbo = 0;
std::vector<OverlayVertex> g_ovVerts;

bool createOverlay() {
    glGenVertexArrays(1, &g_ovVao);
    glGenBuffers(1, &g_ovVbo);
    glBindVertexArray(g_ovVao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ovVbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kMaxOverlayRects * 6 * sizeof(OverlayVertex)),
                 nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                          reinterpret_cast<void*>(offsetof(OverlayVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(OverlayVertex),
                          reinterpret_cast<void*>(offsetof(OverlayVertex, col)));
    glBindVertexArray(0);
    g_ovVerts.reserve(kMaxOverlayRects * 6);
    return true;
}

void destroyScene() {
    destroyLevel();
    destroyTargets();
    g_meshes.clear();  // GpuMesh destructors release GL objects
    g_props.clear();
    if (g_dynEbo) glDeleteBuffers(1, &g_dynEbo);
    if (g_dynVbo) glDeleteBuffers(1, &g_dynVbo);
    if (g_dynVao) glDeleteVertexArrays(1, &g_dynVao);
    if (g_ovVbo)  glDeleteBuffers(1, &g_ovVbo);
    if (g_ovVao)  glDeleteVertexArrays(1, &g_ovVao);
    if (g_shadowFbo) glDeleteFramebuffers(1, &g_shadowFbo);
    if (g_shadowTex) glDeleteTextures(1, &g_shadowTex);
    if (g_emptyVao)  glDeleteVertexArrays(1, &g_emptyVao);
    g_dynEbo = g_dynVbo = g_dynVao = g_ovVbo = g_ovVao = 0;
    g_shadowFbo = g_shadowTex = g_emptyVao = 0;
    g_scene.release(); g_meshShader.release(); g_shadow.release();
    g_sky.release(); g_post.release(); g_overlay.release(); g_menuBg.release();
    if (g_menuBgTex) glDeleteTextures(1, &g_menuBgTex);
    g_menuBgTex = 0;
    if (g_briefTex) glDeleteTextures(1, &g_briefTex);
    g_briefTex = 0;
    g_briefPath.clear();
}

// ---- GLFW callbacks -----------------------------------------------------------

void onGlfwError(int code, const char* desc) {
    log::error("GLFW {}: {}", code, desc);
}

void onFramebufferResize(GLFWwindow*, int w, int h) {
    if (w <= 0 || h <= 0) return;
    glViewport(0, 0, w, h);
    g_aspect = static_cast<float>(w) / static_cast<float>(h);
    if ((w != g_fbW || h != g_fbH) && g_msaaFbo) {
        g_fbW = w;
        g_fbH = h;
        createTargets(w, h);
    }
    g_fbW = w;
    g_fbH = h;
}

// ---- frame helpers ------------------------------------------------------------

// Sun ortho matrix centred ahead of the camera, snapped to shadow texels so
// edges don't crawl as the camera moves.
glm::mat4 sunViewProj(const glm::vec3& eye, const glm::vec3& forward) {
    const glm::vec3 centre = eye + glm::vec3(forward.x, 0.0f, forward.z) * (kShadowRadius * 0.4f);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -kSunDir, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 c = glm::vec3(lightView * glm::vec4(centre, 1.0f));
    const float texel = 2.0f * kShadowRadius / static_cast<float>(kShadowSize);
    c.x = std::floor(c.x / texel) * texel;
    c.y = std::floor(c.y / texel) * texel;

    const glm::mat4 proj = glm::ortho(c.x - kShadowRadius, c.x + kShadowRadius,
                                      c.y - kShadowRadius, c.y + kShadowRadius,
                                      -c.z - kShadowDepth, -c.z + kShadowDepth);
    return proj * lightView;
}

void drawLevel() {
    if (!g_vao) return;
    glBindVertexArray(g_vao);
    glDrawElements(GL_TRIANGLES, g_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void shadowPass(const glm::mat4& lightVP, const BoxBatch& batch) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_shadowFbo);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);

    g_shadow.use();
    g_shadow.set("uLightViewProj", lightVP);
    g_shadow.set("uModel", glm::mat4(1.0f));
    drawLevel();
    drawBoxRange(batch.casters);

    for (const Prop& p : g_props) {
        g_shadow.set("uModel", p.model);
        g_meshes[static_cast<std::size_t>(p.mesh)].draw();
    }
    g_shadow.set("uModel", glm::mat4(1.0f));

    glDisable(GL_POLYGON_OFFSET_FILL);
}

void setLighting(const gfx::Shader& sh, const glm::vec3& eye, const glm::mat4& lightVP,
                 std::span<const PointLight> lights) {
    sh.set("uEye", eye);
    sh.set("uSunDir", kSunDir);
    sh.set("uSunColor", kSunColor);
    sh.set("uHaze", kHaze);
    sh.set("uLightViewProj", lightVP);
    sh.set("uShadowMap", 5);
    sh.set("uShadowTexel", 2.0f * kShadowRadius / static_cast<float>(kShadowSize));

    glm::vec4 posR[kMaxLights]{};
    glm::vec3 col[kMaxLights]{};
    const int n = static_cast<int>(std::min<std::size_t>(lights.size(), kMaxLights));
    for (int i = 0; i < n; ++i) {
        posR[i] = glm::vec4(lights[static_cast<std::size_t>(i)].position,
                            lights[static_cast<std::size_t>(i)].radius);
        col[i]  = lights[static_cast<std::size_t>(i)].color;
    }
    sh.set("uLightCount", n);
    sh.setArray("uLightPosR", std::span<const glm::vec4>(posR, kMaxLights));
    sh.setArray("uLightCol", std::span<const glm::vec3>(col, kMaxLights));
}

} // namespace

GLFWwindow* window() { return g_window; }

bool init() {
    glfwSetErrorCallback(onGlfwError);
    if (glfwInit() != GLFW_TRUE) {
        log::error("Failed to initialise GLFW");
        return false;
    }
    g_glfwReady = true;

    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, kGlMajor);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, kGlMinor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    // The window itself is not multisampled: the scene renders into an MSAA
    // HDR target and the post pass writes the resolved result.

    g_window = glfwCreateWindow(kWidth, kHeight, kTitle, nullptr, nullptr);
    if (!g_window) {
        log::error("Failed to create window (OpenGL {}.{} core required)", kGlMajor, kGlMinor);
        return false;
    }

    if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        glfwSetWindowPos(g_window, mx + (mw - kWidth) / 2, my + (mh - kHeight) / 2);
    }

    glfwMakeContextCurrent(g_window);
    if (!gladLoadGL(glfwGetProcAddress)) {
        log::error("Failed to load OpenGL function pointers");
        return false;
    }

    GLint major = 0, minor = 0, profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (major < kGlMajor || (major == kGlMajor && minor < kGlMinor) ||
        !(profile & GL_CONTEXT_CORE_PROFILE_BIT)) {
        log::error("Need OpenGL {}.{} core, got {}.{}", kGlMajor, kGlMinor, major, minor);
        return false;
    }
    log::info("GL {} | {}",
              reinterpret_cast<const char*>(glGetString(GL_VERSION)),
              reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    glfwSwapInterval(kVsync ? 1 : 0);
    glfwSetFramebufferSizeCallback(g_window, onFramebufferResize);

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(g_window, &fbw, &fbh);
    g_fbW = fbw > 0 ? fbw : kWidth;
    g_fbH = fbh > 0 ? fbh : kHeight;
    glViewport(0, 0, g_fbW, g_fbH);
    g_aspect = static_cast<float>(g_fbW) / static_cast<float>(g_fbH);

    glGenVertexArrays(1, &g_emptyVao);
    return loadShaders() && createTargets(g_fbW, g_fbH) && createShadowMap() &&
           createDynamic() && createOverlay();
}

void shutdown() {
    if (g_window) {
        destroyScene(); // needs the context alive
        glfwDestroyWindow(g_window);
        g_window = nullptr;
    }
    if (g_glfwReady) {
        glfwTerminate();
        g_glfwReady = false;
    }
}

void  pollEvents()   { glfwPollEvents(); }
bool  shouldClose()  { return glfwWindowShouldClose(g_window) == GLFW_TRUE; }
void  requestClose() { glfwSetWindowShouldClose(g_window, GLFW_TRUE); }
void  setVsync(bool on) { glfwSwapInterval(on ? 1 : 0); }
float aspect()       { return g_aspect; }

void reloadShaders() {
    bool ok = true;
    ok &= g_scene.reload();
    ok &= g_meshShader.reload();
    ok &= g_shadow.reload();
    ok &= g_sky.reload();
    ok &= g_post.reload();
    ok &= g_overlay.reload();
    ok &= g_menuBg.reload();
    if (ok) log::info("Shaders reloaded");
    else    log::warn("Shader reload had errors; failed programs kept their old version");
}

bool uploadWorld(const World& world) {
    destroyLevel();

    // Corner i of a block: x/y/z selector bits. Top corners (y+) take the
    // block's sloped height, so ramps are boxes with a tilted top.
    constexpr bool xp[8] = {0, 1, 1, 0, 0, 1, 1, 0};
    constexpr bool yp[8] = {0, 0, 1, 1, 0, 0, 1, 1};
    constexpr bool zp[8] = {0, 0, 0, 0, 1, 1, 1, 1};
    constexpr int  faces[6][4] = {
        {0, 1, 2, 3}, {4, 5, 6, 7}, {0, 4, 7, 3},
        {1, 5, 6, 2}, {3, 2, 6, 7}, {0, 1, 5, 4},
    };

    std::vector<SceneVertex> verts;
    std::vector<GLuint>      idx;
    verts.reserve(world.blocks().size() * 24);
    idx.reserve(world.blocks().size() * 36);

    for (const Block& b : world.blocks()) {
        if (!b.visible) continue;
        glm::vec3 corner[8];
        glm::vec3 centre(0.0f);
        for (int i = 0; i < 8; ++i) {
            const float x   = xp[i] ? b.max.x : b.min.x;
            const float z   = zp[i] ? b.max.y : b.min.y;
            const bool  hi  = b.axis == 0 ? xp[i] : zp[i];
            const float top = hi ? b.h1 : b.h0;
            corner[i] = {x, yp[i] ? top : b.base, z};
            centre += corner[i] * 0.125f;
        }

        const float m = materialId(b.material);
        for (const auto& f : faces) {
            // Newell's method: robust for the planar, possibly skewed quads.
            glm::vec3 n(0.0f), fc(0.0f);
            for (int k = 0; k < 4; ++k) {
                const glm::vec3& a = corner[f[k]];
                const glm::vec3& c = corner[f[(k + 1) % 4]];
                n.x += (a.y - c.y) * (a.z + c.z);
                n.y += (a.z - c.z) * (a.x + c.x);
                n.z += (a.x - c.x) * (a.y + c.y);
                fc += a * 0.25f;
            }
            const float len = glm::length(n);
            n = len > 1e-6f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
            if (glm::dot(n, fc - centre) < 0.0f) n = -n;

            const GLuint base = static_cast<GLuint>(verts.size());
            for (int k = 0; k < 4; ++k) verts.push_back({corner[f[k]], n, b.color, m});
            for (GLuint k : {0u, 1u, 2u, 2u, 3u, 0u}) idx.push_back(base + k);
        }
    }

    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ebo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(SceneVertex)),
                 verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(idx.size() * sizeof(GLuint)),
                 idx.data(), GL_STATIC_DRAW);
    setSceneVertexFormat();
    glBindVertexArray(0);

    g_indexCount = static_cast<GLsizei>(idx.size());
    return true;
}

int loadMesh(const char* relativePath) {
    auto data = gfx::loadMeshCached(relativePath);
    if (!data) {
        log::error("loadMesh('{}'): {}", relativePath, data.error());
        return -1;
    }
    gfx::GpuMesh mesh;
    if (!mesh.upload(*data)) {
        log::error("loadMesh('{}'): empty mesh", relativePath);
        return -1;
    }
    log::info("Mesh {} ({} verts, {} tris)", relativePath, data->vertices.size(),
              data->indices.size() / 3);
    g_meshes.push_back(std::move(mesh));
    return static_cast<int>(g_meshes.size()) - 1;
}

void addProp(int meshId, const glm::mat4& model, const glm::vec3& color, Material material) {
    if (meshId < 0 || static_cast<std::size_t>(meshId) >= g_meshes.size()) return;
    g_props.push_back({meshId, model, color, material});
}

void clearProps() { g_props.clear(); }

void renderScene(const SceneParams& p, std::span<const DrawBox> boxes,
                 std::span<const DrawBox> viewmodel) {
    const glm::mat4 invView = glm::inverse(p.view);
    const glm::vec3 eye     = glm::vec3(invView[3]);
    const glm::vec3 forward = -glm::vec3(invView[2]);
    const glm::mat4 viewProj = p.projection * p.view;
    const glm::mat4 lightVP  = sunViewProj(eye, forward);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    // 1. Sun shadow map (static level, props, non-emissive boxes).
    const BoxBatch batch = uploadBoxes(boxes);
    shadowPass(lightVP, batch);

    // 2. HDR scene.
    glBindFramebuffer(GL_FRAMEBUFFER, g_msaaFbo);
    glViewport(0, 0, g_fbW, g_fbH);
    glClear(GL_DEPTH_BUFFER_BIT);
    glBindTextureUnit(5, g_shadowTex);

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    g_sky.use();
    g_sky.set("uInvViewProj", glm::inverse(viewProj));
    g_sky.set("uEye", eye);
    g_sky.set("uSunDir", kSunDir);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);

    g_scene.use();
    setLighting(g_scene, eye, lightVP, p.lights);
    g_scene.set("uViewProj", viewProj);
    g_scene.set("uModel", glm::mat4(1.0f));
    g_scene.set("uObjectNoise", 0);
    drawLevel();
    drawBoxRange(batch.total);

    if (!g_props.empty()) {
        g_meshShader.use();
        setLighting(g_meshShader, eye, lightVP, p.lights);
        g_meshShader.set("uViewProj", viewProj);
        for (const Prop& pr : g_props) {
            g_meshShader.set("uModel", pr.model);
            g_meshShader.set("uNormalMat", glm::mat3(glm::transpose(glm::inverse(pr.model))));
            g_meshShader.set("uColor", pr.color);
            g_meshShader.set("uMaterial", static_cast<int>(pr.material));
            g_meshes[static_cast<std::size_t>(pr.mesh)].draw();
        }
    }

    // 3. Viewmodel: own projection, depth cleared so it never clips into
    //    walls, still lit in world space so it picks up the scene's light.
    if (!viewmodel.empty()) {
        const BoxBatch vm = uploadBoxes(viewmodel);
        glClear(GL_DEPTH_BUFFER_BIT);
        g_scene.use();
        g_scene.set("uViewProj", p.viewmodelProjection * p.view);
        g_scene.set("uModel", invView);
        g_scene.set("uObjectNoise", 1);
        drawBoxRange(vm.total);
        g_scene.set("uObjectNoise", 0);
        g_scene.set("uModel", glm::mat4(1.0f));
    }

    // 4. Resolve MSAA, then tonemap into the window's framebuffer.
    glBlitNamedFramebuffer(g_msaaFbo, g_resolveFbo, 0, 0, g_fbW, g_fbH, 0, 0, g_fbW, g_fbH,
                           GL_COLOR_BUFFER_BIT, GL_NEAREST);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);
    glDisable(GL_DEPTH_TEST);
    g_post.use();
    g_post.set("uHdr", 0);
    g_post.set("uTime", p.time);
    g_post.set("uAberration", 0.002f + 0.03f * p.shake);
    g_post.set("uExposure", kExposure);
    g_post.set("uFlash", p.flash);
    glBindTextureUnit(0, g_resolveColor);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void drawOverlay(std::span<const OverlayRect> rects) {
    const std::size_t count = std::min(rects.size(), kMaxOverlayRects);
    if (count == 0) return;

    g_ovVerts.clear();
    for (std::size_t i = 0; i < count; ++i) {
        const OverlayRect& r = rects[i];
        const OverlayVertex a{{r.min.x, r.min.y}, r.color};
        const OverlayVertex b{{r.max.x, r.min.y}, r.color};
        const OverlayVertex c{{r.max.x, r.max.y}, r.color};
        const OverlayVertex d{{r.min.x, r.max.y}, r.color};
        g_ovVerts.insert(g_ovVerts.end(), {a, b, c, c, d, a});
    }

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_overlay.use();
    glBindVertexArray(g_ovVao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ovVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(g_ovVerts.size() * sizeof(OverlayVertex)),
                    g_ovVerts.data());
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(g_ovVerts.size()));
    glBindVertexArray(0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void drawMenuBackground(float timeSeconds) {
    if (!g_menuBgTried) loadMenuBackground();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);
    glDisable(GL_DEPTH_TEST);
    g_menuBg.use();
    g_menuBg.set("uTex", 0);
    g_menuBg.set("uHasTex", g_menuBgTex ? 1 : 0);
    g_menuBg.set("uTime", timeSeconds);
    g_menuBg.set("uAspect", g_aspect);
    g_menuBg.set("uImgAspect", g_menuBgAspect);
    if (g_menuBgTex) glBindTextureUnit(0, g_menuBgTex);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

bool setBriefingImage(const char* relativePath) {
    const std::string rel = relativePath ? relativePath : "";
    if (rel == g_briefPath) return g_briefTex != 0;  // already showing (or already known missing)
    if (g_briefTex) glDeleteTextures(1, &g_briefTex);
    g_briefTex = 0;
    g_briefPath = rel;
    if (rel.empty()) return false;
    g_briefTex = loadTextureFile(std::string(CARNAGE_ASSET_DIR) + "/" + rel, g_briefAspect);
    if (!g_briefTex) log::warn("Briefing image '{}' could not be loaded; using a plain background", rel);
    return g_briefTex != 0;
}

void clearBriefingImage() {
    if (g_briefTex) glDeleteTextures(1, &g_briefTex);
    g_briefTex = 0;
    g_briefPath.clear();
}

void drawBriefingBackdrop(float timeSeconds) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_fbW, g_fbH);
    if (!g_briefTex) {
        // No image: a plain near-black background.
        glClearColor(0.02f, 0.02f, 0.025f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    glDisable(GL_DEPTH_TEST);
    g_menuBg.use();
    g_menuBg.set("uTex", 0);
    g_menuBg.set("uHasTex", 1);
    g_menuBg.set("uTime", timeSeconds);
    g_menuBg.set("uAspect", g_aspect);
    g_menuBg.set("uImgAspect", g_briefAspect);
    glBindTextureUnit(0, g_briefTex);
    glBindVertexArray(g_emptyVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

void endFrame() { glfwSwapBuffers(g_window); }

} // namespace carnage::renderer
