#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/Spatial.hpp"
#include "dev/DevUi.hpp"
#include "game/Enemy.hpp"
#include "game/Lights.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

namespace {

// A flat world, its Combat and the Enemies, with helpers to place enemies and step time.
struct Sim {
    World   world = floorWorld();
    Combat  combat{world};
    Enemies enemies;

    explicit Sim(const ActivationConfig& a = {}) { enemies.setActivation(a); }
    Sim(const Sim&) = delete;
    Sim& operator=(const Sim&) = delete;

    std::size_t add(glm::vec3 feet, EnemyType type = EnemyType::Melee, int sector = -1) {
        enemies.add(combat, feet, type, sector);
        return enemies.enemies().size() - 1;
    }
    void tick(glm::vec3 player, int n = 1) {
        for (int i = 0; i < n; ++i) {
            combat.update(kDt);
            enemies.tick(kDt, combat, world, player, 1.7f);
            combat.clearEvents();
        }
    }
    const Enemy& at(std::size_t i) const { return enemies.enemies()[i]; }
    Target& target(std::size_t i) { return *combat.targetAt(at(i).target); }
    // A real kill (starts the respawn timer, unlike setting hp to 0 by hand).
    void kill(std::size_t i) {
        combat.damageTarget(at(i).target, 1.0e6f, at(i).pos + glm::vec3(0.0f, 1.0f, 0.0f), {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    }
};

ActivationConfig cfg(float wake, float sleep) {
    ActivationConfig a;
    a.wakeRadius  = wake;
    a.sleepRadius = sleep;
    return a;
}

constexpr glm::vec3 kFar{0.0f, 0.0f, 200.0f};  // a player position far from enemies near the origin

} // namespace

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

TEST_CASE("Activation settings are sanitized", "[wake][config]") {
    SECTION("the defaults are valid and leave a hysteresis gap") {
        const ActivationConfig d;
        CHECK(d.enabled());
        CHECK(sanitized(d).wakeRadius == d.wakeRadius);
        CHECK(sanitized(d).sleepRadius == d.sleepRadius);
        CHECK(d.sleepRadius >= d.wakeRadius + ActivationConfig::kMinGap);
        // Far beyond anything an enemy can sense, so sleeping never hides a real threat.
        EnemyConfig e;
        CHECK(d.wakeRadius > e.sightRange);
        CHECK(d.wakeRadius > e.rangedRange * 1.5f);   // the longest shot an enemy takes
    }
    SECTION("sleep radius is forced above wake radius") {
        CHECK(sanitized(cfg(50.0f, 50.0f)).sleepRadius == Approx(50.0f + ActivationConfig::kMinGap));
        CHECK(sanitized(cfg(50.0f, 10.0f)).sleepRadius == Approx(50.0f + ActivationConfig::kMinGap));
        CHECK(sanitized(cfg(50.0f, 80.0f)).sleepRadius == Approx(80.0f));   // already fine: untouched
    }
    SECTION("non-positive or NaN wake radius turns sleeping off") {
        for (const float w : {0.0f, -5.0f, std::numeric_limits<float>::quiet_NaN()}) {
            CHECK_FALSE(sanitized(cfg(w, 100.0f)).enabled());
        }
    }
    SECTION("negative timers and radii are clamped to zero") {
        ActivationConfig a = cfg(50.0f, 70.0f);
        a.keepAwake = -3.0f;
        a.groupRadius = -1.0f;
        a.shotAlertRadius = -1.0f;
        a.meleeAlertRadius = -1.0f;
        a.blastAlertRadius = -1.0f;
        const ActivationConfig s = sanitized(a);
        CHECK(s.keepAwake == 0.0f);
        CHECK(s.groupRadius == 0.0f);
        CHECK(s.shotAlertRadius == 0.0f);
        CHECK(s.meleeAlertRadius == 0.0f);
        CHECK(s.blastAlertRadius == 0.0f);
    }
    SECTION("Enemies applies the sanitizing") {
        Enemies e;
        e.setActivation(cfg(40.0f, 30.0f));
        CHECK(e.activation().sleepRadius > e.activation().wakeRadius);
    }
    SECTION("EnemyConfig is untouched") {
        const EnemyConfig e;   // activation is a separate struct: combat balance numbers are unchanged
        CHECK(e.meleeDamage == 12.0f);
        CHECK(e.rangedDamage == 8.0f);
        CHECK(e.chaseSpeed == 4.2f);
        CHECK(e.sightRange == 18.0f);
    }
}

// ---------------------------------------------------------------------------
// Sleeping
// ---------------------------------------------------------------------------

TEST_CASE("A far enemy falls asleep on the first tick, stands still and does no work", "[wake][sleep]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});   // 300 m from kFar... well beyond sleepRadius
    s.tick(kFar);
    CHECK(s.at(i).asleep);
    CHECK(s.at(i).state == AiState::Idle);
    CHECK(s.enemies.activity().asleep == 1);
    CHECK(s.enemies.activity().awake == 0);

    const glm::vec3 where = s.at(i).pos;
    s.world.resetQueryStats();
    s.tick(kFar, 600);                                   // ten seconds of nothing
    CHECK(s.at(i).asleep);
    CHECK(s.at(i).pos == where);                         // it did not patrol
    CHECK(s.enemies.attacks().empty());
    CHECK(s.enemies.footsteps().empty());
    CHECK(s.world.queryStats().queries == 0);            // no vision raycast, no movement, no ground or wall queries
}

TEST_CASE("The same enemy with sleeping off keeps thinking, which is what sleeping saves", "[wake][sleep]") {
    Sim awake(cfg(0.0f, 0.0f));
    const std::size_t i = awake.add({0.0f, 0.0f, -100.0f});
    awake.tick(kFar);
    awake.world.resetQueryStats();
    awake.tick(kFar, 600);
    CHECK_FALSE(awake.at(i).asleep);
    CHECK(awake.world.queryStats().queries > 600);       // at least one query per tick
    CHECK(awake.at(i).pos != glm::vec3(0.0f, 0.0f, -100.0f));  // it wandered off its post (patrol)
    CHECK(awake.enemies.activity().asleep == 0);
}

TEST_CASE("An enemy that has just spawned far from the player never takes a step first", "[wake][sleep]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    CHECK_FALSE(s.at(i).asleep);          // before its first tick it has made no decision
    s.tick(kFar);
    CHECK(s.at(i).asleep);
    CHECK(s.at(i).pos == glm::vec3(0.0f, 0.0f, -100.0f));
}

// ---------------------------------------------------------------------------
// Waking and hysteresis
// ---------------------------------------------------------------------------

TEST_CASE("An enemy wakes at the wake radius and sleeps only beyond the sleep radius", "[wake][hysteresis]") {
    Sim s(cfg(50.0f, 65.0f));
    const std::size_t i = s.add({0.0f, 0.0f, 0.0f});
    const auto playerAt = [](float dist) { return glm::vec3(dist, 0.0f, 0.0f); };

    s.tick(playerAt(200.0f));
    REQUIRE(s.at(i).asleep);

    s.tick(playerAt(50.5f));
    CHECK(s.at(i).asleep);                // just outside: still asleep
    s.tick(playerAt(50.0f));
    CHECK_FALSE(s.at(i).asleep);          // exactly on the radius counts as inside

    s.tick(playerAt(60.0f));
    CHECK_FALSE(s.at(i).asleep);          // between wake and sleep: stays awake
    s.tick(playerAt(65.0f));
    CHECK_FALSE(s.at(i).asleep);
    s.tick(playerAt(65.5f));
    CHECK(s.at(i).asleep);                // beyond the sleep radius

    s.tick(playerAt(60.0f));
    CHECK(s.at(i).asleep);                // coming back into the band does not wake it...
    s.tick(playerAt(50.5f));
    CHECK(s.at(i).asleep);
    s.tick(playerAt(49.0f));
    CHECK_FALSE(s.at(i).asleep);          // ...only crossing the wake radius does
}

TEST_CASE("An enemy on the boundary does not flip between asleep and awake", "[wake][hysteresis]") {
    const auto countFlips = [](float startDist, float nearDist, float farDist) {
        Sim s(cfg(50.0f, 65.0f));
        const std::size_t i = s.add({0.0f, 0.0f, 0.0f});
        s.tick({startDist, 0.0f, 0.0f});
        bool last = s.at(i).asleep;
        int  flips = 0;
        for (int t = 0; t < 600; ++t) {              // the player jitters across the boundary every tick
            s.tick({(t % 2) ? nearDist : farDist, 0.0f, 0.0f});
            if (s.at(i).asleep != last) { ++flips; last = s.at(i).asleep; }
        }
        return flips;
    };
    CHECK(countFlips(200.0f, 49.0f, 51.0f) == 1);    // wakes once, then stays awake
    CHECK(countFlips(10.0f, 64.0f, 66.0f) == 1);     // sleeps once, then stays asleep
    CHECK(countFlips(200.0f, 49.9f, 50.1f) == 1);
}

TEST_CASE("Distance is measured in 3D", "[wake][hysteresis]") {
    Sim s(cfg(50.0f, 65.0f));
    const std::size_t i = s.add({0.0f, -6.0f, 0.0f});      // down in the pit
    s.tick({0.0f, 40.0f, 0.0f});                           // 46 m away vertically: inside
    CHECK_FALSE(s.at(i).asleep);
    s.tick({0.0f, 70.0f, 0.0f});                           // 76 m: beyond sleep
    CHECK(s.at(i).asleep);
}

TEST_CASE("A woken enemy reacts like a fresh one: it notices and chases the player", "[wake][behaviour]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -10.0f});
    s.tick(kFar);
    REQUIRE(s.at(i).asleep);

    s.tick({0.0f, 0.0f, 0.0f}, 60);                        // the player teleports into view
    CHECK_FALSE(s.at(i).asleep);
    CHECK((s.at(i).state == AiState::Chase || s.at(i).state == AiState::Attack));
    CHECK(s.at(i).pos.z > -10.0f);                         // and it is coming
}

// ---------------------------------------------------------------------------
// Forced wake: damage, alerts, groups
// ---------------------------------------------------------------------------

TEST_CASE("Damage wakes a distant enemy and it stays awake for the keep-awake time", "[wake][damage]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    REQUIRE(s.at(i).asleep);

    s.combat.damageTarget(s.at(i).target, 10.0f, {0.0f, 1.0f, -100.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    s.tick(kFar);
    CHECK_FALSE(s.at(i).asleep);                           // woken by being hurt, though the player is 300 m away
    CHECK(s.target(i).hp == Approx(90.0f));

    s.tick(kFar, 60 * 9);                                  // 9 s later: still awake
    CHECK_FALSE(s.at(i).asleep);
    s.tick(kFar, 60 * 2);                                  // past the 10 s: it may sleep again
    CHECK(s.at(i).asleep);
}

TEST_CASE("Being hit again while awake and far restarts the keep-awake time", "[wake][damage]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    for (int hit = 0; hit < 3; ++hit) {
        s.combat.damageTarget(s.at(i).target, 5.0f, {0.0f, 1.0f, -100.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
        s.tick(kFar, 60 * 8);
        CHECK_FALSE(s.at(i).asleep);                       // each hit buys another stretch
    }
    s.tick(kFar, 60 * 12);
    CHECK(s.at(i).asleep);
}

TEST_CASE("A shot from a real weapon wakes the enemy it hits", "[wake][damage]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -80.0f});
    s.tick({0.0f, 0.0f, 0.0f});                            // 80 m: asleep
    REQUIRE(s.at(i).asleep);
    s.combat.hitscan({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, -0.4f}, 250.0f, 30.0f);
    REQUIRE(s.target(i).hp < s.target(i).maxHp);
    s.tick({0.0f, 0.0f, 0.0f});
    CHECK_FALSE(s.at(i).asleep);
}

TEST_CASE("A hurt enemy rouses its neighbours but not distant groups", "[wake][group]") {
    Sim s;
    const std::size_t a = s.add({0.0f, 0.0f, -100.0f});
    const std::size_t b = s.add({8.0f, 0.0f, -100.0f});     // 8 m from a: in its group
    const std::size_t c = s.add({40.0f, 0.0f, -100.0f});    // 40 m: a different group
    s.tick(kFar);
    REQUIRE((s.at(a).asleep && s.at(b).asleep && s.at(c).asleep));

    s.combat.damageTarget(s.at(a).target, 10.0f, {0.0f, 1.0f, -100.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f});
    s.tick(kFar, 2);
    CHECK_FALSE(s.at(a).asleep);
    CHECK_FALSE(s.at(b).asleep);
    CHECK(s.at(c).asleep);
    CHECK(s.enemies.activity().awake == 2);
    CHECK(s.enemies.activity().asleep == 1);
}

TEST_CASE("alert() wakes enemies within the radius and keeps them awake", "[wake][alert]") {
    Sim s;
    const std::size_t near1 = s.add({0.0f, 0.0f, -100.0f});
    const std::size_t near2 = s.add({20.0f, 0.0f, -100.0f});
    const std::size_t far1  = s.add({60.0f, 0.0f, -100.0f});
    s.tick(kFar);
    REQUIRE((s.at(near1).asleep && s.at(near2).asleep && s.at(far1).asleep));

    s.enemies.alert({10.0f, 0.0f, -100.0f}, 15.0f);        // both near ones are within 15 m of this point
    CHECK_FALSE(s.at(near1).asleep);
    CHECK_FALSE(s.at(near2).asleep);
    CHECK(s.at(far1).asleep);

    s.tick(kFar, 60 * 8);                                  // the player is far, but they were alerted
    CHECK_FALSE(s.at(near1).asleep);
    CHECK_FALSE(s.at(near2).asleep);
    CHECK(s.at(far1).asleep);
    s.tick(kFar, 60 * 5);
    CHECK(s.at(near1).asleep);                             // calm again after the keep-awake time
    CHECK(s.at(near2).asleep);
}

TEST_CASE("alert() ignores bad radii and does nothing when sleeping is off", "[wake][alert]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    for (const float r : {0.0f, -5.0f, std::numeric_limits<float>::quiet_NaN()}) {
        s.enemies.alert({0.0f, 0.0f, -100.0f}, r);
        CHECK(s.at(i).asleep);
    }
    s.enemies.alert({0.0f, 0.0f, -100.0f}, 1.0f);          // radius covers it exactly (distance 0)
    CHECK_FALSE(s.at(i).asleep);

    Sim off(cfg(0.0f, 0.0f));
    const std::size_t j = off.add({0.0f, 0.0f, -100.0f});
    off.tick(kFar);
    off.enemies.alert({0.0f, 0.0f, -100.0f}, 50.0f);
    CHECK_FALSE(off.at(j).asleep);
    CHECK(off.at(j).keepAwake == 0.0f);                    // nothing to keep awake when nobody sleeps
}

TEST_CASE("The alert radii are sensible: a blast carries furthest, a punch least", "[wake][alert]") {
    const ActivationConfig a;
    CHECK(a.blastAlertRadius > a.shotAlertRadius);
    CHECK(a.shotAlertRadius > a.meleeAlertRadius);
    CHECK(a.meleeAlertRadius > 0.0f);
    // Every alert radius is within the wake radius plus a margin, so noise never wakes
    // enemies that are meant to be out of the player's world.
    CHECK(a.blastAlertRadius <= a.wakeRadius);
}

// ---------------------------------------------------------------------------
// States that must never sleep
// ---------------------------------------------------------------------------

TEST_CASE("A seized enemy never falls asleep, even far from the player", "[wake][seize]") {
    ActivationConfig a;
    a.keepAwake = 0.0f;                                    // nothing but the Seized state keeps it up
    Sim s(a);
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    REQUIRE(s.at(i).asleep);

    REQUIRE(s.enemies.stagger(s.at(i).target));            // staggering wakes it
    CHECK_FALSE(s.at(i).asleep);
    const int staggerTicks = static_cast<int>(s.enemies.config().staggerTime / kDt);
    for (int t = 0; t < staggerTicks - 5; ++t) {
        s.tick(kFar);
        REQUIRE(s.at(i).state == AiState::Seized);
        REQUIRE_FALSE(s.at(i).asleep);
    }
}

TEST_CASE("Staggering wakes a sleeper and grabbing still works", "[wake][seize]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -2.0f});
    s.tick({0.0f, 0.0f, 0.0f});
    REQUIRE(s.enemies.stagger(s.at(i).target));
    const glm::vec3 eye{0.0f, 1.6f, 0.0f}, fwd{0.0f, 0.0f, -1.0f};
    CHECK(s.enemies.findStaggered(eye, fwd) == 0);
    s.enemies.grab(0);
    CHECK(s.enemies.heldIndex() == 0);
    s.tick({0.0f, 0.0f, 0.0f}, 10);
    CHECK(s.at(i).state == AiState::Seized);
    CHECK_FALSE(s.at(i).asleep);
}

// ---------------------------------------------------------------------------
// Objectives, death and respawn
// ---------------------------------------------------------------------------

TEST_CASE("Sleeping enemies still count for sector objectives", "[wake][objectives]") {
    Sim s;
    s.add({0.0f, 0.0f, -100.0f}, EnemyType::Melee, 0);
    s.add({10.0f, 0.0f, -100.0f}, EnemyType::Ranged, 0);
    s.tick(kFar);
    REQUIRE(s.enemies.activity().asleep == 2);
    CHECK(s.enemies.totalInSector(0) == 2);
    CHECK(s.enemies.aliveInSector(s.combat, 0) == 2);      // asleep is not dead: the sector is not clear

    s.target(0).hp = 0.0f;
    CHECK(s.enemies.aliveInSector(s.combat, 0) == 1);
    s.target(1).hp = 0.0f;
    CHECK(s.enemies.aliveInSector(s.combat, 0) == 0);
}

TEST_CASE("Activity counts awake, asleep and dead enemies", "[wake][stats]") {
    Sim s;
    s.add({0.0f, 0.0f, -5.0f});       // near the player: awake
    s.add({0.0f, 0.0f, -100.0f});     // far: asleep
    const std::size_t dead = s.add({0.0f, 0.0f, -110.0f});
    s.kill(dead);
    s.tick({0.0f, 0.0f, 0.0f});
    const EnemyActivity a = s.enemies.activity();
    CHECK(a.awake == 1);
    CHECK(a.asleep == 1);
    CHECK(a.dead == 1);

    s.enemies.clear();
    CHECK(s.enemies.activity().awake == 0);
    CHECK(s.enemies.activity().asleep == 0);
    CHECK(s.enemies.activity().dead == 0);
}

TEST_CASE("A dead enemy that respawns far away goes back to sleep", "[wake][respawn]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    REQUIRE(s.at(i).asleep);

    s.kill(i);
    s.tick(kFar);
    CHECK(s.enemies.activity().dead == 1);
    s.tick(kFar, 60 * 4);                                  // the respawn timer runs out
    CHECK(s.target(i).alive());
    CHECK(s.at(i).pos == glm::vec3(0.0f, 0.0f, -100.0f));  // back at its post
    CHECK(s.at(i).asleep);
    CHECK(s.enemies.activity().asleep == 1);
    CHECK(s.at(i).lastHp == Approx(s.target(i).maxHp));    // no phantom "damage" from the heal
}

TEST_CASE("Turning sleeping off wakes everyone", "[wake][config]") {
    Sim s;
    const std::size_t i = s.add({0.0f, 0.0f, -100.0f});
    s.tick(kFar);
    REQUIRE(s.at(i).asleep);
    s.enemies.setActivation(cfg(0.0f, 0.0f));
    CHECK_FALSE(s.at(i).asleep);
    s.tick(kFar);
    CHECK(s.enemies.activity().awake == 1);
    CHECK(s.enemies.activity().asleep == 0);
}

// ---------------------------------------------------------------------------
// Inside the active radius nothing changes
// ---------------------------------------------------------------------------

TEST_CASE("Inside the wake radius enemies behave exactly as with sleeping off", "[wake][equivalence]") {
    const auto populate = [](Sim& s) {
        s.add({0.0f, 0.0f, 0.0f}, EnemyType::Melee);
        s.add({6.0f, 0.0f, -4.0f}, EnemyType::Ranged);
        s.add({-8.0f, 0.0f, 3.0f}, EnemyType::Melee, 1);
        s.add({12.0f, 0.0f, 9.0f}, EnemyType::Ranged, 1);
        s.add({-14.0f, 0.0f, -12.0f}, EnemyType::Melee);
        s.add({3.0f, 0.0f, 16.0f}, EnemyType::Ranged);
    };
    Sim awake(cfg(0.0f, 0.0f)), napping;   // `napping` has the default radii, but nobody is far enough to sleep
    populate(awake);
    populate(napping);

    int attacks = 0, steps = 0, chasing = 0;
    for (int t = 0; t < 1200; ++t) {
        // The player runs a loop that stays within ~20 m of everyone.
        const float a = static_cast<float>(t) * 0.01f;
        const glm::vec3 player{std::cos(a) * 9.0f, 0.0f, std::sin(a) * 9.0f};
        awake.tick(player);
        napping.tick(player);

        REQUIRE(napping.enemies.activity().asleep == 0);
        REQUIRE(napping.enemies.attacks().size() == awake.enemies.attacks().size());
        for (std::size_t k = 0; k < awake.enemies.attacks().size(); ++k) {
            const EnemyAttack &x = awake.enemies.attacks()[k], &y = napping.enemies.attacks()[k];
            REQUIRE(x.damage == y.damage);
            REQUIRE(x.ranged == y.ranged);
            REQUIRE(x.from == y.from);
            REQUIRE(x.to == y.to);
        }
        REQUIRE(napping.enemies.footsteps() == awake.enemies.footsteps());
        for (std::size_t i = 0; i < awake.enemies.enemies().size(); ++i) {
            const Enemy &x = awake.at(i), &y = napping.at(i);
            REQUIRE(x.pos == y.pos);
            REQUIRE(x.vel == y.vel);
            REQUIRE(x.yaw == y.yaw);
            REQUIRE(x.state == y.state);
            REQUIRE(x.stateTime == y.stateTime);
            chasing += x.state == AiState::Chase ? 1 : 0;
        }
        attacks += static_cast<int>(awake.enemies.attacks().size());
        steps   += static_cast<int>(awake.enemies.footsteps().size());
    }
    CHECK(chasing > 100);   // the scenario really exercises the AI...
    CHECK(attacks > 0);     // ...including attacks...
    CHECK(steps > 20);      // ...and movement
}

TEST_CASE("Enemy behaviour is deterministic with sleeping on", "[wake][determinism]") {
    const auto run = [] {
        Sim s;
        for (int i = 0; i < 12; ++i)
            s.add({static_cast<float>(i) * 14.0f - 80.0f, 0.0f, -20.0f - static_cast<float>(i % 3) * 30.0f},
                  i % 2 ? EnemyType::Ranged : EnemyType::Melee);
        std::vector<glm::vec3> trace;
        for (int t = 0; t < 900; ++t) {
            const float a = static_cast<float>(t) * 0.004f;
            s.tick({std::sin(a) * 90.0f, 0.0f, std::cos(a) * 60.0f - 20.0f});
            for (const Enemy& e : s.enemies.enemies()) trace.push_back(e.pos);
            trace.emplace_back(static_cast<float>(s.enemies.activity().awake), static_cast<float>(s.enemies.activity().asleep), 0.0f);
        }
        return trace;
    };
    CHECK(run() == run());
}

TEST_CASE("Walking past a field of enemies wakes only the nearby ones", "[wake][scaling]") {
    Sim s;
    for (int i = 0; i < 40; ++i) s.add({static_cast<float>(i) * 25.0f, 0.0f, 0.0f});   // 1 km line of enemies
    glm::vec3 player{0.0f, 0.0f, 30.0f};
    int maxAwake = 0, minAsleep = 40;
    for (int t = 0; t < 60 * 60; ++t) {
        player.x = static_cast<float>(t) * (1000.0f / 3600.0f);                       // jog along it
        s.tick(player);
        maxAwake  = std::max(maxAwake, s.enemies.activity().awake);
        minAsleep = std::min(minAsleep, s.enemies.activity().asleep);
    }
    // A band about 2 * 65 m wide at 25 m spacing: roughly 5 to 7 enemies are ever awake at once.
    CHECK(maxAwake <= 8);
    CHECK(maxAwake >= 3);
    CHECK(minAsleep >= 30);
}

// ---------------------------------------------------------------------------
// Room lights
// ---------------------------------------------------------------------------

namespace {

// The code main.cpp used before: measure every light, partial-sort the nearest few, and
// stop at the first one beyond the cull distance.
std::vector<std::size_t> legacySelect(std::span<const WorldLight> lights, glm::vec3 eye, std::size_t maxLights, float cull) {
    std::vector<std::pair<float, std::size_t>> pick;
    for (std::size_t i = 0; i < lights.size(); ++i) pick.emplace_back(glm::distance(lights[i].pos, eye), i);
    const std::size_t n = std::min(maxLights, pick.size());
    std::partial_sort(pick.begin(), pick.begin() + static_cast<std::ptrdiff_t>(n), pick.end());
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < n; ++i) {
        if (pick[i].first > cull) break;
        out.push_back(pick[i].second);
    }
    return out;
}

WorldLight lightAt(glm::vec3 p) {
    WorldLight l;
    l.pos = p;
    return l;
}

} // namespace

TEST_CASE("selectLights keeps the nearest few lights, nearest first", "[wake][lights]") {
    std::vector<WorldLight> lights;
    for (const float x : {30.0f, 5.0f, 12.0f, 1.0f, 20.0f, 8.0f}) lights.push_back(lightAt({x, 0.0f, 0.0f}));
    std::vector<std::size_t> out;

    const LightSelection r = selectLights(lights, {0.0f, 0.0f, 0.0f}, 3, 100.0f, out);
    CHECK(out == std::vector<std::size_t>{3, 1, 5});       // x = 1, 5, 8
    CHECK(r.considered == 6);
    CHECK(r.picked == 3);
    CHECK(r.culled == 0);
}

TEST_CASE("selectLights culls by distance before picking", "[wake][lights]") {
    std::vector<WorldLight> lights;
    for (const float x : {10.0f, 39.9f, 40.0f, 40.1f, 90.0f, 1000.0f}) lights.push_back(lightAt({x, 0.0f, 0.0f}));
    std::vector<std::size_t> out;
    const LightSelection r = selectLights(lights, {0.0f, 0.0f, 0.0f}, 8, 40.0f, out);
    CHECK(out == std::vector<std::size_t>{0, 1, 2});       // 40.0 is inside, 40.1 is not
    CHECK(r.culled == 3);
    CHECK(r.picked == 3);
    CHECK(r.considered == 6);

    SECTION("nothing in range") {
        const LightSelection none = selectLights(lights, {500.0f, 0.0f, 500.0f}, 4, 40.0f, out);
        CHECK(out.empty());
        CHECK(none.culled == 6);
        CHECK(none.picked == 0);
    }
}

TEST_CASE("selectLights edge cases", "[wake][lights]") {
    std::vector<std::size_t> out{99, 98};

    SECTION("no lights") {
        const LightSelection r = selectLights({}, {0, 0, 0}, 4, 40.0f, out);
        CHECK(out.empty());                                  // stale output is cleared
        CHECK(r.considered == 0);
    }
    SECTION("a limit of zero picks nothing") {
        std::vector<WorldLight> lights{lightAt({1, 0, 0}), lightAt({2, 0, 0})};
        const LightSelection r = selectLights(lights, {0, 0, 0}, 0, 40.0f, out);
        CHECK(out.empty());
        CHECK(r.culled == 2);
    }
    SECTION("equal distances keep index order") {
        std::vector<WorldLight> lights;
        for (int i = 0; i < 10; ++i) lights.push_back(lightAt({0.0f, 5.0f, 0.0f}));      // identical positions
        selectLights(lights, {0, 0, 0}, 4, 40.0f, out);
        CHECK(out == std::vector<std::size_t>{0, 1, 2, 3});
    }
    SECTION("a NaN or infinite position is never picked") {
        std::vector<WorldLight> lights{lightAt({std::numeric_limits<float>::quiet_NaN(), 0, 0}),
                                       lightAt({std::numeric_limits<float>::infinity(), 0, 0}), lightAt({3, 0, 0})};
        const LightSelection r = selectLights(lights, {0, 0, 0}, 4, 1e9f, out);
        CHECK(out == std::vector<std::size_t>{2});
        CHECK(r.culled == 2);
    }
    SECTION("an absurd limit is capped") {
        std::vector<WorldLight> lights;
        for (int i = 0; i < 100; ++i) lights.push_back(lightAt({static_cast<float>(i), 0, 0}));
        selectLights(lights, {0, 0, 0}, 1000, 1e9f, out);
        CHECK(out.size() == kMaxPickedLights);
        CHECK(out.front() == 0);
    }
    SECTION("the eye position matters") {
        std::vector<WorldLight> lights{lightAt({0, 0, 0}), lightAt({100, 0, 0})};
        selectLights(lights, {95, 0, 0}, 1, 40.0f, out);
        CHECK(out == std::vector<std::size_t>{1});
    }
}

TEST_CASE("selectLights matches the code it replaced on random levels", "[wake][lights][equivalence]") {
    std::uint32_t s = 17;
    const auto rnd = [&] { s = s * 1664525u + 1013904223u; return static_cast<float>((s >> 8) & 0xFFFFFF) / 16777216.0f; };
    std::vector<std::size_t> out;
    for (int level = 0; level < 300; ++level) {
        std::vector<WorldLight> lights;
        const int count = static_cast<int>(rnd() * 120.0f);
        const float extent = 20.0f + rnd() * 400.0f;
        for (int i = 0; i < count; ++i) {
            // a coarse grid of positions makes exact distance ties common
            const float step = rnd() < 0.5f ? 5.0f : 0.01f;
            lights.push_back(lightAt({std::round((rnd() - 0.5f) * extent / step) * step, std::round(rnd() * 10.0f),
                                      std::round((rnd() - 0.5f) * extent / step) * step}));
        }
        const glm::vec3 eye{(rnd() - 0.5f) * extent, 1.7f, (rnd() - 0.5f) * extent};
        const std::size_t maxLights = 1 + static_cast<std::size_t>(rnd() * 7.0f);
        const float cull = 5.0f + rnd() * 80.0f;
        selectLights(lights, eye, maxLights, cull, out);
        REQUIRE(out == legacySelect(lights, eye, maxLights, cull));
    }
}

TEST_CASE("selectLights handles a huge level in one pass", "[wake][lights][scaling]") {
    std::vector<WorldLight> lights;
    for (int i = 0; i < 20000; ++i)
        lights.push_back(lightAt({static_cast<float>(i % 200) * 10.0f, 5.0f, static_cast<float>(i / 200) * 10.0f}));
    std::vector<std::size_t> out;
    const LightSelection r = selectLights(lights, {1000.0f, 1.7f, 500.0f}, 4, 40.0f, out);
    CHECK(r.considered == 20000);
    CHECK(out.size() == 4);
    CHECK(r.culled > 19900);
    CHECK(r.culled + r.picked <= r.considered);
    CHECK(out == legacySelect(lights, {1000.0f, 1.7f, 500.0f}, 4, 40.0f));
}

// ---------------------------------------------------------------------------
// Audible range and the stats readout
// ---------------------------------------------------------------------------

TEST_CASE("inAudibleRange agrees exactly with when spatial audio is silent", "[wake][audio]") {
    const audio::Listener l;                                // at the origin, facing -Z, right = +X
    CHECK(audio::inAudibleRange(0.0f));
    CHECK(audio::inAudibleRange(44.99f));
    CHECK_FALSE(audio::inAudibleRange(45.0f));
    CHECK_FALSE(audio::inAudibleRange(500.0f));

    for (const glm::vec3 dir : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 0, 1),
                                glm::normalize(glm::vec3(1, 1, 1))}) {
        for (float d = 0.0f; d < 120.0f; d += 0.37f) {
            INFO("distance " << d);
            const bool audible = audio::spatialize(l, dir * d).gain > 0.0f;
            CHECK(audio::inAudibleRange(d) == audible);
        }
    }
}

TEST_CASE("The stats overlay draws the new AI, light and world lines on screen", "[wake][stats]") {
    dev::DevState d;
    d.showStats = true;
    dev::FrameProfile p;
    dev::StatsInfo big;
    big.fps = 144; big.ticksPerSec = 60;
    big.enemiesAlive = 120; big.enemiesAwake = 120; big.enemiesAsleep = 120;
    big.lightsSubmitted = 4; big.lightsTotal = 250;
    big.worldQueriesPerSec = 123456; big.worldBlocksPerQuery = 12.34f;

    dev::StatsInfo none;
    std::vector<OverlayRect> a, b;
    dev::drawStats(d, p, big, 16.0f / 9.0f, a);
    dev::drawStats(d, p, none, 16.0f / 9.0f, b);
    CHECK_FALSE(a.empty());
    CHECK(a.size() > b.size());                              // more text: bigger numbers have more digits

    for (const float aspect : {1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
        std::vector<OverlayRect> out;
        dev::drawStats(d, p, big, aspect, out);
        for (const OverlayRect& r : out) {
            REQUIRE(r.min.x >= -1.0f);                      // the longest line still fits on the screen
            REQUIRE(r.max.x <= 1.0f);
            REQUIRE(r.min.y >= -1.0f);
        }
    }
}
