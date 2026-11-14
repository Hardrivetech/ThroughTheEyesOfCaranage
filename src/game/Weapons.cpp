#include "game/Weapons.hpp"

#include <glm/gtc/constants.hpp>

#include <cmath>

namespace carnage {

namespace {

glm::vec3 spreadDirection(const Aim& a, float spreadDeg, Rng& rng) {
    const float tanSpread = std::tan(glm::radians(spreadDeg));
    if (tanSpread <= 0.0f) return a.forward;
    // Uniform over a disc.
    const float r  = tanSpread * std::sqrt(rng.uniform());
    const float th = rng.uniform() * glm::two_pi<float>();
    return glm::normalize(a.forward + a.right * (r * std::cos(th)) + a.up * (r * std::sin(th)));
}

} // namespace

void HitscanWeapon::shoot(const Aim& aim, Combat& combat, Rng& rng) {
    for (int i = 0; i < stats_.pellets; ++i) {
        combat.hitscan(aim.origin, spreadDirection(aim, stats_.spreadDeg, rng), aim.muzzle,
                       stats_.range, stats_.damage);
    }
}

void MeleeWeapon::shoot(const Aim& aim, Combat& combat, Rng& rng) {
    side_ = 1 - side_;
    // Melee punch: swept ray without bullet tracer
    combat.hitscan(aim.origin, spreadDirection(aim, stats_.spreadDeg, rng),
                   aim.origin + aim.forward * 0.4f, stats_.range, stats_.damage, false);
}

void ProjectileWeapon::shoot(const Aim& aim, Combat& combat, Rng&) {
    // Converge on whatever the crosshair is over so the rocket doesn't miss
    // just because the muzzle is offset from the eye.
    const float focusRange = 120.0f;
    glm::vec3 focus = aim.origin + aim.forward * focusRange;
    if (const auto hit = combat.world().raycast(aim.origin, aim.forward, focusRange))
        focus = aim.origin + aim.forward * hit->t;

    glm::vec3 dir = glm::normalize(focus - aim.muzzle);
    if (glm::dot(dir, aim.forward) < 0.5f) dir = aim.forward;  // target closer than the muzzle

    Projectile p;
    p.pos         = aim.muzzle;
    p.vel         = dir * stats_.projSpeed;
    p.radius      = stats_.projRadius;
    p.gravity     = stats_.projGravity;
    p.lifetime    = stats_.projLifetime;
    p.damage      = stats_.damage;
    p.blastRadius = stats_.blastRadius;
    p.blastDamage = stats_.blastDamage;
    p.knockback   = stats_.knockback;
    combat.spawn(p, aim.origin);
}

std::unique_ptr<Weapon> makeFists() {
    WeaponStats s;
    s.name = "Fists";
    s.damage = 45.0f;  s.rpm = 220.0f;  s.magSize = 1;  s.reloadTime = 0.0f;
    s.automatic = false;  s.spreadDeg = 1.0f;  s.range = 2.4f;
    s.infiniteAmmo = true;
    s.recoilPitch = glm::radians(3.2f);  s.recoilYaw = glm::radians(1.0f);
    s.recoilMax = glm::radians(10.0f);   s.recoilDelay = 0.06f;
    s.recoilRate = 12.0f;                s.recoilLinear = glm::radians(6.0f);
    s.trauma = 0.28f;  s.recoilSpikeChance = 0.0f;
    s.length = 0.20f;  s.viewSize = {0.08f, 0.08f};  s.color = {0.82f, 0.64f, 0.52f};
    s.casing = CasingKind::None;  s.flashSize = 0.0f;
    return std::make_unique<MeleeWeapon>(s);
}

std::unique_ptr<Weapon> makePistol() {
    WeaponStats s;
    s.name = "Pistol";
    s.damage = 34.0f;  s.rpm = 400.0f;  s.magSize = 12;  s.reloadTime = 1.1f;
    s.automatic = false;  s.spreadDeg = 0.25f;
    s.recoilPitch = glm::radians(2.2f);  s.recoilYaw = glm::radians(0.5f);
    s.recoilMax = glm::radians(10.0f);   s.recoilDelay = 0.10f;
    s.recoilRate = 10.0f;                s.recoilLinear = glm::radians(6.0f);
    s.trauma = 0.18f;
    s.length = 0.30f;  s.viewSize = {0.04f, 0.08f};  s.color = {0.25f, 0.25f, 0.30f};
    s.casing = CasingKind::Brass;  s.flashSize = 0.07f;
    return std::make_unique<HitscanWeapon>(s);
}

std::unique_ptr<Weapon> makeSmg() {
    WeaponStats s;
    s.name = "SMG";
    s.damage = 14.0f;  s.rpm = 850.0f;  s.magSize = 30;  s.reloadTime = 1.6f;
    s.automatic = true;  s.spreadDeg = 1.2f;
    s.recoilPitch = glm::radians(0.9f);  s.recoilYaw = glm::radians(0.5f);
    s.recoilMax = glm::radians(9.0f);    s.recoilDelay = 0.07f;
    s.recoilRate = 8.0f;                 s.recoilLinear = glm::radians(4.0f);
    s.trauma = 0.10f;
    s.length = 0.42f;  s.viewSize = {0.05f, 0.10f};  s.color = {0.18f, 0.20f, 0.25f};
    s.casing = CasingKind::Brass;  s.flashSize = 0.08f;
    return std::make_unique<HitscanWeapon>(s);
}

std::unique_ptr<Weapon> makeShotgun() {
    WeaponStats s;
    s.name = "Shotgun";
    s.damage = 12.0f;  s.rpm = 70.0f;  s.magSize = 6;  s.reloadTime = 2.4f;
    s.automatic = false;  s.pellets = 9;  s.spreadDeg = 3.2f;  s.range = 60.0f;
    s.recoilPitch = glm::radians(6.0f);  s.recoilYaw = glm::radians(1.0f);
    s.recoilMax = glm::radians(14.0f);   s.recoilDelay = 0.15f;
    s.recoilRate = 7.0f;                 s.recoilLinear = glm::radians(8.0f);
    s.trauma = 0.55f;
    s.length = 0.55f;  s.viewSize = {0.06f, 0.09f};  s.color = {0.35f, 0.25f, 0.15f};
    s.casing = CasingKind::Shell;  s.flashSize = 0.14f;
    return std::make_unique<HitscanWeapon>(s);
}

std::unique_ptr<Weapon> makeRocketLauncher() {
    WeaponStats s;
    s.name = "Rocket Launcher";
    s.damage = 0.0f;  s.rpm = 60.0f;  s.magSize = 1;  s.reloadTime = 1.8f;
    s.automatic = false;
    s.projSpeed = 38.0f;  s.projRadius = 0.15f;
    s.blastRadius = 5.0f;  s.blastDamage = 120.0f;  s.knockback = 18.0f;
    s.recoilPitch = glm::radians(7.0f);  s.recoilYaw = glm::radians(0.8f);
    s.recoilMax = glm::radians(12.0f);   s.recoilDelay = 0.20f;
    s.recoilRate = 6.0f;                 s.recoilLinear = glm::radians(8.0f);
    s.trauma = 0.70f;
    s.length = 0.70f;  s.viewSize = {0.09f, 0.12f};  s.color = {0.20f, 0.30f, 0.20f};
    s.casing = CasingKind::None;  s.flashSize = 0.16f;
    return std::make_unique<ProjectileWeapon>(s);
}

std::unique_ptr<Weapon> makeWeapon(std::size_t index) {
    switch (index) {
        case 0: return makeFists();
        case 1: return makePistol();
        case 2: return makeSmg();
        case 3: return makeShotgun();
        default: return makeRocketLauncher();
    }
}

} // namespace carnage
