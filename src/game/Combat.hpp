#pragma once
#include "core/DrawTypes.hpp"
#include "game/World.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace carnage {

enum class CasingKind { None, Brass, Shell };

// A box-shaped dummy. The top `headFraction` of its height is a head zone
// worth double damage. Optionally patrols back and forth along an axis.
struct Target {
    glm::vec3 center{0.0f};
    glm::vec3 prevCenter{0.0f};
    glm::vec3 half{0.45f, 0.9f, 0.25f};
    glm::vec3 color{0.85f, 0.30f, 0.15f};
    float hp           = 100.0f;
    float maxHp        = 100.0f;
    float headFraction = 0.22f;
    float flash        = 0.0f;
    float respawnTimer = 0.0f;
    bool  respawns     = true;   // false: stays dead (objective enemies)
    bool  claimed      = false;  // corpse already picked up (cleared on revive)

    glm::vec3 patrolOrigin{0.0f};
    glm::vec3 patrolAxis{1.0f, 0.0f, 0.0f};
    float     patrolAmp   = 0.0f;
    float     patrolSpeed = 1.4f;
    float     patrolPhase = 0.0f;

    // Corpse AABB (a dead target lying on the floor), used for rendering and
    // for grabbing. Only dead, unclaimed targets are grabbable.
    bool      isCorpse() const { return !alive() && !claimed; }
    glm::vec3 corpseCenter() const { return {center.x, center.y - half.y + 0.2f, center.z}; }
    glm::vec3 corpseHalf() const { return {half.y, 0.2f, half.x + 0.1f}; }

    bool      alive() const { return hp > 0.0f; }
    glm::vec3 min() const { return center - half; }
    glm::vec3 max() const { return center + half; }
    float     headBottom() const { return center.y + half.y - 2.0f * half.y * headFraction; }

    static Target dummy(glm::vec3 feetPosition, float patrolAmplitude = 0.0f);
};

enum class HitKind { Miss, World, Body, Head, Explosion };

// Everything visual or audible reacts to these; combat itself knows nothing
// about particles or the HUD.
struct HitEvent {
    HitKind   kind = HitKind::Miss;
    glm::vec3 pos{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 from{0.0f};       // tracer start (hitscan only)
    glm::vec3 color{0.5f};      // surface colour, for debris
    bool      tracer = false;
    bool      killed = false;
    float     damage    = 0.0f;
    float     radius    = 0.0f;  // explosions
    float     knockback = 0.0f;  // explosions
};

struct Projectile {
    glm::vec3 pos{0.0f};
    glm::vec3 prevPos{0.0f};
    glm::vec3 vel{0.0f};
    float radius      = 0.12f;
    float gravity     = 0.0f;
    float lifetime    = 5.0f;
    float age         = 0.0f;
    float damage      = 0.0f;   // direct hit (used when blastRadius == 0)
    float blastRadius = 0.0f;
    float blastDamage = 0.0f;
    float knockback   = 0.0f;
    bool  alive       = true;
};

class Combat {
public:
    explicit Combat(const World& world) : world_(world) {}

    const World& world() const { return world_; }
    // Returns the new target's index (stable until clear()).
    std::size_t addTarget(const Target& t) { targets_.push_back(t); return targets_.size() - 1; }
    Target* targetAt(std::size_t i) { return i < targets_.size() ? &targets_[i] : nullptr; }

    // Instant ray. `dir` must be normalised. The tracer is drawn from `muzzle` if `tracer` is true.
    void hitscan(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& muzzle,
                 float range, float damage, bool tracer = true);

    // The shot is swept from `origin` to the projectile's start position so a
    // muzzle that pokes through a wall still detonates on it.
    void spawn(const Projectile& p, const glm::vec3& origin);

    // Instant melee/impact sphere: damages every live target it overlaps and
    // emits Body/Head events. Returns the number of targets hit.
    int strike(const glm::vec3& center, float radius, float damage, const glm::vec3& dir);

    // Public blast (hazard barrels).
    void detonate(const glm::vec3& pos, float radius, float damage, float knockback);

    // Live targets can never be grabbed: enemies must be fought. Returns the
    // nearest unclaimed corpse within `radius` of `center` and marks it
    // claimed so it can be hurled. Returns its feet position.
    std::optional<glm::vec3> seize(const glm::vec3& center, float radius);
    bool canSeize(const glm::vec3& center, float radius) const;

    // Fixed tick: targets, then projectiles.
    void update(float dt);

    std::span<const Target>     targets() const { return targets_; }
    std::span<const Projectile> projectiles() const { return projectiles_; }
    const std::vector<HitEvent>& events() const { return events_; }
    void clearEvents() { events_.clear(); }
    // Drops all targets, projectiles and pending events (level unload).
    void clear() { targets_.clear(); projectiles_.clear(); events_.clear(); }

    void collect(float alpha, std::vector<DrawBox>& out) const;
    void collectLights(float alpha, std::vector<PointLight>& out) const;

private:
    bool advance(Projectile& p, const glm::vec3& from, const glm::vec3& to);
    void explode(const glm::vec3& pos, const glm::vec3& normal, float radius,
                 float damage, float knockback);
    bool applyDamage(Target& t, float damage);
    void registerTargetHit(Target& t, const glm::vec3& point, const glm::vec3& normal,
                           float damage, bool head, bool tracer, const glm::vec3& from);

    const World&            world_;
    std::vector<Target>     targets_;
    std::vector<Projectile> projectiles_;
    std::vector<HitEvent>   events_;
};

// Velocity change an explosion applies to a body centred at `bodyCenter`.
glm::vec3 blastImpulse(const HitEvent& explosion, const glm::vec3& bodyCenter);

} // namespace carnage
