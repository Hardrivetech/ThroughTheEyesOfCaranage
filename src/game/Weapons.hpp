#pragma once
#include "game/Weapon.hpp"

#include <memory>

namespace carnage {

// Instant-hit: pistols, SMGs, shotguns (pellets + spread cone).
class HitscanWeapon final : public Weapon {
public:
    using Weapon::Weapon;

protected:
    void shoot(const Aim& aim, Combat& combat, Rng& rng) override;
};

// Melee strike: short-range punch with alternating fists and no bullet tracer.
class MeleeWeapon final : public Weapon {
public:
    using Weapon::Weapon;
    int punchSide() const { return side_; }

protected:
    void shoot(const Aim& aim, Combat& combat, Rng& rng) override;

private:
    int side_ = 0;  // 0 = Left Jab, 1 = Right Hook
};

// Physics-driven projectile with a swept bounding-box test (rockets).
class ProjectileWeapon final : public Weapon {
public:
    using Weapon::Weapon;

protected:
    void shoot(const Aim& aim, Combat& combat, Rng& rng) override;
};

std::unique_ptr<Weapon> makeFists();
std::unique_ptr<Weapon> makePistol();
std::unique_ptr<Weapon> makeSmg();
std::unique_ptr<Weapon> makeShotgun();
std::unique_ptr<Weapon> makeRocketLauncher();

// 0 Fists, 1 Pistol, 2 SMG, 3 Shotgun, 4 Rocket launcher.
std::unique_ptr<Weapon> makeWeapon(std::size_t index);

} // namespace carnage
