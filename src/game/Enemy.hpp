#pragma once
#include "game/Combat.hpp"
#include "game/World.hpp"
#include "core/Random.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace carnage {

// Seized: staggered by a heavy melee hit, then optionally carried (human
// shield) and thrown. The brain is suspended for the whole time.
enum class AiState { Idle, Patrol, Chase, Attack, Seized };

enum class SeizeSub { None, Staggered, Held, Thrown };

inline const char* name(AiState s) {
    switch (s) {
        case AiState::Idle:   return "Idle";
        case AiState::Patrol: return "Patrol";
        case AiState::Chase:  return "Chase";
        case AiState::Attack: return "Attack";
        case AiState::Seized: return "Seized";
    }
    return "?";
}

// All values are untuned guesses.
struct EnemyConfig {
    float sightRange    = 18.0f;
    float fovCos        = 0.34f;   // cos(70 deg): half-angle of the vision cone
    float hearRange     = 3.5f;    // proximity: noticed regardless of facing / walls
    float attackRange   = 1.7f;
    float loseTime      = 3.0f;    // seconds without sight before giving up the chase
    float idleTime      = 1.5f;
    float patrolTime    = 5.0f;
    float patrolSpeed   = 1.6f;
    float chaseSpeed    = 4.2f;
    float accel         = 14.0f;
    float attackWindup  = 0.45f;   // time from entering Attack to the blow landing
    float attackRecover = 0.80f;   // time after the blow before the next decision
    float patrolRadius  = 3.0f;    // metres along Z either side of the origin
    float radius        = 0.45f;   // circle used for wall collision (covers box XZ extents)
    float stepHeight    = 0.4f;
    float eyeHeight     = 1.6f;

    // Melee lunge: a short forward burst at the end of the windup.
    float meleeDamage   = 12.0f;
    float lungeTime     = 0.22f;
    float lungeSpeed    = 9.0f;
    float lungeStopDist = 0.9f;    // closes to this distance, never through the player

    // Ranged hitscan.
    float rangedRange   = 14.0f;   // starts shooting inside this distance
    float fireInterval  = 1.1f;    // cooldown after each shot (replaces attackRecover)
    float rangedWindup  = 0.35f;   // aim time before each shot
    float rangedDamage  = 8.0f;
    float spread        = 0.07f;   // tangent of the cone half-angle
    float playerRadius  = 0.45f;   // a shot whose aim point lands outside this misses

    // Seize / human shield / throw.
    float staggerTime   = 2.5f;    // window in which a staggered enemy can be grabbed
    float grabReach     = 2.4f;
    float grabRadius    = 0.9f;    // max distance from the view ray
    float throwMass     = 80.0f;   // kg, scales impact damage
    float throwEnergy   = 0.04f;   // damage per joule
    float throwBase     = 10.0f;
    float throwMaxDmg   = 160.0f;
    float shieldMeleeScale = 0.5f; // melee damage to the player while a shield is held
};

enum class EnemyType { Melee, Ranged };

// What the enemy currently knows about the player.
struct Perception {
    float dist    = 1e9f;
    bool  visible = false;  // in sight cone with clear LOS, or inside hearing range
};

struct Enemy {
    AiState     state = AiState::Idle;
    float       stateTime = 0.0f;   // time in the current state
    float       lostTime  = 0.0f;   // chase: time since the player was last seen
    bool        strikeDone = false; // attack: blow already delivered this cycle
    glm::vec3   pos{0.0f};          // feet
    glm::vec3   origin{0.0f};
    glm::vec2   vel{0.0f};
    float       yaw = 0.0f;         // forward = (sin yaw, 0, cos yaw)
    float       patrolDir = 1.0f;
    std::size_t target = 0;         // index into Combat::targets()
    bool        wasAlive = true;
    EnemyType   type   = EnemyType::Melee;
    int         sector = -1;        // objective sector id; -1 = free roaming
    float       stride = 0.0f;      // distance walked since the last footstep
    SeizeSub    sub = SeizeSub::None;  // only meaningful while state == Seized
    float       seizeTimer = 0.0f;     // stagger time remaining
    glm::vec3   vel3{0.0f};            // thrown flight velocity
    bool        asleep    = false;     // far from the player: the brain is not run (see ActivationConfig)
    float       keepAwake = 0.0f;      // seconds left during which this enemy may not fall asleep
    float       lastHp    = -1.0f;     // hit points last tick, to notice damage while asleep (-1 = unknown)
};

