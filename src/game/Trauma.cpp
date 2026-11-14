#include "game/Trauma.hpp"

#include <glm/gtc/noise.hpp>

#include <algorithm>

namespace carnage {

namespace {

// Perlin 2D: x = time, y = per-channel seed row. Seeds are non-integer so
// the channels never sit on lattice lines (where Perlin is exactly zero).
float channel(double t, int index) {
    const float n = glm::perlin(glm::vec2(static_cast<float>(t),
                                          static_cast<float>(index) * 31.7f + 0.37f));
    return std::clamp(n * 1.4142f, -1.0f, 1.0f); // 2D Perlin peaks near +/-0.707
}

} // namespace

void TraumaManager::add(float amount) {
    trauma_ = std::clamp(trauma_ + amount, 0.0f, 1.0f);
}

void TraumaManager::update(float dt) {
    trauma_ = std::max(0.0f, trauma_ - cfg_.decayPerSecond * dt);
    time_  += static_cast<double>(dt) * cfg_.frequency;
    prev_   = curr_;
    curr_   = compute();
}

ShakeOffset TraumaManager::compute() const {
    const float s = shake();
    if (s <= 0.0f) return {};

    ShakeOffset o;
    o.translation = s * cfg_.maxTranslation *
                    glm::vec3(channel(time_, 0), channel(time_, 1), channel(time_, 2));
    o.rotation    = s * cfg_.maxRotation *
                    glm::vec3(channel(time_, 3), channel(time_, 4), channel(time_, 5));
    return o;
}

ShakeOffset TraumaManager::sample(float alpha) const {
    return {glm::mix(prev_.translation, curr_.translation, alpha),
            glm::mix(prev_.rotation,    curr_.rotation,    alpha)};
}

} // namespace carnage
