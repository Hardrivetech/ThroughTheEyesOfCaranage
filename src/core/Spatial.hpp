#pragma once
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace carnage::audio {

struct Listener {
    glm::vec3 pos{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
};

struct SpatialParams {
    float refDistance = 2.0f;    // full volume inside this range
    float maxDistance = 45.0f;   // silent beyond this
    float rolloff     = 1.0f;    // 1 = inverse distance
    float behindDim   = 0.30f;   // extra attenuation for sounds directly behind
};

// True if a sound `distance` metres away can be heard at all. Beyond maxDistance
// spatialize() returns gain 0 exactly, so callers may skip such sounds (and the
// occlusion raycasts that would colour them) without changing what the player hears.
inline bool inAudibleRange(float distance, const SpatialParams& p = {}) { return distance < p.maxDistance; }

struct SpatialResult {
    float gain = 1.0f;   // 0..1 multiplier
    float pan  = 0.0f;   // -1 left .. +1 right
};

// Inverse-distance rolloff with a linear fade to zero at maxDistance (so
// voices never linger inaudibly), equal-power-friendly pan from the angle to
// the listener's right vector, and a mild "behind the head" dimming. Panning
// fades toward centre at very close range, where direction is unstable.
inline SpatialResult spatialize(const Listener& l, const glm::vec3& src,
                                const SpatialParams& p = {}) {
    const glm::vec3 d    = src - l.pos;
    const float     dist = glm::length(d);
    SpatialResult   r;

    if (dist >= p.maxDistance) { r.gain = 0.0f; return r; }
    const float d0   = std::max(dist, p.refDistance);
    float       gain = p.refDistance / (p.refDistance + p.rolloff * (d0 - p.refDistance));
    gain *= 1.0f - std::clamp((dist - 0.6f * p.maxDistance) / (0.4f * p.maxDistance), 0.0f, 1.0f);

    if (dist > 1e-3f) {
        const glm::vec3 dir = d / dist;
        const float side  = glm::dot(dir, l.right);
        const float front = glm::dot(dir, l.forward);
        const float near  = std::clamp(dist / p.refDistance, 0.0f, 1.0f);
        r.pan = std::clamp(side, -1.0f, 1.0f) * near;
        if (front < 0.0f) gain *= 1.0f - p.behindDim * (-front);
    }
    r.gain = std::clamp(gain, 0.0f, 1.0f);
    return r;
}

// ---- occlusion / low-pass -------------------------------------------------

// Fraction of probe rays blocked (0 = clear, 1 = fully walled off).
inline float occlusionFraction(int blocked, int total) {
    return total > 0 ? std::clamp(static_cast<float>(blocked) / static_cast<float>(total), 0.0f, 1.0f)
                     : 0.0f;
}

// Low-pass cutoff in Hz. Air absorbs highs with distance, and walls absorb far
// more. Geometric interpolation keeps the sweep perceptually even.
inline float lowpassCutoff(float distance, float occlusion) {
    constexpr float kClear = 20000.0f, kMuffled = 600.0f;
    const float distT = std::clamp(distance / 60.0f, 0.0f, 1.0f) * 0.35f;  // air: down to ~35% of the sweep
    const float t     = std::clamp(std::max(distT, occlusion), 0.0f, 1.0f);
    return kClear * std::pow(kMuffled / kClear, t);
}

// One-pole low-pass coefficient for the given cutoff.
inline float lowpassCoeff(float cutoffHz, float sampleRate) {
    return 1.0f - std::exp(-6.2831853f * cutoffHz / sampleRate);
}

// Gain reduction from occlusion (the filter does most of the work).
inline float occlusionGain(float occlusion) {
    return 1.0f - 0.55f * std::clamp(occlusion, 0.0f, 1.0f);
}

} // namespace carnage::audio
