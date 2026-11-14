#pragma once
#include "core/Random.hpp"
#include "game/Combat.hpp"
#include "game/Recoil.hpp"

#include <glm/glm.hpp>
#include <glm/trigonometric.hpp>

namespace carnage {

struct WeaponStats {
    const char* name = "";

    // Firing
    float damage     = 10.0f;   // per pellet / direct hit
    float rpm        = 600.0f;
    int   magSize    = 10;
    float reloadTime = 1.5f;
    bool  automatic  = false;
    int   pellets    = 1;
    float spreadDeg  = 0.0f;
    float range      = 250.0f;

    // Recoil: pitch/yaw kick per shot (radians), then recovery.
    float recoilPitch  = glm::radians(1.0f);
    float recoilYaw    = glm::radians(0.3f);
    float recoilMax    = glm::radians(12.0f);
    float recoilDelay  = 0.08f;                 // seconds before recovery starts
    float recoilRate   = 9.0f;                  // exponential recovery, 1/s
    float recoilLinear = glm::radians(1.5f);    // linear floor, rad/s
    float recoilSpikeChance = 0.08f;            // chance a shot kicks much harder
    float trauma       = 0.2f;                  // camera shake per shot

    // Projectile weapons
    float projSpeed    = 0.0f;
    float projRadius   = 0.12f;
    float projGravity  = 0.0f;
    float projLifetime = 5.0f;
    float blastRadius  = 0.0f;
    float blastDamage  = 0.0f;
    float knockback    = 0.0f;
    bool  infiniteAmmo = false;  // melee weapons (fists)

    // Presentation
    float      length    = 0.4f;                      // viewmodel length to the muzzle
    glm::vec2  viewSize{0.05f, 0.09f};                // viewmodel width, height
    glm::vec3  color{0.2f};
    CasingKind casing    = CasingKind::Brass;
    float      flashSize = 0.07f;
};

struct Aim {
    glm::vec3 origin{0.0f};   // eye
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 muzzle{0.0f};
};

struct TriggerInput {
    bool held    = false;
    bool pressed = false;
    bool reload  = false;
};

struct WeaponEvents {
    bool fired          = false;
    bool dryFired       = false;
    bool reloadStarted  = false;
    bool reloadFinished = false;
};

enum class WeaponState { Ready, Cooldown, Reloading, Empty };

// Shared state machine: fire-rate pacing with exact carry-over, trigger
// buffering, ammo, reload and recoil. Subclasses decide how a shot resolves.
class Weapon {
public:
    explicit Weapon(const WeaponStats& stats) : stats_(stats), ammo_(stats.magSize) {}
    virtual ~Weapon() = default;

    // Fixed tick. `aim` must already include the current recoil.
    WeaponEvents tick(float dt, const TriggerInput& in, const Aim& aim, Combat& combat, Rng& rng);

    void equip();
    void refill() { ammo_ = stats_.magSize; reloading_ = false; reloadLeft_ = 0.0f; }  // dev

    const WeaponStats& stats() const { return stats_; }
    int         ammo() const { return ammo_; }
    int         magSize() const { return stats_.magSize; }
    bool        reloading() const { return reloading_; }
    WeaponState state() const;
    const Recoil& recoil() const { return recoil_; }

    // Presentation (0..1 style values, interpolated for render rate).
    float kick(float alpha) const { return prevKick_ + (kick_ - prevKick_) * alpha; }
    float reloadProgress() const { return reloading_ ? 1.0f - reloadLeft_ / stats_.reloadTime : -1.0f; }
    bool  flashing() const { return flash_ > 0.0f; }
    float flashT() const { return flash_ / kFlashTime; }

protected:
    virtual void shoot(const Aim& aim, Combat& combat, Rng& rng) = 0;

    const WeaponStats stats_;

private:
    static constexpr float kFlashTime     = 0.05f;
    static constexpr float kTriggerBuffer = 0.12f;

    void startReload(WeaponEvents& ev);

    int    ammo_;
    bool   reloading_  = false;
    float  reloadLeft_ = 0.0f;
    float  timeToNext_ = 0.0f;
    float  buffered_   = 0.0f;
    float  kick_ = 0.0f, prevKick_ = 0.0f;
    float  flash_ = 0.0f;
    Recoil recoil_;
};

} // namespace carnage
