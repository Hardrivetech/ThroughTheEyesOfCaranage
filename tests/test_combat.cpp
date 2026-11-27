#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/Random.hpp"
#include "game/Combat.hpp"
#include "game/Movement.hpp"
#include "game/PlayerHealth.hpp"
#include "game/Weapons.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

namespace {

// Dummy standing on the floor at z = -10: the front face is at z = -9.75,
// the body spans y 0..1.8 and the head zone starts at y = headBottom() (~1.4).
constexpr float kFrontFace = -9.75f;

Aim aimForward(glm::vec3 origin = {0.0f, 1.0f, 0.0f}) {
    Aim a;
    a.origin  = origin;
    a.forward = {0.0f, 0.0f, -1.0f};
    a.right   = {1.0f, 0.0f, 0.0f};
    a.up      = {0.0f, 1.0f, 0.0f};
    a.muzzle  = origin + glm::vec3(0.15f, -0.1f, -0.4f);
    return a;
}

TriggerInput press() {
    TriggerInput t;
    t.pressed = true;
    t.held    = true;
    return t;
}

std::size_t count(const Combat& c, HitKind k) {
    return static_cast<std::size_t>(std::count_if(c.events().begin(), c.events().end(),
                                                  [&](const HitEvent& e) { return e.kind == k; }));
}

} // namespace

TEST_CASE("rayAabb hits, misses, clamps and handles inside/parallel rays", "[combat][raycast]") {
    const glm::vec3 bmin{-1.0f, 0.0f, -11.0f}, bmax{1.0f, 2.0f, -9.0f};
    float     t = 0.0f;
    glm::vec3 n{0.0f};

    REQUIRE(rayAabb({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, bmin, bmax, 100.0f, t, n));
    CHECK(t == Approx(9.0f));
    CHECK(near(n, {0.0f, 0.0f, 1.0f}));

    REQUIRE(rayAabb({5.0f, 1.0f, -10.0f}, {-1.0f, 0.0f, 0.0f}, bmin, bmax, 100.0f, t, n));
    CHECK(t == Approx(4.0f));
    CHECK(near(n, {1.0f, 0.0f, 0.0f}));

    CHECK_FALSE(rayAabb({3.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, bmin, bmax, 100.0f, t, n));  // offset
    CHECK_FALSE(rayAabb({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, bmin, bmax, 5.0f, t, n));    // too short
    CHECK_FALSE(rayAabb({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, bmin, bmax, 100.0f, t, n));   // wrong way

    REQUIRE(rayAabb({0.0f, 1.0f, -10.0f}, {0.0f, 0.0f, -1.0f}, bmin, bmax, 100.0f, t, n));    // starts inside
    CHECK(t == 0.0f);
}

TEST_CASE("Hitscan damage: body, headshot multiplier, kills and occlusion", "[combat][hitscan]") {
    World w = floorWorld();
    Combat combat(w);
    const std::size_t id = combat.addTarget(Target::dummy({0.0f, 0.0f, -10.0f}));
    const Target& t = combat.targets()[id];
    const glm::vec3 fwd{0.0f, 0.0f, -1.0f};

    SECTION("a body shot deals base damage and reports the hit") {
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        const HitEvent& e = combat.events()[0];
        CHECK(e.kind == HitKind::Body);
        CHECK(e.damage == Approx(30.0f));
        CHECK(e.target == static_cast<int>(id));
        CHECK(e.tracer);
        CHECK_FALSE(e.melee);
        CHECK_FALSE(e.killed);
        CHECK(e.pos.z == Approx(kFrontFace));
        CHECK(near(e.normal, {0.0f, 0.0f, 1.0f}));
        CHECK(t.hp == Approx(70.0f));
    }
    SECTION("a headshot deals double damage") {
        combat.hitscan({0.0f, 1.6f, 0.0f}, fwd, {0.0f, 1.5f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Head);
        CHECK(combat.events()[0].damage == Approx(60.0f));
        CHECK(t.hp == Approx(40.0f));
    }
    SECTION("lethal damage kills once, clamps hp, and dead targets are ignored") {
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 60.0f);  // body: 60
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 60.0f);  // body: 60 more
        REQUIRE(combat.events().size() == 2);
        CHECK_FALSE(combat.events()[0].killed);
        CHECK(combat.events()[1].killed);
        CHECK(t.hp == 0.0f);
        CHECK_FALSE(t.alive());

        combat.clearEvents();
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Miss);  // the corpse is not a hit box
        CHECK(t.hp == 0.0f);
    }
    SECTION("range limits the shot") {
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 5.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Miss);
        CHECK(t.hp == Approx(100.0f));
    }
    SECTION("a wall in front of the target takes the bullet") {
        w.add(solid(-5.0f, -5.0f, 5.0f, -4.0f, 3.0f));
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::World);
        CHECK(combat.events()[0].pos.z == Approx(-4.0f));
        CHECK(t.hp == Approx(100.0f));
    }
    SECTION("a target in front of the wall still gets hit") {
        w.add(solid(-5.0f, -20.0f, 5.0f, -19.0f, 3.0f));
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Body);
    }
    SECTION("the nearer of two targets absorbs the shot") {
        const std::size_t front = combat.addTarget(Target::dummy({0.0f, 0.0f, -4.0f}));
        combat.hitscan({0.0f, 1.0f, 0.0f}, fwd, {0.0f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].target == static_cast<int>(front));
        CHECK(combat.targets()[front].hp == Approx(70.0f));
        CHECK(t.hp == Approx(100.0f));
    }
    SECTION("a shot that passes beside the target misses") {
        combat.hitscan({1.5f, 1.0f, 0.0f}, fwd, {1.5f, 0.9f, -0.4f}, 100.0f, 30.0f);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Miss);
    }
}

