#pragma once
#include <cstdint>
#include <cmath>

#include <glm/glm.hpp>

namespace carnage {

// Small, fast, deterministic xorshift32. Gameplay (spread) and visuals use
// separate instances so cosmetic randomness never perturbs the simulation.
class Rng {
public:
    explicit Rng(std::uint32_t seed = 0x9E3779B9u) : s_(seed ? seed : 1u) {}

    std::uint32_t next() {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }
    float uniform()             { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }  // [0,1)
    float signedUnit()          { return uniform() * 2.0f - 1.0f; }                                  // [-1,1)
    float range(float a, float b) { return a + (b - a) * uniform(); }

    glm::vec3 unitSphere() {
        for (;;) {
            const glm::vec3 v{signedUnit(), signedUnit(), signedUnit()};
            const float l = glm::dot(v, v);
            if (l > 1e-4f && l <= 1.0f) return v * (1.0f / std::sqrt(l));
        }
    }

private:
    std::uint32_t s_;
};

} // namespace carnage
