#pragma once
#include "core/Material.hpp"

#include <cstdint>
#include <vector>

namespace carnage::gfx {

// Side length of the generated textures. Every layer of the texture arrays has this size, so
// override images of other sizes are resampled to it.
inline constexpr int kProcTextureSize = 256;

// One tileable material: three RGBA8 images of size x size, row 0 first. All of them wrap, so
// the texture can repeat across any surface without a visible seam.
struct TextureSet {
    int size = 0;
    // sRGB-encoded colour, A = 255.
    std::vector<std::uint8_t> albedo;
    // Tangent-space normal packed as n * 0.5 + 0.5 (+Z points out of the surface; X and Y follow
    // the texture's u and v axes). A = height.
    std::vector<std::uint8_t> normal;
    // R = roughness, G = metallic, B = ambient occlusion, A = glow (the part of the surface that
    // carries animated warning light; 0 for everything but hazard_trim).
    std::vector<std::uint8_t> mask;

    bool empty() const { return size <= 0; }
};

// Generates layer 0..kTexturedMaterialCount-1 (see core/Material.hpp, texturedLayer) at
// `size` x `size` pixels (8..2048). Pure and deterministic: integer hashing only, no clock or
// global state, so the same arguments always give the same bytes. An invalid layer or size
// returns an empty set.
TextureSet generateTextureSet(int layer, int size = kProcTextureSize);

// Bilinear resample of a tightly packed RGBA8 image (`sw` x `sh`) to `dstSize` x `dstSize`,
// treating the image as tiling (so the edges stay seamless). Returns an empty vector for an
// invalid size.
std::vector<std::uint8_t> resampleRGBA(const std::uint8_t* src, int sw, int sh, int dstSize);

} // namespace carnage::gfx