TEST_CASE("Melee strikes: sphere overlap, head bonus, exclusion", "[combat][melee]") {
    World w = floorWorld();
    Combat combat(w);
    combat.addTarget(Target::dummy({0.0f, 0.0f, -2.0f}));  // front face at z = -1.75
    const glm::vec3 dir{0.0f, 0.0f, -1.0f};

    SECTION("reach is the sphere radius to the nearest point of the body") {
        CHECK(combat.strike({0.0f, 0.9f, 0.0f}, 1.5f, 40.0f, dir) == 0);  // 1.75 away
        CHECK(combat.events().empty());
        CHECK(combat.strike({0.0f, 0.9f, 0.0f}, 1.8f, 40.0f, dir) == 1);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].melee);
        CHECK(combat.events()[0].kind == HitKind::Body);
        CHECK(combat.events()[0].damage == Approx(40.0f));
        CHECK_FALSE(combat.events()[0].tracer);
    }
    SECTION("head hits do 1.5x") {
        CHECK(combat.strike({0.0f, 1.6f, 0.0f}, 2.0f, 40.0f, dir) == 1);
        CHECK(combat.events()[0].kind == HitKind::Head);
        CHECK(combat.events()[0].damage == Approx(60.0f));
    }
    SECTION("excluded and held targets are skipped") {
        CHECK(combat.strike({0.0f, 0.9f, 0.0f}, 3.0f, 40.0f, dir, 0) == 0);
        combat.targetAt(0)->held = true;
        CHECK(combat.strike({0.0f, 0.9f, 0.0f}, 3.0f, 40.0f, dir) == 0);
    }
    SECTION("one strike can hit several targets") {
        combat.addTarget(Target::dummy({1.0f, 0.0f, -2.0f}));
        CHECK(combat.strike({0.5f, 0.9f, -1.0f}, 2.0f, 10.0f, dir) == 2);
    }
    SECTION("direct damage reports kills and ignores the dead") {
        const glm::vec3 p{0.0f, 0.9f, -1.75f}, n{0.0f, 0.0f, 1.0f};
        CHECK_FALSE(combat.damageTarget(0, 60.0f, p, n, {0.0f, 0.0f, 0.0f}));
        CHECK(combat.damageTarget(0, 60.0f, p, n, {0.0f, 0.0f, 0.0f}));
        CHECK_FALSE(combat.damageTarget(0, 60.0f, p, n, {0.0f, 0.0f, 0.0f}));  // already dead
        CHECK_FALSE(combat.damageTarget(99, 60.0f, p, n, {0.0f, 0.0f, 0.0f})); // out of range
    }
}

