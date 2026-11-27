#pragma once
#include <algorithm>
#include <cmath>

namespace carnage {

// How strongly the screen-space motion and damage effects show, both 0..1.
struct PostFx {
    float speedLines = 0.0f;  // radial streaks at the screen edge
    float crimson    = 0.0f;  // red tint in the vignette
};

// Speed lines (and the HUD's momentum gauge "overdrive" tier) start this far above a sprint, so a
// plain run looks clean. Shared so the two always agree.
inline constexpr float kSpeedLineStart = 1.1f;

// Below this many hit points the screen and the crosshair start to pulse like a heartbeat.
inline constexpr float kLowHealthHp = 25.0f;

// 0 at or above kLowHealthHp (and when dead: the death fade takes over), rising to 1 as health
// runs out. Non-finite health counts as healthy.
inline float lowHealthSeverity(float hp) {
    if (!std::isfinite(hp) || hp <= 0.0f || hp >= kLowHealthHp) return 0.0f;
    return 1.0f - hp / kLowHealthHp;
}

// A double heartbeat, 0..1, once a second or so, only while health is low. The beat gets stronger
// as health drops. Deterministic in `timeSeconds`, so it can be tested and never drifts.
inline float lowHealthPulse(float hp, float timeSeconds) {
    const float severity = lowHealthSeverity(hp);
    if (severity <= 0.0f || !std::isfinite(timeSeconds)) return 0.0f;
    constexpr float kRate = 1.2f;   // beats per second
    const float phase = timeSeconds * kRate - std::floor(timeSeconds * kRate);
    const auto  thump = [](float p) { return p < 0.0f ? 0.0f : std::exp(-14.0f * p); };
    const float beat  = std::max(thump(phase), 0.6f * thump(phase - 0.25f));
    return std::clamp(beat * (0.4f + 0.6f * severity), 0.0f, 1.0f);
}

// Speed lines stay off up to a sprint (a normal run must look clean), then ramp in smoothly
// and reach full strength at the movement cap. `crimson` follows the camera trauma (the
// renderer's `shake`, already trauma^2, so small bumps stay invisible) and, below 25 HP, a
// subtle heartbeat pulse in the vignette. Non-finite inputs give 0, so a bad value can never
// put a stuck effect on screen.
inline PostFx computePostFx(float horizontalSpeed, float sprintSpeed, float maxSpeed, float shake,
                            float health = 100.0f, float timeSeconds = 0.0f) {
    const auto smooth = [](float lo, float hi, float x) {
        if (!std::isfinite(x) || !(hi > lo)) return 0.0f;
        const float t = std::clamp((x - lo) / (hi - lo), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    PostFx fx;
    fx.speedLines = smooth(sprintSpeed * kSpeedLineStart, maxSpeed, horizontalSpeed);
    fx.crimson    = std::max(smooth(0.05f, 0.6f, shake), 0.45f * lowHealthPulse(health, timeSeconds));
    return fx;
}

} // namespace carnage
