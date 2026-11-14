#pragma once
#include "game/Combat.hpp"
#include "game/World.hpp"
#include "core/Random.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace carnage {

enum class AiState { Idle, Patrol, Chase, Attack };

inline const char* name(AiState s) {
    switch (s) {
        case AiState::Idle:   return "Idle";
        case AiState::Patrol: return "Patrol";
        case AiState::Chase:  return "Chase";
        case AiState::Attack: return "Attack";
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
};

// Owns the AI brains; each brain drives a Combat::Target so hitscan, melee,
// explosions and seizing all work on enemies without special cases.
class Enemies {
public:
    explicit Enemies(const EnemyConfig& cfg = {}) : cfg_(cfg) {}

    // sector >= 0 marks an objective enemy: it never respawns, so "cleared"
    // is a stable condition.
    void add(Combat& combat, glm::vec3 feet, EnemyType type = EnemyType::Melee, int sector = -1);
    void clear() { enemies_.clear(); attacks_.clear(); }

    // Call after Combat::update so the interpolation snapshot is consistent.
    // Attacks released this tick are available from attacks() until the next call.
    void tick(float dt, Combat& combat, const World& world,
              glm::vec3 playerFeet, float playerEyeHeight);
    const std::vector<EnemyAttack>& attacks() const { return attacks_; }
    // World positions of enemy footsteps this tick (for spatial audio).
    const std::vector<glm::vec3>& footsteps() const { return footsteps_; }

    // Living enemies assigned to a sector (for objective triggers).
    int aliveInSector(const Combat& combat, int sector) const;
    int totalInSector(int sector) const;

    const std::vector<Enemy>& enemies() const { return enemies_; }
    const EnemyConfig&        config() const { return cfg_; }

    // LOS + cone + proximity check, exposed for tests and debug drawing.
    Perception perceive(const Enemy& e, const World& world, glm::vec3 playerFeet,
                        float playerEyeHeight) const;

private:
    void move(Enemy& e, glm::vec2 wish, float speed, const World& world, float dt);

    EnemyConfig        cfg_;
    std::vector<Enemy> enemies_;
    std::vector<EnemyAttack> attacks_;
    std::vector<glm::vec3>   footsteps_;
    Rng                rng_{0xC0FFEEu};
};

} // namespace carnage