TEST_CASE("Explosion damage falls off linearly with distance", "[combat][explosion]") {
    World w = floorWorld();
    Combat combat(w);
    combat.addTarget(Target::dummy({0.0f, 0.0f, -10.0f}));
    combat.targetAt(0)->hp = combat.targetAt(0)->maxHp = 1000.0f;  // survive every case
    const float radius = 5.0f, damage = 120.0f, knockback = 18.0f;

    const auto blastAt = [&](float distFromFace) {
        combat.clearEvents();
        combat.targetAt(0)->hp = 1000.0f;
        combat.detonate({0.0f, 0.9f, kFrontFace + distFromFace}, radius, damage, knockback);
        return combat.targetAt(0)->maxHp - combat.targetAt(0)->hp;
    };

    SECTION("damage is damage * (1 - d / radius)") {
        CHECK(blastAt(3.0f) == Approx(damage * (1.0f - 3.0f / radius)).margin(0.01f));
        CHECK(blastAt(1.0f) == Approx(damage * (1.0f - 1.0f / radius)).margin(0.01f));
        CHECK(blastAt(0.0f) == Approx(damage).margin(0.01f));
    }
    SECTION("damage strictly decreases with distance and is zero beyond the radius") {
        float last = blastAt(0.0f);
        for (float d = 0.5f; d < radius; d += 0.5f) {
            const float now = blastAt(d);
            CHECK(now < last);
            CHECK(now > 0.0f);
            last = now;
        }
        CHECK(blastAt(radius) == 0.0f);
        CHECK(blastAt(radius + 3.0f) == 0.0f);
    }
    SECTION("every blast emits one Explosion event with its parameters") {
        blastAt(2.0f);
        REQUIRE(count(combat, HitKind::Explosion) == 1);
        REQUIRE(count(combat, HitKind::Body) == 1);
        const HitEvent& e = combat.events().back();
        CHECK(e.kind == HitKind::Explosion);
        CHECK(e.radius == Approx(radius));
        CHECK(e.knockback == Approx(knockback));
        CHECK(e.damage == Approx(damage));

        combat.clearEvents();
        blastAt(radius + 1.0f);  // out of range of the target: still a blast, no body hit
        CHECK(count(combat, HitKind::Explosion) == 1);
        CHECK(count(combat, HitKind::Body) == 0);
    }
    SECTION("blasts can kill and ignore corpses") {
        combat.targetAt(0)->hp = 50.0f;
        combat.clearEvents();
        combat.detonate({0.0f, 0.9f, kFrontFace + 1.0f}, radius, damage, knockback);  // 96 damage
        CHECK_FALSE(combat.targetAt(0)->alive());
        CHECK(std::any_of(combat.events().begin(), combat.events().end(),
                          [](const HitEvent& e) { return e.kind == HitKind::Body && e.killed; }));

        combat.clearEvents();
        combat.detonate({0.0f, 0.9f, kFrontFace + 1.0f}, radius, damage, knockback);
        CHECK(count(combat, HitKind::Body) == 0);  // a corpse takes no further damage
    }
}

TEST_CASE("Blast impulse scales with distance and lifts the player", "[combat][explosion]") {
    HitEvent e;
    e.kind      = HitKind::Explosion;
    e.pos       = {0.0f, 0.0f, 0.0f};
    e.radius    = 5.0f;
    e.knockback = 18.0f;

    const glm::vec3 near2 = blastImpulse(e, {0.0f, 0.0f, -2.5f});
    CHECK(glm::length(near2) == Approx(9.0f));     // half the radius, half the knockback
    CHECK(near2.z < 0.0f);                         // pushed away
    CHECK(near2.y > 0.0f);                         // with an upward bias

    CHECK(near(blastImpulse(e, {0.0f, 0.0f, 0.0f}), {0.0f, 18.0f, 0.0f}));  // dead centre: straight up
    CHECK(glm::length(blastImpulse(e, {0.0f, 0.0f, -1.0f})) >
          glm::length(blastImpulse(e, {0.0f, 0.0f, -4.0f})));
    CHECK(near(blastImpulse(e, {0.0f, 0.0f, -5.0f}), {0.0f, 0.0f, 0.0f}));  // at the edge
    CHECK(near(blastImpulse(e, {20.0f, 0.0f, 0.0f}), {0.0f, 0.0f, 0.0f}));

    HitEvent notBlast = e;
    notBlast.kind = HitKind::Body;
    CHECK(near(blastImpulse(notBlast, {0.0f, 0.0f, -1.0f}), {0.0f, 0.0f, 0.0f}));

    SECTION("a rocket jump launches a grounded player") {
        World w = floorWorld();
        MovementController mc(w);
        mc.respawn({0.0f, 0.0f, 0.0f});
        e.pos = {0.0f, -0.5f, 0.0f};  // blast under the feet
        mc.impulse(blastImpulse(e, mc.position() + glm::vec3(0.0f, 0.9f, 0.0f)));
        CHECK_FALSE(mc.grounded());
        CHECK(mc.velocity().y > 5.0f);
    }
}

