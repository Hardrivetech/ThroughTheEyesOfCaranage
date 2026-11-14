#pragma once
#include <algorithm>
#include <cmath>

namespace carnage {

// Muzzle-flash blind: a 0..1 value that spikes on each shot and decays
// exponentially. Fixed-tick update, render-rate interpolation; the renderer
// uses it to blow out the image.
class Blind {
public:
    void add(float amount) { cur_ = std::min(cur_ + amount, kMax); }

    void update(float dt) {
        prev_ = cur_;
        cur_ *= std::exp(-kDecay * dt);
        if (cur_ < 1e-3f) cur_ = 0.0f;
    }

    float sample(float alpha) const { return prev_ + (cur_ - prev_) * alpha; }

private:
    static constexpr float kMax   = 0.9f;
    static constexpr float kDecay = 5.0f;  // 1/s
    float prev_ = 0.0f, cur_ = 0.0f;
};

} // namespace carnage
