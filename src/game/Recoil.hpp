#pragma once
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

// Aim offset (pitch, yaw in radians) added on top of the camera orientation.
// Each shot kicks it; after a short delay it recovers along an exponential
// curve with a linear floor so it always settles fully to zero.
class Recoil {
public:
    void kick(float pitch, float yaw, float maxPitch) {
        cur_.x = std::min(cur_.x + pitch, maxPitch);
        cur_.y += yaw;
        sinceKick_ = 0.0f;
        heat_ = std::min(heat_ + 1.0f, kMaxHeat);
    }

    // Fixed tick. rate = exponential recovery (1/s), linear = floor (rad/s).
    void update(float dt, float delay, float rate, float linear) {
        prev_ = cur_;
        sinceKick_ += dt;
        heat_ = std::max(0.0f, heat_ - kHeatDecay * dt);

        // Sustained fire: the aim is held up longer and drifts back slower;
        // once the heat bleeds off, recovery snaps in at full rate.
        delay *= 1.0f + 0.5f * heat_;
        rate  /= 1.0f + 0.3f * heat_;
        if (sinceKick_ < delay) return;

        const float len = glm::length(cur_);
        if (len < 1e-6f) { cur_ = glm::vec2(0.0f); return; }
        const float drop = len * (1.0f - std::exp(-rate * dt)) + linear * dt;
        cur_ *= std::max(0.0f, len - drop) / len;
    }

    glm::vec2 current() const { return cur_; }
    glm::vec2 offset(float alpha) const { return glm::mix(prev_, cur_, alpha); }

private:
    glm::vec2 prev_{0.0f};
    glm::vec2 cur_{0.0f};
    float     sinceKick_ = 1.0e3f;
    float     heat_      = 0.0f;
    static constexpr float kMaxHeat   = 6.0f;
    static constexpr float kHeatDecay = 2.5f;  // heat units per second
};

} // namespace carnage