TEST_CASE("Rockets fly, detonate on contact and damage in a radius", "[combat][rocket]") {
    World w = floorWorld();
    Combat combat(w);
    combat.addTarget(Target::dummy({0.0f, 0.0f, -20.0f}));
    combat.targetAt(0)->hp = combat.targetAt(0)->maxHp = 1000.0f;
    combat.addTarget(Target::dummy({3.0f, 0.0f, -20.0f}));  // 3 m to the side: inside the blast
    combat.targetAt(1)->hp = combat.targetAt(1)->maxHp = 1000.0f;
    combat.addTarget(Target::dummy({12.0f, 0.0f, -20.0f}));  // well outside it
    combat.targetAt(2)->hp = combat.targetAt(2)->maxHp = 1000.0f;

    auto launcher = makeRocketLauncher();
    Rng rng{7};
    const WeaponEvents fired = launcher->tick(kDt, press(), aimForward(), combat, rng);
    REQUIRE(fired.fired);
    REQUIRE(combat.projectiles().size() == 1);
    CHECK(launcher->ammo() == 0);

    int ticks = 0;
    while (!combat.projectiles().empty() && ticks < 300) { combat.update(kDt); ++ticks; }
    CHECK(combat.projectiles().empty());
    CHECK(ticks > 5);  // it travelled, rather than detonating at the muzzle

    REQUIRE(count(combat, HitKind::Explosion) == 1);
    const WeaponStats& s = launcher->stats();
    const auto explosion = std::find_if(combat.events().begin(), combat.events().end(),
                                        [](const HitEvent& e) { return e.kind == HitKind::Explosion; });
    CHECK(explosion->radius == Approx(s.blastRadius));
    CHECK(explosion->knockback == Approx(s.knockback));

    const float direct = combat.targets()[0].maxHp - combat.targets()[0].hp;
    const float side   = combat.targets()[1].maxHp - combat.targets()[1].hp;
    CHECK(direct > 0.0f);
    CHECK(direct <= s.blastDamage);
    CHECK(side > 0.0f);
    CHECK(side < direct);                                    // falloff
    CHECK(combat.targets()[2].hp == combat.targets()[2].maxHp);  // outside the radius
}

TEST_CASE("Rockets detonate on world geometry and by lifetime", "[combat][rocket]") {
    World w = floorWorld();
    w.add(solid(-10.0f, -15.0f, 10.0f, -14.0f, 5.0f));  // wall in front of the launcher
    Combat combat(w);
    combat.addTarget(Target::dummy({0.0f, 0.0f, -17.0f}));  // hiding behind it, within blast range
    combat.targetAt(0)->hp = combat.targetAt(0)->maxHp = 1000.0f;

    auto launcher = makeRocketLauncher();
    Rng rng{7};
    REQUIRE(launcher->tick(kDt, press(), aimForward(), combat, rng).fired);
    for (int i = 0; i < 300 && !combat.projectiles().empty(); ++i) combat.update(kDt);
    REQUIRE(count(combat, HitKind::Explosion) == 1);
    const auto e = std::find_if(combat.events().begin(), combat.events().end(),
                                [](const HitEvent& ev) { return ev.kind == HitKind::Explosion; });
    CHECK(e->pos.z > -14.5f);   // the blast is on the near side of the wall
    CHECK(e->normal.z > 0.5f);  // facing back toward the shooter

    SECTION("a projectile that runs out of lifetime still explodes") {
        World open = floorWorld();
        Combat c2(open);
        Projectile p;
        p.pos = {0.0f, 50.0f, 0.0f};
        p.vel = {0.0f, 0.0f, -1.0f};
        p.lifetime    = 0.5f;
        p.blastRadius = 4.0f;
        p.blastDamage = 50.0f;
        c2.spawn(p, p.pos);
        for (int i = 0; i < 60; ++i) c2.update(kDt);
        CHECK(c2.projectiles().empty());
        CHECK(count(c2, HitKind::Explosion) == 1);
    }
    SECTION("a muzzle poking through a wall detonates on that wall") {
        World wall = floorWorld();
        wall.add(solid(-5.0f, -2.0f, 5.0f, -1.0f, 5.0f));
        Combat c3(wall);
        Projectile p;
        p.pos = {0.0f, 1.0f, -3.0f};          // muzzle is already past the wall
        p.vel = {0.0f, 0.0f, -30.0f};
        p.blastRadius = 3.0f;
        p.blastDamage = 50.0f;
        c3.spawn(p, {0.0f, 1.0f, 0.0f});      // fired from the eye, in front of the wall
        CHECK(c3.projectiles().empty());
        CHECK(count(c3, HitKind::Explosion) == 1);
    }
}

