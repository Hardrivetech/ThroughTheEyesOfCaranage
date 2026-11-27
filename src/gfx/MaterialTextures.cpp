#include "gfx/MaterialTextures.hpp"

#include "core/Log.hpp"
#include "core/Material.hpp"
#include "gfx/ProcTextures.hpp"

#include <glad/gl.h>
#include <stb_image.h>

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

namespace carnage::gfx {

namespace {

enum class Kind { Albedo, Normal, Mask };

constexpr const char* kKindSuffix[] = {"albedo", "normal", "mask"};

// An override image for one map of one material, resampled to `size`. Empty if there is none.
std::vector<std::uint8_t> loadOverride(std::string_view material, Kind kind, int size) {
    constexpr int kMaxSide = 8192;  // refuse absurd sizes before allocating anything
    for (const char* ext : {"png", "jpg", "bmp"}) {
        const std::string path = std::string(CARNAGE_ASSET_DIR) + "/textures/" + std::string(material) + "_" +
                                 kKindSuffix[static_cast<int>(kind)] + "." + ext;
        int w = 0, h = 0, n = 0;
        if (!stbi_info(path.c_str(), &w, &h, &n)) continue;  // not there (or not an image)
        if (w <= 0 || h <= 0 || w > kMaxSide || h > kMaxSide) {
            log::warn("Texture override {} ignored: {}x{} is not a usable size", path, w, h);
            continue;
        }
        stbi_uc* px = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!px) {
            log::warn("Texture override {} could not be read", path);
            continue;
        }
        std::vector<std::uint8_t> out = (w == size && h == size)
            ? std::vector<std::uint8_t>(px, px + static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4)
            : resampleRGBA(px, w, h, size);
        stbi_image_free(px);
        log::info("Texture override: {}_{} ({}x{})", material, kKindSuffix[static_cast<int>(kind)], w, h);
        return out;
    }
    return {};
}

GLuint makeArray(GLenum format, int size, int layers) {
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D_ARRAY, 1, &tex);
    const int levels = std::bit_width(static_cast<unsigned>(size));  // size, size/2, ... 1
    glTextureStorage3D(tex, levels, format, size, size, layers);
    glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GLfloat maxAniso = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
    glTextureParameterf(tex, GL_TEXTURE_MAX_ANISOTROPY, std::min(8.0f, maxAniso));
    return tex;
}

} // namespace

bool MaterialTextures::create() {
    release();
    constexpr int size = kProcTextureSize;
    albedo_ = makeArray(GL_SRGB8_ALPHA8, size, kTexturedMaterialCount);
    normal_ = makeArray(GL_RGBA8, size, kTexturedMaterialCount);
    mask_   = makeArray(GL_RGBA8, size, kTexturedMaterialCount);

    int overridden = 0;
    for (int layer = 0; layer < kTexturedMaterialCount; ++layer) {
        TextureSet set = generateTextureSet(layer, size);
        if (set.empty()) {
            log::error("Material texture generation failed for layer {}", layer);
            release();
            return false;
        }
        const std::string_view name = materialName(static_cast<Material>(kFirstTexturedMaterial + layer));
        const struct { Kind kind; std::vector<std::uint8_t>* data; GLuint tex; } maps[] = {
            {Kind::Albedo, &set.albedo, albedo_},
            {Kind::Normal, &set.normal, normal_},
            {Kind::Mask, &set.mask, mask_},
        };
        for (const auto& m : maps) {
            std::vector<std::uint8_t> over = loadOverride(name, m.kind, size);
            if (!over.empty()) { *m.data = std::move(over); ++overridden; }
            glTextureSubImage3D(m.tex, 0, 0, 0, layer, size, size, 1, GL_RGBA, GL_UNSIGNED_BYTE, m.data->data());
        }
    }
    glGenerateTextureMipmap(albedo_);
    glGenerateTextureMipmap(normal_);
    glGenerateTextureMipmap(mask_);
    log::info("Material textures: {} layers at {}x{} ({} map overrides from assets/textures)",
              kTexturedMaterialCount, size, size, overridden);
    return true;
}

void MaterialTextures::release() {
    if (albedo_) glDeleteTextures(1, &albedo_);
    if (normal_) glDeleteTextures(1, &normal_);
    if (mask_)   glDeleteTextures(1, &mask_);
    albedo_ = normal_ = mask_ = 0;
}

void MaterialTextures::bind(unsigned albedoUnit, unsigned normalUnit, unsigned maskUnit) const {
    glBindTextureUnit(albedoUnit, albedo_);
    glBindTextureUnit(normalUnit, normal_);
    glBindTextureUnit(maskUnit, mask_);
}

} // namespace carnage::gfx
