#include "game/Weapon.hpp"

#include <algorithm>
#include <cmath>

namespace carnage {

WeaponState Weapon::state() const {
    if (reloading_)                            return WeaponState::Reloading;
    if (!stats_.infiniteAmmo && ammo_ == 0)    return WeaponState::Empty;
    if (timeToNext_ > 0)                       return WeaponState::Cooldown;
    return WeaponState::Ready;
}

void Weapon::equip() {
    reloading_  = false;
    reloadLeft_ = 0.0f;
    timeToNext_ = 0.2f;  // draw time
    buffered_   = 0.0f;
    kick_ = prevKick_ = 0.0f;
    flash_ = 0.0f;
    recoil_ = Recoil{};
}

void Weapon::startReload(WeaponEvents& ev) {
    if (stats_.infiniteAmmo) return;
    reloading_  = true;
    reloadLeft_ = stats_.reloadTime;
    ev.reloadStarted = true;
}

WeaponEvents Weapon::tick(float dt, const TriggerInput& in, const Aim& aim, Combat& combat,
                          Rng& rng) {
    WeaponEvents ev;

    recoil_.update(dt, stats_.recoilDelay, stats_.recoilRate, stats_.recoilLinear);
    prevKick_ = kick_;
    kick_    *= std::exp(-16.0f * dt);
    flash_    = std::max(0.0f, flash_ - dt);

    // Keep at most one tick of credit so the average rate is exact but an
    // idle weapon can't bank a burst.
    timeToNext_ = std::max(timeToNext_ - dt, -dt);
    buffered_   = in.pressed ? kTriggerBuffer : std::max(0.0f, buffered_ - dt);

    if (reloading_) {
        reloadLeft_ -= dt;
        if (reloadLeft_ <= 0.0f) {
            reloading_  = false;
            reloadLeft_ = 0.0f;
            ammo_       = stats_.magSize;
            ev.reloadFinished = true;
        }
        return ev;
    }

    if (in.reload && !stats_.infiniteAmmo && ammo_ < stats_.magSize) {
        startReload(ev);
        return ev;
    }

    const bool wantFire = stats_.automatic ? in.held : buffered_ > 0.0f;
    if (!wantFire || timeToNext_ > 0.0f) return ev;

    if (!stats_.infiniteAmmo && ammo_ == 0) {
        ev.dryFired = true;
        buffered_   = 0.0f;
        startReload(ev);
        return ev;
    }

    if (!stats_.infiniteAmmo) --ammo_;
    shoot(aim, combat, rng);

    float pitchKick = stats_.recoilPitch * rng.range(0.85f, 1.15f);
    float yawKick   = stats_.recoilYaw * rng.signedUnit();
    if (rng.uniform() < stats_.recoilSpikeChance) {  // the occasional wild one
        pitchKick *= 1.7f;
        yawKick   *= 2.2f;
    }
    recoil_.kick(pitchKick, yawKick, stats_.recoilMax);
    kick_       += 1.0f;
    flash_       = stats_.flashSize > 0.0f ? kFlashTime : 0.0f;
    buffered_    = 0.0f;
    timeToNext_ += 60.0f / stats_.rpm;
    ev.fired     = true;

    if (!stats_.infiniteAmmo && ammo_ == 0) startReload(ev);  // auto-reload on empty
    return ev;
}

} // namespace carnage