TEST_CASE("Weapon fire rate, ammo, reload and trigger behaviour", "[weapon]") {
    World w = floorWorld();
    Combat combat(w);
    Rng rng{99};
    const Aim aim = aimForward();

    SECTION("a semi-auto pistol fires once per press, not while held") {
        auto pistol = makePistol();
        TriggerInput held;
        held.held = true;
        for (int i = 0; i < 60; ++i) CHECK_FALSE(pistol->tick(kDt, held, aim, combat, rng).fired);

        const WeaponEvents first = pistol->tick(kDt, press(), aim, combat, rng);
        CHECK(first.fired);
        CHECK(pistol->ammo() == pistol->magSize() - 1);
        CHECK(pistol->state() == WeaponState::Cooldown);
        for (int i = 0; i < 60; ++i) CHECK_FALSE(pistol->tick(kDt, held, aim, combat, rng).fired);
        CHECK(pistol->ammo() == pistol->magSize() - 1);
    }
    SECTION("an automatic weapon fires at its rpm") {
        auto smg = makeSmg();
        TriggerInput held;
        held.held = true;
        int shots = 0;
        for (int i = 0; i < 60; ++i) shots += smg->tick(kDt, held, aim, combat, rng).fired ? 1 : 0;
        const float expected = smg->stats().rpm / 60.0f;  // shots in one second
        CHECK(static_cast<float>(shots) == Approx(expected).margin(1.5f));
        CHECK(smg->ammo() == smg->magSize() - shots);
    }
    SECTION("a press shortly before the weapon is ready is buffered") {
        auto pistol = makePistol();
        REQUIRE(pistol->tick(kDt, press(), aim, combat, rng).fired);
        int shots = 1;
        for (int i = 1; i < 14; ++i) {
            const TriggerInput in = (i == 3) ? press() : TriggerInput{};
            shots += pistol->tick(kDt, in, aim, combat, rng).fired ? 1 : 0;
        }
        CHECK(shots == 2);  // the early press fired as soon as the cooldown ended
    }
    SECTION("emptying the magazine auto-reloads, then refills") {
        auto pistol = makePistol();
        int shots = 0;
        bool reloadStarted = false;
        for (int i = 0; i < 600 && shots < pistol->magSize(); ++i) {
            const WeaponEvents ev = pistol->tick(kDt, (i % 12 == 0) ? press() : TriggerInput{}, aim, combat, rng);
            shots += ev.fired ? 1 : 0;
            reloadStarted = reloadStarted || ev.reloadStarted;
        }
        CHECK(shots == pistol->magSize());
        CHECK(pistol->ammo() == 0);
        CHECK(reloadStarted);
        CHECK(pistol->reloading());
        CHECK(pistol->state() == WeaponState::Reloading);
        CHECK_FALSE(pistol->tick(kDt, press(), aim, combat, rng).fired);  // can't fire mid-reload

        bool finished = false;
        const int reloadTicks = static_cast<int>(pistol->stats().reloadTime / kDt) + 5;
        for (int i = 0; i < reloadTicks && !finished; ++i)
            finished = pistol->tick(kDt, TriggerInput{}, aim, combat, rng).reloadFinished;
        CHECK(finished);
        CHECK(pistol->ammo() == pistol->magSize());
        CHECK_FALSE(pistol->reloading());
    }
    SECTION("manual reload needs a missing round; dry fire starts a reload") {
        auto pistol = makePistol();
        TriggerInput reload;
        reload.reload = true;
        CHECK_FALSE(pistol->tick(kDt, reload, aim, combat, rng).reloadStarted);  // full magazine

        REQUIRE(pistol->tick(kDt, press(), aim, combat, rng).fired);
        CHECK(pistol->tick(kDt, reload, aim, combat, rng).reloadStarted);
        CHECK(pistol->reloading());

        pistol->refill();  // dev command: instant full mag
        CHECK(pistol->ammo() == pistol->magSize());
        CHECK_FALSE(pistol->reloading());
    }
    SECTION("fists never run dry or reload") {
        auto fists = makeFists();
        TriggerInput reload;
        reload.reload = true;
        for (int i = 0; i < 300; ++i) {
            const WeaponEvents ev = fists->tick(kDt, (i % 20 == 0) ? press() : reload, aim, combat, rng);
            CHECK_FALSE(ev.reloadStarted);
            CHECK_FALSE(ev.dryFired);
        }
        CHECK(fists->ammo() == fists->magSize());
        CHECK_FALSE(fists->reloading());
    }
    SECTION("recoil kicks on every shot and fully settles afterwards") {
        auto pistol = makePistol();
        CHECK(pistol->recoil().current().x == 0.0f);
        REQUIRE(pistol->tick(kDt, press(), aim, combat, rng).fired);
        CHECK(pistol->recoil().current().x > 0.0f);
        for (int i = 0; i < 300; ++i) pistol->tick(kDt, TriggerInput{}, aim, combat, rng);
        CHECK(pistol->recoil().current().x == Approx(0.0f).margin(1e-6f));
        CHECK(pistol->recoil().current().y == Approx(0.0f).margin(1e-6f));
    }
}

