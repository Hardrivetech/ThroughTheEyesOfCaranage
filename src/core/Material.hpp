#pragma once

namespace carnage {

// Shading model selector. Values are mirrored by the M_* constants in
// assets/shaders/scene.frag; keep them in sync.
enum class Material : int {
    Concrete     = 0,  // board-formed concrete, stains, pits
    Tile         = 1,  // floor tiles with seams and wet patches
    PaintedMetal = 2,  // worn paint over bare/rusty metal
    Steel        = 3,  // smooth dark metal
    Brass        = 4,  // casings
    Flesh        = 5,  // targets
    Plastic      = 6,  // generic dielectric
    Emissive     = 7,  // unlit, HDR bright
    Skin         = 8,  // subsurface-scattered organic skin
    Cloth        = 9,  // woven tactical fabric
    Leather      = 10, // boots, gloves, straps
};

} // namespace carnage
