#pragma once

namespace carnage::gfx {

// The texture-backed materials' images as three GL 2D texture arrays (one layer per material,
// see core/Material.hpp texturedLayer): albedo (sRGB), tangent-space normal, and a
// roughness / metallic / AO / glow mask. Layers are generated on the CPU at startup
// (gfx/ProcTextures.hpp); an image placed in assets/textures/ replaces the matching map:
//
//     assets/textures/<material>_albedo.png|jpg|bmp
//     assets/textures/<material>_normal.png|jpg|bmp
//     assets/textures/<material>_mask.png|jpg|bmp
//
// where <material> is a name such as concrete_worn. Overrides of another size are resampled to
// the generated size; unreadable files are skipped with a warning. Needs a current GL context.
class MaterialTextures {
public:
    MaterialTextures() = default;
    ~MaterialTextures() { release(); }
    MaterialTextures(const MaterialTextures&)            = delete;
    MaterialTextures& operator=(const MaterialTextures&) = delete;

    // Builds and uploads every layer. Returns false (leaving nothing allocated) if GL refuses.
    bool create();
    void release();
    bool valid() const { return albedo_ != 0; }

    // Binds the three arrays to the given texture units.
    void bind(unsigned albedoUnit, unsigned normalUnit, unsigned maskUnit) const;

private:
    unsigned albedo_ = 0, normal_ = 0, mask_ = 0;
};

} // namespace carnage::gfx