inline float attackReach(const Enemy& e, const EnemyConfig& c) {
    return e.type == EnemyType::Ranged ? c.rangedRange : c.attackRange;
}
inline float attackWindup(const Enemy& e, const EnemyConfig& c) {
    return e.type == EnemyType::Ranged ? c.rangedWindup : c.attackWindup;
}
inline float attackCooldown(const Enemy& e, const EnemyConfig& c) {
    return e.type == EnemyType::Ranged ? c.fireInterval : c.attackRecover;
}

inline void enterState(Enemy& e, AiState s) {
    e.state = s;
    e.stateTime = 0.0f;
    e.lostTime = 0.0f;
    e.strikeDone = false;
}

// Pure transition logic (no world access) so it is trivially unit-testable.
// Returns true on the tick the attack blow lands.
inline bool stepState(Enemy& e, const EnemyConfig& c, const Perception& p, float dt) {
    e.stateTime += dt;
    const float reach   = attackReach(e, c);
    const float windup  = attackWindup(e, c);
    const bool  inReach = p.visible && p.dist <= reach;
    switch (e.state) {
        case AiState::Idle:
            if (p.visible) enterState(e, AiState::Chase);
            else if (e.stateTime >= c.idleTime) enterState(e, AiState::Patrol);
            break;
        case AiState::Patrol:
            if (p.visible) enterState(e, AiState::Chase);
            else if (e.stateTime >= c.patrolTime) enterState(e, AiState::Idle);
            break;
        case AiState::Seized:
            break;  // driven by Enemies::stepSeized, not the perception brain
        case AiState::Chase:
            e.lostTime = p.visible ? 0.0f : e.lostTime + dt;
            if (inReach) enterState(e, AiState::Attack);
            else if (e.lostTime >= c.loseTime) enterState(e, AiState::Patrol);
            break;
        case AiState::Attack: {
            bool landed = false;
            // LOS is re-checked every tick: losing sight or range during the
            // windup aborts the attack, and the blow only lands if the target
            // is still visible at the moment of release.
            if (!e.strikeDone && (!p.visible || p.dist > reach * 1.6f)) {
                enterState(e, AiState::Chase);
                return false;
            }
            if (!e.strikeDone && e.stateTime >= windup) {
                e.strikeDone = true;
                landed = p.visible && p.dist <= reach * 1.3f;
            }
            if (e.strikeDone && e.stateTime >= windup + attackCooldown(e, c))
                enterState(e, p.dist <= reach * 1.3f && p.visible ? AiState::Attack : AiState::Chase);
            return landed;
        }
    }
    return false;
}

// One attack released this tick. `damage` is only non-zero when it hit the
// player; ranged misses still carry a tracer segment (from -> to).
struct EnemyAttack {
    bool      ranged = false;
    float     damage = 0.0f;
    glm::vec3 from{0.0f};   // attacker feet (melee) or muzzle (ranged)
    glm::vec3 to{0.0f};     // tracer end (ranged)
    bool      blocked = false;  // ranged shot ended on world geometry
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    bool      shielded = false;  // absorbed by the human shield (damage == 0)
};

// Distance-based AI activation. This is a performance setting, not balance: an enemy far
// from the player cannot see or hear them, so its brain (vision raycast, movement, ground
// and wall queries) is simply not run until the player comes near, and it stands still.
// Sleeping enemies still exist: they count as alive for objectives and can be shot.
struct ActivationConfig {
    float wakeRadius  = 50.0f;  // metres: a sleeping enemy wakes when the player is this close. <= 0 turns
                                // sleeping off entirely (every enemy always thinks)
    float sleepRadius = 65.0f;  // an awake enemy sleeps only when the player is farther than this. It must
                                // exceed wakeRadius (enforced, with a gap of at least kMinGap): the band
                                // between them is the hysteresis that stops an enemy on the boundary
                                // flipping between asleep and awake
    float keepAwake   = 10.0f;  // seconds a hurt or alerted enemy stays awake however far away the player is
    float groupRadius = 18.0f;  // a hurt enemy rouses sleeping enemies within this distance of it
    float shotAlertRadius  = 35.0f;  // a gunshot, melee blow or blast wakes sleepers this close to it
    float meleeAlertRadius = 8.0f;
    float blastAlertRadius = 45.0f;

    static constexpr float kMinGap = 2.0f;
    bool enabled() const { return wakeRadius > 0.0f; }
};

