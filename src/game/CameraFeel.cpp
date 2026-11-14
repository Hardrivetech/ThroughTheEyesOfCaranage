#include "game/CameraFeel.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {
constexpr float kSprintRef = 10.5f;

float wrapPi(float a) { return std::remainder(a, glm::two_pi<float>()); }
} // namespace

void CameraFeel::tick(float dt, const Inputs& in) {
    prev_     = curr_;
    footstep_ = false;

    const glm::vec2 h{in.velocity.x, in.velocity.z};
    const float speed = glm::length(h);

    // Yaw rate from the camera yaw delta (mouse-driven, so measured, not input).
    float yawRate = 0.0f;
    if (haveYaw_) yawRate = wrapPi(in.yaw - prevYaw_) / dt;
    prevYaw_ = in.yaw;
    haveYaw_ = true;
    yawRate  = std::clamp(yawRate, -12.0f, 12.0f);

    // Camera-local velocity axes (yaw only).
    const glm::vec3 right{std::cos(in.yaw), 0.0f, -std::sin(in.yaw)};
    const glm::vec3 fwd{-std::sin(in.yaw), 0.0f, -std::cos(in.yaw)};
    const float vRight = glm::dot(in.velocity, right);
    const float vFwd   = glm::dot(in.velocity, fwd);

    // --- Head bob -----------------------------------------------------------
    const bool  stepping = in.grounded && (in.state == MoveState::Walking ||
                                           in.state == MoveState::Sprinting);
    sprintT_ += ((in.state == MoveState::Sprinting ? 1.0f : 0.0f) - sprintT_) *
                (1.0f - std::exp(-6.0f * dt));
    const float stride  = glm::mix(cfg_.strideWalk, cfg_.strideSprint, sprintT_);
    const float gainTgt = stepping ? std::clamp(speed / kSprintRef, 0.0f, 1.0f) : 0.0f;
    bobGain_ += (gainTgt - bobGain_) * (1.0f - std::exp(-10.0f * dt));

    const float prevPhase = phase_;
    if (stepping) phase_ += glm::two_pi<float>() * (speed / stride) * dt;
    if (std::floor(phase_ / glm::pi<float>()) != std::floor(prevPhase / glm::pi<float>()) &&
        bobGain_ > 0.2f)
        footstep_ = true;

    // --- Velocity lag ---------------------------------------------------------
    lagVel_ += (in.velocity - lagVel_) * (1.0f - std::exp(-cfg_.lagRate * dt));
    const glm::vec3 d = in.velocity - lagVel_;
    glm::vec3 lag{-glm::dot(d, right), -d.y, glm::dot(d, fwd)};  // camera-local, +z = back
    lag *= cfg_.lagGain;
    const float ll = glm::length(lag);
    if (ll > cfg_.lagMax) lag *= cfg_.lagMax / ll;
    lagOffset_ = lag;

    // --- Tilt: corners + strafing --------------------------------------------
    const float speedT = std::clamp(speed / kSprintRef, 0.0f, 1.5f);
    rollTarget_ = -cfg_.cornerGain * yawRate * speedT - cfg_.strafeGain * (vRight / kSprintRef);
    rollTarget_ = std::clamp(rollTarget_, -cfg_.maxTilt, cfg_.maxTilt);
    (void)vFwd;

    // --- Event impulses -----------------------------------------------------------
    const MoveEvents& ev = in.events;
    if (ev.landImpact > 4.0f) {
        const float k = std::clamp((ev.landImpact - 4.0f) / 20.0f, 0.0f, 1.2f);
        dropY_.v  -= 2.4f * k;
        pitch_.v  -= 1.7f * k;   // head snaps down
        backZ_.v  += 1.6f * k;   // and back
    }
    if (ev.jumped)         { dropY_.v += 0.9f; backZ_.v += 0.3f; }
    if (ev.vaultStarted)   { dropY_.v -= 1.3f; pitch_.v -= 0.6f; }
    if (ev.vaultFinished)  { dropY_.v -= 1.8f; backZ_.v += 0.8f; }
    if (ev.slideStarted)   { dropY_.v -= 0.9f; roll_.v += 0.0f; }

    // --- Integrate ------------------------------------------------------------------
    roll_.step(dt, cfg_.tiltOmega, cfg_.tiltZeta, rollTarget_);
    pitch_.step(dt, cfg_.impactOmega, cfg_.impactZeta);
    yawKick_.step(dt, cfg_.impactOmega, cfg_.impactZeta);
    dropY_.step(dt, cfg_.impactOmega, cfg_.impactZeta);
    backZ_.step(dt, cfg_.impactOmega, cfg_.impactZeta);

    curr_ = compute();
}

ShakeOffset CameraFeel::compute() const {
    const float g = bobGain_ * (0.65f + 0.35f * sprintT_);
    ShakeOffset o;
    o.translation = glm::vec3(cfg_.bobX * g * std::sin(phase_) + lagOffset_.x,
                              cfg_.bobY * g * std::cos(2.0f * phase_) + dropY_.x + lagOffset_.y,
                              backZ_.x + lagOffset_.z);
    o.rotation    = glm::vec3(pitch_.x + cfg_.bobPitch * g * std::cos(2.0f * phase_),
                              yawKick_.x,
                              roll_.x + cfg_.bobRoll * g * std::sin(phase_));
    return o;
}

void CameraFeel::onDamage(float amount, float side) {
    const float a = std::clamp(amount, 0.0f, 1.0f);
    pitch_.v   -= 2.2f * a;
    roll_.v    -= side * 3.5f * a;
    yawKick_.v += side * 1.4f * a;
    backZ_.v   += 2.4f * a;
    dropY_.v   -= 1.2f * a;
}

void CameraFeel::onFire(float strength, float side) {
    backZ_.v += 2.6f * strength;
    pitch_.v += 0.5f * strength;
    roll_.v  -= side * 0.6f * strength;
    yawKick_.v += side * 0.25f * strength;
}

ShakeOffset CameraFeel::sample(float alpha) const {
    ShakeOffset o;
    o.translation = glm::mix(prev_.translation, curr_.translation, alpha);
    o.rotation    = glm::mix(prev_.rotation, curr_.rotation, alpha);
    return o;
}

} // namespace carnage