TEST_CASE("Weapons resolve their shots through Combat", "[weapon][combat]") {
    World w = floorWorld();
    Combat combat(w);
    Rng rng{1234};
    const auto addDummy = [&](glm::vec3 feet) {
        const std::size_t i = combat.addTarget(Target::dummy(feet));
        combat.targetAt(i)->hp = combat.targetAt(i)->maxHp = 1000.0f;
        return i;
    };

    SECTION("a pistol shot deals the weapon's damage and shows a tracer") {
        const std::size_t t = addDummy({0.0f, 0.0f, -10.0f});
        auto pistol = makePistol();
        REQUIRE(pistol->tick(kDt, press(), aimForward({0.0f, 1.0f, 0.0f}), combat, rng).fired);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].kind == HitKind::Body);
        CHECK(combat.events()[0].tracer);
        CHECK(combat.targets()[t].maxHp - combat.targets()[t].hp == Approx(pistol->stats().damage));
    }
    SECTION("a shotgun fires every pellet in one trigger pull") {
        const std::size_t t = addDummy({0.0f, 0.0f, -5.0f});
        auto shotgun = makeShotgun();
        REQUIRE(shotgun->tick(kDt, press(), aimForward({0.0f, 0.9f, 0.0f}), combat, rng).fired);
        CHECK(static_cast<int>(combat.events().size()) == shotgun->stats().pellets);
        CHECK(shotgun->ammo() == shotgun->magSize() - 1);  // one shell, many pellets
        const float dealt = combat.targets()[t].maxHp - combat.targets()[t].hp;
        CHECK(dealt == Approx(shotgun->stats().damage * static_cast<float>(shotgun->stats().pellets)));
    }
    SECTION("shotgun pellets spread but stay inside the cone") {
        w.add(solid(-50.0f, -21.0f, 50.0f, -20.0f, 10.0f));  // backstop 20 m ahead
        auto shotgun = makeShotgun();
        const float tanSpread = std::tan(glm::radians(shotgun->stats().spreadDeg));
        REQUIRE(shotgun->tick(kDt, press(), aimForward({0.0f, 1.0f, 0.0f}), combat, rng).fired);
        REQUIRE(static_cast<int>(combat.events().size()) == shotgun->stats().pellets);
        float widest = 0.0f;
        for (const HitEvent& e : combat.events()) {
            CHECK(e.kind == HitKind::World);
            const float run = std::abs(e.pos.z);
            const float off = glm::length(glm::vec2(e.pos.x, e.pos.y - 1.0f));
            CHECK(off <= run * tanSpread * 1.001f + 1e-3f);
            widest = std::max(widest, off);
        }
        CHECK(widest > 0.05f);  // and they really do spread out
    }
    SECTION("nothing beyond the shotgun's range is hit") {
        auto shotgun = makeShotgun();
        const std::size_t far = addDummy({0.0f, 0.0f, -(shotgun->stats().range + 20.0f)});
        shotgun->tick(kDt, press(), aimForward({0.0f, 0.9f, 0.0f}), combat, rng);
        CHECK(combat.targets()[far].hp == combat.targets()[far].maxHp);
    }
    SECTION("fists punch at close range only and flag the hit as melee") {
        const std::size_t near1 = addDummy({0.0f, 0.0f, -2.0f});
        auto fists = makeFists();
        REQUIRE(fists->tick(kDt, press(), aimForward({0.0f, 1.0f, 0.0f}), combat, rng).fired);
        REQUIRE(combat.events().size() == 1);
        CHECK(combat.events()[0].melee);
        CHECK_FALSE(combat.events()[0].tracer);
        CHECK(combat.targets()[near1].maxHp - combat.targets()[near1].hp >= fists->stats().damage - 1e-3f);

        combat.clearEvents();
        const std::size_t far1 = addDummy({0.0f, 0.0f, -6.0f});  // beyond arm's reach
        auto fists2 = makeFists();
        combat.targetAt(near1)->held = true;                     // take the near one out of play
        fists2->tick(kDt, press(), aimForward({0.0f, 1.0f, 0.0f}), combat, rng);
        CHECK(combat.targets()[far1].hp == combat.targets()[far1].maxHp);
    }
    SECTION("makeWeapon maps slots to weapons") {
        CHECK(std::string(makeWeapon(0)->stats().name) == "Fists");
        CHECK(std::string(makeWeapon(1)->stats().name) == "Pistol");
        CHECK(std::string(makeWeapon(2)->stats().name) == "SMG");
        CHECK(std::string(makeWeapon(3)->stats().name) == "Shotgun");
        CHECK(std::string(makeWeapon(4)->stats().name) == "Rocket Launcher");
    }
}

