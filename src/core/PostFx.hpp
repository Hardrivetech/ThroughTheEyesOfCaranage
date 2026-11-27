#pragma once
#include <algorithm>
#include <cmath>

namespace carnage {

// How strongly the screen-space motion and damage effects show, both 0..1.
struct PostFx {
    float speedLines = 0.0f;  // radial streaks at the screen edge
    float crimson    = 0.0f;  // red tint in the vignette
};

// Speed lines stay off up to a sprint (a normal run must look clean), then ramp in smoothly
// and reach full strength at the movement cap. `crimson` follows the camera trauma (the
// renderer's `shake`, already trauma^2, so small bumps stay invisible).
// Non-finite inputs give 0, so a bad value can never put a stuck effect on screen.
inline PostFx computePostFx(float horizontalSpeed, float sprintSpeed, float maxSpeed, float shake) {
    const auto smooth = [](float lo, float hi, float x) {
        if (!std::isfinite(x) || !(hi > lo)) return 0.0f;
        const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    PostFx fx;
    fx.speedLines = smooth(sprintSpeed * 1.1f, maxSpeed, horizontalSpeed);
    fx.crimson    = smooth(0.05f, 0.6f, shake);
    return fx;
}

} // namespace carnage
