#pragma once
#include "core/Material.hpp"

#include <glm/glm.hpp>

#include <cmath>

namespace carnage {

// An oriented, flat-shaded box. `axes` columns are the box's local x/y/z
// (a rotation matrix); `half` are the half-extents along them.
struct DrawBox {
    glm::vec3 center;
    glm::vec3 half;
    glm::mat3 axes;
    glm::vec3 color;
    bool      emissive = false;  // shorthand for Material::Emissive
    Material  material = Material::Plastic;
};

// A screen-space rectangle in NDC ([-1,1] on both axes), alpha-blended.
struct OverlayRect {
    glm::vec2 min;
    glm::vec2 max;
    glm::vec4 color;
};

// Unshadowed point light. `color` is HDR radiance-scale (includes intensity);
// influence fades to zero at `radius`.
struct PointLight {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float     radius = 10.0f;
};

// Right-handed orthonormal basis whose local +Z is `z` (must be normalised).
inline glm::mat3 basisFromDirection(const glm::vec3& z) {
    const glm::vec3 ref = std::abs(z.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 x = glm::normalize(glm::cross(ref, z));
    const glm::vec3 y = glm::cross(z, x);
    return glm::mat3(x, y, z);
}

} // namespace carnage