// Returns `a` with impossible values fixed (see the field comments).
inline ActivationConfig sanitized(ActivationConfig a) {
    if (!(a.wakeRadius > 0.0f)) { a.wakeRadius = 0.0f; return a; }  // also catches NaN
    a.sleepRadius       = std::max(a.sleepRadius, a.wakeRadius + ActivationConfig::kMinGap);
    a.keepAwake         = std::max(a.keepAwake, 0.0f);
    a.groupRadius       = std::max(a.groupRadius, 0.0f);
    a.shotAlertRadius   = std::max(a.shotAlertRadius, 0.0f);
    a.meleeAlertRadius  = std::max(a.meleeAlertRadius, 0.0f);
    a.blastAlertRadius  = std::max(a.blastAlertRadius, 0.0f);
    return a;
}

// How many enemies were thinking, standing by, or dead on the last tick.
struct EnemyActivity {
    int awake = 0, asleep = 0, dead = 0;
};

// Owns the AI brains; each brain drives a Combat::Target so hitscan, melee,
// explosions and seizing all work on enemies without special cases.
class Enemies {
public:
    explicit Enemies(const EnemyConfig& cfg = {}) : cfg_(cfg) {}

    // Distance-based sleeping (see ActivationConfig). Values are sanitized; turning it off
    // wakes everyone.
    void setActivation(const ActivationConfig& a);
    const ActivationConfig& activation() const { return act_; }
    // Wakes every sleeping enemy within `radius` of `pos` and keeps it awake for
    // activation().keepAwake seconds (a gunshot, an explosion, a shout).
    void alert(const glm::vec3& pos, float radius);
    EnemyActivity activity() const { return activity_; }

    // sector >= 0 marks an objective enemy: it never respawns, so "cleared"
    // is a stable condition.
    void add(Combat& combat, glm::vec3 feet, EnemyType type = EnemyType::Melee, int sector = -1);
    void clear() { enemies_.clear(); attacks_.clear(); held_ = -1; impact_ = 0.0f; activity_ = {}; }

    // Call after Combat::update so the interpolation snapshot is consistent.
    // Attacks released this tick are available from attacks() until the next call.
    void tick(float dt, Combat& combat, const World& world,
              glm::vec3 playerFeet, float playerEyeHeight);
    const std::vector<EnemyAttack>& attacks() const { return attacks_; }
    // World positions of enemy footsteps this tick (for spatial audio).
    const std::vector<glm::vec3>& footsteps() const { return footsteps_; }

    // Living enemies assigned to a sector (for objective triggers).
    // ---- seize / shield / throw (indices are into enemies()) ----------------
    // Staggers the enemy driving Combat target `targetIndex`. False if it is
    // not an enemy or is already seized.
    bool stagger(std::size_t targetIndex);
    // Nearest staggered enemy along the view ray, or -1.
    int  findStaggered(const glm::vec3& eye, const glm::vec3& forward) const;
    void grab(int enemyIndex);
    // Hurls the held enemy with the given velocity.
    void throwHeld(const glm::vec3& velocity);
    int  heldIndex() const { return held_; }
    // Where the held enemy's feet are placed each tick (in front of the player).
    void setCarryPoint(const glm::vec3& feet) { carry_ = feet; }
    // Strongest throw impact since the last call, 0..1 (camera feedback).
    float takeImpact() { const float v = impact_; impact_ = 0.0f; return v; }

    int aliveInSector(const Combat& combat, int sector) const;
    int totalInSector(int sector) const;

    const std::vector<Enemy>& enemies() const { return enemies_; }
    const EnemyConfig&        config() const { return cfg_; }

    // LOS + cone + proximity check, exposed for tests and debug drawing.
    Perception perceive(const Enemy& e, const World& world, glm::vec3 playerFeet,
                        float playerEyeHeight) const;

private:
    // Decides whether `e` thinks this tick. Returns false while it sleeps.
    bool updateActivation(Enemy& e, Target& t, const glm::vec3& playerFeet, float dt);
    void move(Enemy& e, glm::vec2 wish, float speed, const World& world, float dt);
    void stepSeized(Enemy& e, Target& t, Combat& combat, const World& world, float dt);

    EnemyConfig        cfg_;
    ActivationConfig   act_;
    EnemyActivity      activity_;
    std::vector<Enemy> enemies_;
    std::vector<EnemyAttack> attacks_;
    std::vector<glm::vec3>   footsteps_;
    Rng                rng_{0xC0FFEEu};
    int                held_   = -1;
    glm::vec3          carry_{0.0f};
    float              impact_ = 0.0f;
};

} // namespace carnage
