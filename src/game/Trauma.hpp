#pragma once
#include <glm/glm.hpp>

namespace carnage {

struct TraumaConfig {
    float     decayPerSecond = 0.9f;   // linear trauma loss per second
    float     frequency      = 22.0f;  // noise samples per second
    glm::vec3 maxTranslation{0.15f, 0.15f, 0.10f};                // metres
    glm::vec3 maxRotation{glm::radians(5.0f), glm::radians(5.0f),
                          glm::radians(10.0f)};                   // pitch, yaw, roll
};

struct ShakeOffset {
    glm::vec3 translation{0.0f};  // camera-local metres
    glm::vec3 rotation{0.0f};     // pitch, yaw, roll in radians
};

// Trauma-driven procedural camera shake.
//   offset = maxOffset * trauma^2 * perlin(t)
// Call update() from the fixed tick, sample() from the render loop.
class TraumaManager {
public:
    TraumaManager() = default;
    explicit TraumaManager(const TraumaConfig& cfg) : cfg_(cfg) {}

    void  add(float amount);                 // clamped to [0,1]
    void  update(float dt);
    float trauma() const { return trauma_; }
    float shake()  const { return trauma_ * trauma_; }

    // Interpolates the last two fixed-tick samples.
    ShakeOffset sample(float alpha) const;

private:
    ShakeOffset compute() const;

    TraumaConfig cfg_{};
    float        trauma_ = 0.0f;
    double       time_   = 0.0;
    ShakeOffset  prev_{};
    ShakeOffset  curr_{};
};

} // namespace carnage
