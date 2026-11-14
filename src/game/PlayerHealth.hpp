#pragma once
#include <algorithm>

namespace carnage {

// Pure player vitals: no world, audio or rendering dependencies, so the rules
// (armor split, invulnerability frames, death timing) are unit-testable.
// All numbers are untuned guesses.
struct PlayerHealth {
    static constexpr float kMaxHealth    = 100.0f;
    static constexpr float kMaxArmor     = 100.0f;
    static constexpr float kStartArmor   = 50.0f;
    static constexpr float kArmorAbsorb  = 0.5f;   // fraction of a hit armor soaks up
    static constexpr float kIFrameTime   = 0.45f;  // seconds of immunity after a hit
    static constexpr float kFadeTime     = 1.2f;   // death: time to fully fade out
    static constexpr float kRespawnDelay = 2.4f;   // death: time until the reload

    float health   = kMaxHealth;
    float armor    = kStartArmor;
    float iFrames  = 0.0f;
    float deadTime = 0.0f;
    bool  dead     = false;

    // Returns true if damage was applied. Explosions pass `ignoreIFrames` so a
    // blast is never swallowed by the grace window of an earlier bullet.
    bool damage(float amount, bool ignoreIFrames = false) {
        if (dead || amount <= 0.0f) return false;
        if (iFrames > 0.0f && !ignoreIFrames) return false;

        const float absorbed = std::min(armor, amount * kArmorAbsorb);
        armor  -= absorbed;
        health -= amount - absorbed;
        iFrames = std::max(iFrames, kIFrameTime);
        if (health <= 0.0f) {
            health   = 0.0f;
            dead     = true;
            deadTime = 0.0f;
        }
        return true;
    }

    void heal(float a)     { if (!dead) health = std::min(kMaxHealth, health + a); }
    void addArmor(float a) { if (!dead) armor  = std::min(kMaxArmor, armor + a); }

    void update(float dt) {
        iFrames = std::max(0.0f, iFrames - dt);
        if (dead) deadTime += dt;
    }

    float health01() const { return health / kMaxHealth; }
    float armor01() const  { return armor / kMaxArmor; }
    float deathFade() const { return dead ? std::clamp(deadTime / kFadeTime, 0.0f, 1.0f) : 0.0f; }
    bool  readyToRespawn() const { return dead && deadTime >= kRespawnDelay; }

    void reset() { *this = PlayerHealth{}; }
};

} // namespace carnage
