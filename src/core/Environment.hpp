#pragma once
#include <algorithm>
#include <cmath>

namespace carnage {

// Per-level atmosphere and view distances. A level without an `environment` record gets
// the defaults below, which are exactly the values the renderer used before levels could
// set them, so existing levels look the same.
//
//   haze         exponential fog density (per metre): fog = 1 - exp(-distance * haze)
//   farPlane     the camera's far clip distance
//   shadowRadius half-width of the sun shadow box around the player
//   snow         weather: how much snow settles on upward-facing surfaces of weathered materials
//                (0 = none, 1 = blanket). The default 0 leaves every existing level unchanged.
//
// A big open map wants three things to agree: fog thick enough to hide the far clip (so
// the edge of the world fades out instead of popping), a far plane that reaches the
// horizon, and a shadow box wide enough to matter but not so wide that shadows blur.
struct Environment {
    static constexpr float kDefaultHaze         = 0.0045f;
    static constexpr float kDefaultFarPlane     = 300.0f;
    static constexpr float kDefaultShadowRadius = 50.0f;

    static constexpr float kMinVisibility = 20.0f,  kMaxVisibility = 10000.0f;
    static constexpr float kMinFar        = 50.0f,  kMaxFar        = 5000.0f;
    static constexpr float kMinShadow     = 10.0f,  kMaxShadow     = 200.0f;

    // Fog this thick (1 - e^-3) hides 95% of what is behind it.
    static constexpr float kOpaqueExponent = 3.0f;
    // The far clip should be at least this foggy, or the end of the world pops into view.
    static constexpr float kMinFogAtFar    = 0.95f;

    float haze         = kDefaultHaze;
    float farPlane     = kDefaultFarPlane;
    float shadowRadius = kDefaultShadowRadius;
    float snow         = 0.0f;

    // Distance at which fog is 95% opaque.
    float visibility() const { return kOpaqueExponent / haze; }
    static float hazeForVisibility(float metres) { return kOpaqueExponent / metres; }

    float fogAt(float distance) const { return 1.0f - std::exp(-std::max(distance, 0.0f) * haze); }
    float fogAtFarPlane() const { return fogAt(farPlane); }
    bool  farPlaneIsHidden() const { return fogAtFarPlane() >= kMinFogAtFar; }

    bool operator==(const Environment&) const = default;
};

// `e` with every value forced into its allowed range (NaN and non-positive values fall back
// to the default), so nothing a file or a console command says can break the renderer.
inline Environment sanitized(Environment e) {
    const auto fix = [](float v, float def, float lo, float hi) { return (v >= lo && v <= hi) ? v : (v > hi ? hi : (v > 0.0f && v < lo ? lo : def)); };
    e.haze         = fix(e.haze, Environment::kDefaultHaze, Environment::kOpaqueExponent / Environment::kMaxVisibility,
                         Environment::kOpaqueExponent / Environment::kMinVisibility);
    e.farPlane     = fix(e.farPlane, Environment::kDefaultFarPlane, Environment::kMinFar, Environment::kMaxFar);
    e.shadowRadius = fix(e.shadowRadius, Environment::kDefaultShadowRadius, Environment::kMinShadow, Environment::kMaxShadow);
    e.snow         = (e.snow >= 0.0f && e.snow <= 1.0f) ? e.snow : (e.snow > 1.0f ? 1.0f : 0.0f);  // NaN -> 0
    return e;
}

} // namespace carnage
