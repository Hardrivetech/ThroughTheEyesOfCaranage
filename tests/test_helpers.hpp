#pragma once
// Shared helpers for the gameplay tests: small hand-built worlds and a fixed
// simulation step, so tests don't depend on the real level layout or on tuning.
#include "game/World.hpp"

#include <glm/glm.hpp>

namespace carnage::test {

constexpr float kDt = 1.0f / 60.0f;

// Flat-topped box (a wall, crate or slab). `base` is the underside height.
inline Block solid(float x0, float z0, float x1, float z1, float top, float base = -0.2f) {
    Block b;
    b.min  = {x0, z0};
    b.max  = {x1, z1};
    b.h0   = top;
    b.h1   = top;
    b.base = base;
    return b;
}

// Ramp along `axis` (0 = X, 1 = Z): top height h0 at min[axis], h1 at max[axis].
inline Block slope(float x0, float z0, float x1, float z1, int axis, float h0, float h1) {
    Block b = solid(x0, z0, x1, z1, h0);
    b.h1   = h1;
    b.axis = axis;
    return b;
}

// A 100 x 100 m floor at height 0.
inline World floorWorld() {
    World w;
    w.add(solid(-50.0f, -50.0f, 50.0f, 50.0f, 0.0f));
    return w;
}

inline bool near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-4f) {
    return glm::length(a - b) <= eps;
}

} // namespace carnage::test
