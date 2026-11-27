#pragma once
#include "core/PostFx.hpp"
#include "game/Movement.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <span>

namespace carnage {

// The numbers behind the combat HUD, kept free of drawing so the rules can be unit-tested:
// how full the momentum gauge is, where a hit came from, whether a seize prompt or the
// human-shield panel should show, and when the crosshair warns of low health. Hud.cpp draws them.

// ---- momentum ------------------------------------------------------------------------------

enum class MomentumTier {
    Idle,       // standing still
    Walk,
    Run,        // faster than a walk, short of a sprint
    Sprint,     // at sprint speed (or wall-running)
    Overdrive,  // fast enough that the speed lines appear (kSpeedLineStart x sprint)
};

struct MomentumGauge {
    float        fill = 0.0f;        // 0..1 along the bar
    float        sprintMark = 0.0f;  // where sprint speed sits on the bar, 0..1
    float        wallRunMark = 0.0f; // where wall-run speed sits
    float        overdriveMark = 0.0f;  // where the speed lines start
    MomentumTier tier = MomentumTier::Idle;
};

// The gauge is full at twice sprint speed, so the interesting range (a walk up to a boosted
// slide or wall-run) fills most of the bar. Non-finite or negative speeds read as standing still.
// The Overdrive tier starts exactly where PostFx's speed lines do (kSpeedLineStart).
MomentumGauge momentumGauge(float horizontalSpeed, const MovementConfig& cfg = {});

// ---- directional damage --------------------------------------------------------------------

// Angle from where the player is facing to the attacker, in radians in (-pi, pi]:
// 0 = straight ahead, +pi/2 = on the right, +-pi = behind, -pi/2 = on the left. Only the ground
// plane counts, so an attacker above or below does not change it. `yaw` is the camera yaw
// (0 faces -Z, +pi/2 faces -X). Returns false (and leaves `angle` alone) when the attacker is
// on top of the player and there is no direction to show.
bool attackAngle(const glm::vec3& playerPos, float yaw, const glm::vec3& attackerPos, float& angle);

struct DamageArc {
    float angle    = 0.0f;  // see attackAngle
    float life     = 0.0f;  // seconds left
    float strength = 0.0f;  // 0..1, how hard the hit was
};

// The few arcs currently on screen. A new hit from roughly the same direction refreshes the
// existing arc instead of stacking another, so a burst of fire does not carpet the screen.
class DamageIndicators {
public:
    static constexpr float       kLife = 1.1f;          // seconds an arc stays
    static constexpr float       kMergeAngle = 0.35f;   // radians: hits this close share an arc
    static constexpr std::size_t kMax = 6;

    // `strength` is clamped to 0..1; non-finite values are ignored.
    void add(float angle, float strength);
    void update(float dt);
    void clear() { count_ = 0; }

    std::span<const DamageArc> arcs() const { return {arcs_.data(), count_}; }
    // 0..1 fade of an arc (1 when fresh).
    static float alpha(const DamageArc& a) { return std::clamp(a.life / kLife, 0.0f, 1.0f) * a.strength; }

private:
    std::array<DamageArc, kMax> arcs_{};
    std::size_t                 count_ = 0;
};

// ---- seize / human shield / throw ------------------------------------------------------------

enum class SeizeState {
    None,      // nothing to show
    CanSeize,  // a staggered enemy is within reach: show "[key] SEIZE"
    Holding,   // carrying one as a shield: show its integrity and the throw prompt
};

struct SeizeHud {
    SeizeState state = SeizeState::None;
    float      shield01 = 0.0f;       // the held enemy's remaining health, 0..1
    bool       shieldWeak = false;    // about to break
    bool       throwReady = false;    // can be thrown right now
    float      grip01 = 0.0f;         // 0..1 for the first moments of the hold (the ready pip fills)
};

inline constexpr float kShieldWeakBelow = 0.25f;
inline constexpr float kGripSeconds     = 0.35f;

// Holding wins over a seizable enemy in reach. The shield is the held enemy's health fraction
// (it soaks the shots aimed at the player); the throw is ready as long as the shield is alive.
SeizeHud seizeHud(bool holdingEnemy, bool staggeredInReach, float shieldHp, float shieldMaxHp, float heldSeconds);

// ---- crosshair --------------------------------------------------------------------------------

struct CrosshairWarning {
    bool  active = false;  // below kLowHealthHp and alive
    float pulse  = 0.0f;   // 0..1 heartbeat (the same one the screen vignette uses)
};
CrosshairWarning crosshairWarning(float hp, float timeSeconds);

} // namespace carnage
