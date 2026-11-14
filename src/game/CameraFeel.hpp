#pragma once
#include "game/Movement.hpp"
#include "game/Trauma.hpp"

#include <glm/glm.hpp>

namespace carnage {

// Damped harmonic oscillator, semi-implicit Euler (stable while omega*dt < ~1):
//   a = -w^2 (x - target) - 2 zeta w v
struct Spring1 {
    float x = 0.0f, v = 0.0f;
    void step(float dt, float omega, float zeta, float target = 0.0f) {
        const float a = -omega * omega * (x - target) - 2.0f * zeta * omega * v;
        v += a * dt;
        x += v * dt;
    }
};

struct CameraFeelConfig {
    // Head bob: phase advances by 2*pi*speed/stride per second.
    float strideWalk   = 2.0f;    // metres per full left+right cycle
    float strideSprint = 2.9f;
    float bobY    = 0.035f;       // vertical, two bounces per cycle
    float bobX    = 0.022f;       // lateral sway, once per cycle
    float bobRoll = glm::radians(0.9f);
    float bobPitch = glm::radians(0.55f);

    // Velocity lag: offset = -(v - lagged(v)) * lagGain, lagged() is an
    // exponential filter with rate lagRate. Zero at steady speed.
    float lagGain = 0.03f;
    float lagRate = 8.0f;
    float lagMax  = 0.12f;

    // Tilt: roll target = -cornerGain * yawRate * speedT - strafeGain * vRight/sprint
    float cornerGain = 0.060f;    // radians of roll per (rad/s of yaw) at full speed
    float strafeGain = glm::radians(3.0f);
    float maxTilt    = glm::radians(9.0f);
    float tiltOmega  = 14.0f;
    float tiltZeta   = 0.8f;

    // Impact springs.
    float impactOmega = 15.0f;
    float impactZeta  = 0.42f;
};

// Additive first-person camera feedback: bob, lag, corner tilt, landing and
// damage snaps. Fixed-tick update, render-rate interpolation, and the result
// is summed into the TraumaManager's ShakeOffset so Camera needs no changes.
class CameraFeel {
public:
    explicit CameraFeel(const CameraFeelConfig& cfg = {}) : cfg_(cfg) {}

    struct Inputs {
        glm::vec3 velocity{0.0f};
        float     yaw     = 0.0f;   // radians, camera yaw
        MoveState state   = MoveState::Walking;
        bool      grounded = true;
        MoveEvents events{};
    };

    void tick(float dt, const Inputs& in);

    // Impulses from gameplay.
    void onDamage(float amount01, float sideSign);  // sideSign: +1 hit from the right
    void onFire(float strength, float side = 0.0f);  // side: -1 left hand, +1 right                    // pushes the camera back

    ShakeOffset sample(float alpha) const;
    bool        footstep() const { return footstep_; }
    float       bobPhase() const { return phase_; }

private:
    ShakeOffset compute() const;

    CameraFeelConfig cfg_;
    float phase_ = 0.0f, bobGain_ = 0.0f, sprintT_ = 0.0f;
    float prevYaw_ = 0.0f;
    bool  haveYaw_ = false, footstep_ = false;
    glm::vec3 lagVel_{0.0f}, lagOffset_{0.0f};
    Spring1 roll_, pitch_, yawKick_, dropY_, backZ_;
    float   rollTarget_ = 0.0f;
    ShakeOffset prev_{}, curr_{};
};

} // namespace carnage
