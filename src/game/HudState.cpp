#include "game/HudState.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace carnage {

MomentumGauge momentumGauge(float horizontalSpeed, const MovementConfig& cfg) {
    MomentumGauge g;
    const float full = cfg.sprintSpeed * 2.0f;
    g.sprintMark     = std::clamp(cfg.sprintSpeed / full, 0.0f, 1.0f);
    g.wallRunMark    = std::clamp(cfg.wallRunSpeed / full, 0.0f, 1.0f);
    g.overdriveMark  = std::clamp(cfg.sprintSpeed * kSpeedLineStart / full, 0.0f, 1.0f);

    const float v = (std::isfinite(horizontalSpeed) && horizontalSpeed > 0.0f) ? horizontalSpeed : 0.0f;
    g.fill = std::clamp(v / full, 0.0f, 1.0f);
    if (v >= cfg.sprintSpeed * kSpeedLineStart)  g.tier = MomentumTier::Overdrive;
    else if (v >= cfg.sprintSpeed * 0.95f)       g.tier = MomentumTier::Sprint;
    else if (v >= cfg.walkSpeed * 1.05f)         g.tier = MomentumTier::Run;
    else if (v >= 0.5f)                          g.tier = MomentumTier::Walk;
    else                                         g.tier = MomentumTier::Idle;
    return g;
}

bool attackAngle(const glm::vec3& playerPos, float yaw, const glm::vec3& attackerPos, float& angle) {
    const glm::vec2 rel{attackerPos.x - playerPos.x, attackerPos.z - playerPos.z};
    if (!std::isfinite(rel.x) || !std::isfinite(rel.y) || !std::isfinite(yaw)) return false;
    if (glm::dot(rel, rel) < 0.3f * 0.3f) return false;   // on top of us: no direction to show

    // Same convention as the camera: forward = (-sin yaw, -cos yaw), right = (cos yaw, -sin yaw).
    const glm::vec2 forward{-std::sin(yaw), -std::cos(yaw)};
    const glm::vec2 right{std::cos(yaw), -std::sin(yaw)};
    angle = std::atan2(glm::dot(rel, right), glm::dot(rel, forward));
    return true;
}

namespace {

// Shortest signed distance between two angles.
float angleDiff(float a, float b) {
    constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
    float d = std::fmod(a - b, kTwoPi);
    if (d > std::numbers::pi_v<float>) d -= kTwoPi;
    if (d < -std::numbers::pi_v<float>) d += kTwoPi;
    return d;
}

} // namespace

void DamageIndicators::add(float angle, float strength) {
    if (!std::isfinite(angle) || !std::isfinite(strength)) return;
    strength = std::clamp(strength, 0.0f, 1.0f);

    for (std::size_t i = 0; i < count_; ++i) {
        DamageArc& a = arcs_[i];
        if (std::abs(angleDiff(angle, a.angle)) <= kMergeAngle) {
            a.angle    = a.angle + angleDiff(angle, a.angle) * 0.5f;   // drift towards the newest hit
            a.life     = kLife;
            a.strength = std::max(a.strength, strength);
            return;
        }
    }
    if (count_ < kMax) {
        arcs_[count_++] = {angle, kLife, strength};
        return;
    }
    // Full: replace the arc closest to fading out.
    std::size_t oldest = 0;
    for (std::size_t i = 1; i < count_; ++i)
        if (arcs_[i].life < arcs_[oldest].life) oldest = i;
    arcs_[oldest] = {angle, kLife, strength};
}

void DamageIndicators::update(float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) return;
    std::size_t kept = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        arcs_[i].life -= dt;
        if (arcs_[i].life > 0.0f) arcs_[kept++] = arcs_[i];
    }
    count_ = kept;
}

SeizeHud seizeHud(bool holdingEnemy, bool staggeredInReach, float shieldHp, float shieldMaxHp, float heldSeconds) {
    SeizeHud h;
    if (holdingEnemy) {
        h.state = SeizeState::Holding;
        const bool ok = std::isfinite(shieldHp) && std::isfinite(shieldMaxHp) && shieldMaxHp > 0.0f;
        h.shield01   = ok ? std::clamp(shieldHp / shieldMaxHp, 0.0f, 1.0f) : 0.0f;
        h.shieldWeak = h.shield01 < kShieldWeakBelow;
        h.throwReady = h.shield01 > 0.0f;
        h.grip01     = std::isfinite(heldSeconds) ? std::clamp(heldSeconds / kGripSeconds, 0.0f, 1.0f) : 0.0f;
    } else if (staggeredInReach) {
        h.state = SeizeState::CanSeize;
    }
    return h;
}

CrosshairWarning crosshairWarning(float hp, float timeSeconds) {
    CrosshairWarning w;
    w.active = lowHealthSeverity(hp) > 0.0f;
    w.pulse  = lowHealthPulse(hp, timeSeconds);
    return w;
}

} // namespace carnage