TEST_CASE("Health, armor and i-frames under mixed bullet and blast damage", "[health][combat]") {
    PlayerHealth h;  // 100 health, 50 armor
    constexpr float kd = 1.0f / 60.0f;

    SECTION("armor soaks half of each hit until it runs out") {
        REQUIRE(h.damage(40.0f));                  // armor 50 -> 30, health 100 -> 80
        CHECK(h.armor == Approx(30.0f));
        CHECK(h.health == Approx(80.0f));
        h.update(PlayerHealth::kIFrameTime + kd);
        REQUIRE(h.damage(100.0f));                 // armor absorbs 30 (not 50): the rest hurts
        CHECK(h.armor == Approx(0.0f));
        CHECK(h.health == Approx(80.0f - 70.0f));
        h.update(PlayerHealth::kIFrameTime + kd);
        REQUIRE(h.damage(5.0f));                   // no armor left: full damage
        CHECK(h.health == Approx(5.0f));
    }
    SECTION("a bullet storm is limited by i-frames, but a blast always lands") {
        REQUIRE(h.damage(8.0f));                   // first bullet
        int landed = 0;
        for (int i = 0; i < 10; ++i) {             // ~0.17 s of bullets: all inside the grace window
            h.update(kd);
            landed += h.damage(8.0f) ? 1 : 0;
        }
        CHECK(landed == 0);
        CHECK(h.damage(30.0f, true));              // the rocket ignores i-frames
        const float healthAfterBlast = h.health;
        h.update(PlayerHealth::kIFrameTime + kd);
        CHECK(h.damage(8.0f));                     // bullets work again afterwards
        CHECK(h.health < healthAfterBlast);
    }
    SECTION("lethal damage kills, then nothing else applies") {
        h.armor = 0.0f;
        REQUIRE(h.damage(150.0f));
        CHECK(h.dead);
        CHECK(h.health == 0.0f);
        CHECK_FALSE(h.damage(10.0f, true));
        h.heal(50.0f);
        CHECK(h.health == 0.0f);                   // the dead cannot be healed
        h.addArmor(50.0f);
        CHECK(h.armor == 0.0f);
    }
    SECTION("healing and armor pickups clamp to their maximums") {
        h.heal(500.0f);
        h.addArmor(500.0f);
        CHECK(h.health == PlayerHealth::kMaxHealth);
        CHECK(h.armor == PlayerHealth::kMaxArmor);
        CHECK_FALSE(h.damage(-5.0f));              // negative or zero damage is ignored
        CHECK_FALSE(h.damage(0.0f));
        CHECK(h.health == PlayerHealth::kMaxHealth);
    }
}
