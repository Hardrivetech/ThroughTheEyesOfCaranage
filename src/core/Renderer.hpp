#pragma once
#include "core/DrawTypes.hpp"
#include "core/Environment.hpp"

#include <glm/mat4x4.hpp>

#include <span>

struct GLFWwindow;

namespace carnage {
class World;
}

namespace carnage::renderer {

// Owns GLFW, the borderless window, the GL core context and every GPU
// resource: HDR/MSAA targets, the sun shadow map, shaders, the static level
// mesh, props and the dynamic box / overlay batches.
bool        init();
void        shutdown();
GLFWwindow* window();

void  pollEvents();
bool  shouldClose();
void  requestClose();
void  setVsync(bool on);
float aspect();

// Builds the static level mesh (per-face normals + per-block material).
bool uploadWorld(const World& world);

// Loads an .obj/.cmesh under assets/ (cached as .cmesh). Returns a mesh id
// or -1 on failure.
int  loadMesh(const char* relativePath);
void addProp(int meshId, const glm::mat4& model, const glm::vec3& color, Material material);
void clearProps();  // drops every prop (call before building a new level's props)

// Briefing backdrop. One image is kept at a time; asking for another frees the
// previous one, and shutdown frees whatever is left.
//   setBriefingImage: path relative to assets/ ("" = none). Returns true if an image is
//                     now shown; false means a plain dark background is used.
//   drawBriefingBackdrop: clears the screen and draws the image (cover-fit, slow drift)
//                     or the plain background. Draw overlay rects on top afterwards.
bool setBriefingImage(const char* relativePath);
void clearBriefingImage();
void drawBriefingBackdrop(float timeSeconds);

// Re-reads every shader from disk; a shader that fails to compile keeps its
// previous program. Bound to F5 in main.
void reloadShaders();

struct SceneParams {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewmodelProjection{1.0f};
    float     time  = 0.0f;
    float     shake = 0.0f;  // trauma^2, drives chromatic aberration
    float     flash = 0.0f;  // muzzle blind, 0..1
    std::span<const PointLight> lights;  // first 8 are used
    Environment env;                     // fog density and shadow reach (sanitized before use)
};

// Renders the whole 3D frame: sun shadow pass, HDR sky + level + props +
// boxes, viewmodel, then resolve and post-process into the window.
// `viewmodel` boxes are in camera space.
void renderScene(const SceneParams& params, std::span<const DrawBox> boxes,
                 std::span<const DrawBox> viewmodel);

// Screen-space alpha-blended rectangles in NDC, drawn after the post pass.
void drawOverlay(std::span<const OverlayRect> rects);

// Full-screen main-menu backdrop (assets/ui/menu_background.png|jpg, or a
// procedural corridor if absent). Replaces renderScene on the main menu.
void drawMenuBackground(float timeSeconds);

void endFrame();

} // namespace carnage::renderer
