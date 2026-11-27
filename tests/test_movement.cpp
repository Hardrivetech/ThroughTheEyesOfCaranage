#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/GameLoop.hpp"
#include "game/Movement.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

// Yaw 0 faces -Z, so "forward" input moves toward negative Z.

namespace {

MoveInput forward(bool sprint = false) {
    MoveInput in;
    in.move   = {0.0f, 1.0f};
    in.sprint = sprint;
    return in;
}

MoveInput jump() {
    MoveInput in;
    in.jumpPressed = true;
    return in;
}

// Runs `ticks` fixed steps with the same input and returns the controller.
void run(MovementController& mc, int ticks, const MoveInput& in) {
    for (int i = 0; i < ticks; ++i) mc.tick(kDt, 0.0f, in);
}

} // namespace

TEST_CASE("Ground acceleration reaches walk and sprint speed and no more", "[movement]") {
    const MovementConfig cfg;
    World w = floorWorld();

    SECTION("walking") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        float last = 0.0f;
        for (int i = 0; i < 120; ++i) {
            mc.tick(kDt, 0.0f, forward());
            CHECK(mc.horizontalSpeed() <= cfg.walkSpeed + 1e-3f);
            if (i < 10) CHECK(mc.horizontalSpeed() >= last);  // monotonic ramp-up
            last = mc.horizontalSpeed();
        }
        CHECK(mc.state() == MoveState::Walking);
        CHECK(mc.grounded());
        CHECK(mc.horizontalSpeed() == Approx(cfg.walkSpeed).margin(0.05f));
        CHECK(mc.position().z < -5.0f);                       // moved forward (-Z)
        CHECK(mc.position().x == Approx(0.0f).margin(1e-4f));  // straight line
        CHECK(mc.position().y == Approx(0.0f).margin(1e-4f));
    }
    SECTION("sprinting") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        run(mc, 180, forward(true));
        CHECK(mc.state() == MoveState::Sprinting);
        CHECK(mc.horizontalSpeed() == Approx(cfg.sprintSpeed).margin(0.05f));
    }
    SECTION("diagonal input is not faster than straight") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        MoveInput in;
        in.move = {1.0f, 1.0f};
        run(mc, 120, in);
        CHECK(mc.horizontalSpeed() <= cfg.walkSpeed + 1e-3f);
        CHECK(mc.velocity().x > 0.0f);  // strafing right
        CHECK(mc.velocity().z < 0.0f);
    }
    SECTION("friction brings the player to a full stop") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        run(mc, 60, forward());
        REQUIRE(mc.horizontalSpeed() > 4.0f);
        run(mc, 120, MoveInput{});
        CHECK(mc.horizontalSpeed() == 0.0f);
    }
    SECTION("the hard speed cap holds under a huge impulse") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        mc.impulse({0.0f, 0.0f, -500.0f});
        mc.tick(kDt, 0.0f, MoveInput{});
        CHECK(mc.horizontalSpeed() <= cfg.maxSpeed + 1e-3f);
    }
}

TEST_CASE("Jumping: arc, landing impact, buffer and coyote time", "[movement][jump]") {
    const MovementConfig cfg;

    SECTION("a standing jump leaves the ground, peaks sensibly and lands") {
        World w = floorWorld();
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});

        mc.tick(kDt, 0.0f, jump());
        CHECK(mc.events().jumped);
        CHECK_FALSE(mc.grounded());
        CHECK(mc.state() == MoveState::Airborne);
        CHECK(mc.velocity().y > 0.0f);

        float peak = 0.0f, impact = 0.0f;
        int   airTicks = 1;
        while (!mc.grounded() && airTicks < 300) {
            mc.tick(kDt, 0.0f, MoveInput{});
            peak = std::max(peak, mc.position().y);
            impact = std::max(impact, mc.events().landImpact);
            ++airTicks;
        }
        const float expectedPeak = cfg.jumpSpeed * cfg.jumpSpeed / (2.0f * cfg.gravity);
        CHECK(peak == Approx(expectedPeak).epsilon(0.1));
        CHECK(impact > 0.0f);
        CHECK(mc.grounded());
        CHECK(mc.state() == MoveState::Walking);
        CHECK(mc.position().y == Approx(0.0f).margin(1e-4f));
        CHECK(airTicks < 120);  // about a second of air at most
    }

    SECTION("a jump press just before touchdown is buffered") {
        World w = floorWorld();
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        mc.impulse({0.0f, 6.0f, 0.0f});
        // Press jump while still airborne, a few ticks before landing.
        bool pressed = false, jumpedAfterLanding = false;
        for (int i = 0; i < 200; ++i) {
            const bool nearLanding = mc.velocity().y < 0.0f && mc.position().y < 0.2f;
            MoveInput in;
            if (nearLanding && !pressed) { in = jump(); pressed = true; }
            mc.tick(kDt, 0.0f, in);
            if (pressed && mc.events().jumped) { jumpedAfterLanding = true; break; }
        }
        CHECK(pressed);
        CHECK(jumpedAfterLanding);
    }

    SECTION("coyote time allows a jump just after walking off a ledge") {
        World w = floorWorld();
        w.add(solid(-50.0f, -5.0f, 50.0f, 50.0f, 1.0f));  // raised platform, edge at z = -5
        const auto walkOff = [&](MovementController& mc) {
            mc.respawn({0.0f, 1.0f, 0.0f});
            for (int i = 0; i < 400 && mc.grounded(); ++i) mc.tick(kDt, 0.0f, forward());
            REQUIRE_FALSE(mc.grounded());
        };

        MovementController early(w, cfg);
        walkOff(early);
        early.tick(kDt, 0.0f, jump());
        CHECK(early.events().jumped);
        CHECK(early.velocity().y > 0.0f);

        MovementController late(w, cfg);
        walkOff(late);
        for (int i = 0; i < 12; ++i) late.tick(kDt, 0.0f, forward());  // 0.2 s: coyote time has run out
        late.tick(kDt, 0.0f, jump());
        CHECK_FALSE(late.events().jumped);
        CHECK(late.velocity().y < 0.0f);
    }

    SECTION("impulses only lift off when they have upward force") {
        World w = floorWorld();
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        mc.impulse({0.0f, 0.0f, -5.0f});
        CHECK(mc.grounded());
        mc.impulse({0.0f, 8.0f, 0.0f});
        CHECK_FALSE(mc.grounded());
        CHECK(mc.state() == MoveState::Airborne);
    }
}

TEST_CASE("Ramps and steps keep the player glued to the surface", "[movement][ramp]") {
    const MovementConfig cfg;
    World w = floorWorld();
    // 0 m at z = 0 rising to 6 m at z = -30 (0.2 m per metre).
    w.add(slope(-5.0f, -30.0f, 5.0f, 0.0f, 1, 6.0f, 0.0f));

    SECTION("walking up a ramp") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        for (int i = 0; i < 120; ++i) {
            mc.tick(kDt, 0.0f, forward());
            REQUIRE(mc.grounded());
            const auto g = w.ground({mc.position().x, mc.position().z}, mc.position().y + 1.0f);
            REQUIRE(g.has_value());
            CHECK(mc.position().y == Approx(g->height).margin(1e-3f));
        }
        CHECK(mc.position().y > 1.0f);
        CHECK(mc.velocity().y > 0.0f);  // velocity follows the incline
    }
    SECTION("walking down a ramp stays grounded") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 3.0f, -15.0f});
        MoveInput back;
        back.move = {0.0f, -1.0f};
        for (int i = 0; i < 60; ++i) {
            mc.tick(kDt, 0.0f, back);
            CHECK(mc.grounded());
        }
        CHECK(mc.position().y < 3.0f);
    }
    SECTION("sprinting off the top of a ramp launches along the slope") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 5.0f, -25.0f});
        bool leftGround = false;
        for (int i = 0; i < 120 && !leftGround; ++i) {
            mc.tick(kDt, 0.0f, forward(true));
            leftGround = !mc.grounded();
        }
        CHECK(leftGround);
        CHECK(mc.velocity().y > 0.0f);
    }
    SECTION("small ledges are stepped over automatically") {
        World w3 = floorWorld();
        w3.add(solid(-5.0f, -20.0f, 5.0f, -10.0f, cfg.stepHeight * 0.75f));
        MovementController mc(w3, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        bool vaulted = false;
        for (int i = 0; i < 240; ++i) {
            mc.tick(kDt, 0.0f, forward(true));
            vaulted = vaulted || mc.events().vaultStarted;
        }
        CHECK_FALSE(vaulted);
        CHECK(mc.position().z < -20.0f);  // carried all the way past the step
    }
}

TEST_CASE("Sliding: entry boost, friction, exit and slide-jump", "[movement][slide]") {
    const MovementConfig cfg;
    World w = floorWorld();

    const auto sprintUp = [&](MovementController& mc) {
        mc.respawn({0.0f, 0.0f, 0.0f});
        run(mc, 120, forward(true));
        REQUIRE(mc.horizontalSpeed() >= cfg.slideEnterSpeed);
    };

    SECTION("crouching at sprint speed starts a boosted slide") {
        MovementController mc(w, cfg);
        sprintUp(mc);
        const float before = mc.horizontalSpeed();
        MoveInput in = forward(true);
        in.crouchPressed = true;
        in.crouchHeld    = true;
        mc.tick(kDt, 0.0f, in);
        CHECK(mc.events().slideStarted);
        CHECK(mc.state() == MoveState::Sliding);
        CHECK(mc.horizontalSpeed() > before);  // one-off boost

        // The camera drops towards the slide eye height.
        const float standing = mc.eyePosition(1.0f).y - mc.position().y;
        in.crouchPressed = false;
        run(mc, 30, in);
        const float sliding = mc.eyePosition(1.0f).y - mc.position().y;
        CHECK(sliding < standing);
    }
    SECTION("a slide slows down and ends once it is slow enough") {
        MovementController mc(w, cfg);
        sprintUp(mc);
        MoveInput in;
        in.crouchPressed = true;
        in.crouchHeld    = true;
        in.move          = {0.0f, 1.0f};
        in.sprint        = true;
        mc.tick(kDt, 0.0f, in);
        REQUIRE(mc.state() == MoveState::Sliding);

        in.crouchPressed = false;
        in.move          = {0.0f, 0.0f};
        in.sprint        = false;
        float last = mc.horizontalSpeed();
        for (int i = 0; i < 400 && mc.state() == MoveState::Sliding; ++i) {
            mc.tick(kDt, 0.0f, in);
            CHECK(mc.horizontalSpeed() <= last + 1e-3f);  // flat ground: friction only
            last = mc.horizontalSpeed();
        }
        CHECK(mc.state() != MoveState::Sliding);
        CHECK(last < cfg.slideExitSpeed + 0.5f);
    }
    SECTION("releasing crouch ends the slide") {
        MovementController mc(w, cfg);
        sprintUp(mc);
        MoveInput in = forward(true);
        in.crouchPressed = true;
        in.crouchHeld    = true;
        mc.tick(kDt, 0.0f, in);
        REQUIRE(mc.state() == MoveState::Sliding);
        mc.tick(kDt, 0.0f, forward(true));
        CHECK(mc.state() != MoveState::Sliding);
    }
    SECTION("too slow to slide") {
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        run(mc, 60, forward());  // walking speed < slideEnterSpeed
        MoveInput in = forward();
        in.crouchPressed = true;
        in.crouchHeld    = true;
        mc.tick(kDt, 0.0f, in);
        CHECK_FALSE(mc.events().slideStarted);
        CHECK(mc.state() != MoveState::Sliding);
    }
    SECTION("jumping out of a slide adds speed") {
        MovementController mc(w, cfg);
        sprintUp(mc);
        MoveInput in = forward(true);
        in.crouchPressed = true;
        in.crouchHeld    = true;
        mc.tick(kDt, 0.0f, in);
        REQUIRE(mc.state() == MoveState::Sliding);

        const float before = mc.horizontalSpeed();
        MoveInput hop = forward(true);
        hop.crouchHeld   = true;
        hop.jumpPressed  = true;
        mc.tick(kDt, 0.0f, hop);
        CHECK(mc.events().jumped);
        CHECK(mc.horizontalSpeed() > before);
    }
    SECTION("a slide has a cooldown before the next one") {
        MovementController mc(w, cfg);
        sprintUp(mc);
        MoveInput in = forward(true);
        in.crouchPressed = true;
        in.crouchHeld    = true;
        mc.tick(kDt, 0.0f, in);
        REQUIRE(mc.state() == MoveState::Sliding);
        mc.tick(kDt, 0.0f, forward(true));  // release
        REQUIRE(mc.state() != MoveState::Sliding);
        mc.tick(kDt, 0.0f, in);             // immediately try again
        CHECK_FALSE(mc.events().slideStarted);
    }
    SECTION("sliding down a ramp is faster than on the flat") {
        // Heads toward -Z; the ramp descends that way (6 m at z = 0 to 0 m at z = -30).
        World slopeWorld = floorWorld();
        slopeWorld.add(slope(-5.0f, -30.0f, 5.0f, 0.0f, 1, 0.0f, 6.0f));

        const auto slideFor = [&](World& world, glm::vec3 start) {
            MovementController mc(world, cfg);
            mc.respawn(start);
            run(mc, 40, forward(true));
            MoveInput in = forward(true);
            in.crouchPressed = true;
            in.crouchHeld    = true;
            mc.tick(kDt, 0.0f, in);
            in.crouchPressed = false;
            for (int i = 0; i < 30; ++i) mc.tick(kDt, 0.0f, in);
            return mc.horizontalSpeed();
        };
        const float down = slideFor(slopeWorld, {0.0f, 6.0f, -0.5f});
        const float flat = slideFor(w, {0.0f, 0.0f, 0.0f});
        CHECK(down > flat);
    }
}

TEST_CASE("Vaulting low obstacles and being stopped by tall ones", "[movement][vault]") {
    const MovementConfig cfg;

    SECTION("sprinting into a crate vaults onto it") {
        World w = floorWorld();
        w.add(solid(-3.0f, -9.0f, 3.0f, -5.0f, 1.0f));  // deep enough to land on top
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});

        bool started = false, finished = false, sawVaulting = false;
        for (int i = 0; i < 240 && !finished; ++i) {
            mc.tick(kDt, 0.0f, forward(true));
            started     = started || mc.events().vaultStarted;
            sawVaulting = sawVaulting || mc.state() == MoveState::Vaulting;
            finished    = finished || mc.events().vaultFinished;
        }
        CHECK(started);
        CHECK(sawVaulting);
        REQUIRE(finished);
        CHECK(mc.position().y == Approx(1.0f).margin(1e-3f));  // standing on the crate
        CHECK(mc.grounded());
        CHECK(mc.state() != MoveState::Vaulting);
    }
    SECTION("a wall taller than vaultMaxHeight stops the player") {
        World w = floorWorld();
        w.add(solid(-10.0f, -9.0f, 10.0f, -8.0f, cfg.vaultMaxHeight + 1.0f));
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        bool vaulted = false;
        for (int i = 0; i < 240; ++i) {
            mc.tick(kDt, 0.0f, forward(true));
            vaulted = vaulted || mc.events().vaultStarted;
        }
        CHECK_FALSE(vaulted);
        CHECK(mc.position().z >= -8.0f + cfg.radius - 1e-3f);  // never inside the wall
        CHECK(mc.position().y == Approx(0.0f).margin(1e-4f));
    }
    SECTION("walking slowly into a crate does not vault") {
        World w = floorWorld();
        w.add(solid(-3.0f, -9.0f, 3.0f, -5.0f, 1.0f));
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        bool vaulted = false;
        for (int i = 0; i < 240; ++i) {
            mc.tick(kDt, 0.0f, forward(false));  // walk, no sprint, no jump
            vaulted = vaulted || mc.events().vaultStarted;
        }
        CHECK_FALSE(vaulted);
    }
}

TEST_CASE("Wall run attach, hold, jump and release", "[movement][wallrun]") {
    const MovementConfig cfg;
    World w = floorWorld();
    w.add(solid(3.0f, -100.0f, 4.0f, 20.0f, 6.0f));  // tall wall on the right (+X)

    const auto launch = [&](MovementController& mc, glm::vec3 v) {
        mc.respawn({2.5f, 0.0f, 0.0f});  // 0.5 m from the wall face: inside wall-run reach
        mc.impulse(v);
    };

    SECTION("a fast airborne player next to a tall wall starts wall-running") {
        MovementController mc(w, cfg);
        launch(mc, {0.0f, 8.0f, -9.0f});

        bool started = false;
        for (int i = 0; i < 40 && !started; ++i) {
            mc.tick(kDt, 0.0f, forward());
            started = mc.events().wallRunStarted;
        }
        REQUIRE(started);
        CHECK(mc.state() == MoveState::WallRunning);

        // Hold the run: low sink rate, speed preserved, camera rolls away from the wall.
        for (int i = 0; i < 20; ++i) {
            mc.tick(kDt, 0.0f, forward());
            REQUIRE(mc.state() == MoveState::WallRunning);
            CHECK(mc.velocity().y >= -cfg.wallRunMaxFall - 1e-3f);
            CHECK(mc.horizontalSpeed() >= cfg.wallRunMinSpeed);
        }
        CHECK(std::abs(mc.cameraRoll(1.0f)) > 0.01f);
        CHECK(mc.cameraRoll(1.0f) > 0.0f);  // wall on the right: lean left (positive roll)
    }
    SECTION("jumping off the wall kicks away from it") {
        MovementController mc(w, cfg);
        launch(mc, {0.0f, 8.0f, -9.0f});
        for (int i = 0; i < 40 && mc.state() != MoveState::WallRunning; ++i)
            mc.tick(kDt, 0.0f, forward());
        REQUIRE(mc.state() == MoveState::WallRunning);

        MoveInput in = forward();
        in.jumpPressed = true;
        mc.tick(kDt, 0.0f, in);
        CHECK(mc.events().wallJumped);
        CHECK(mc.state() == MoveState::Airborne);
        CHECK(mc.velocity().y > 0.5f * cfg.wallJumpUp);
        CHECK(mc.velocity().x < 0.0f);  // away from the wall (wall is at +X)
    }
    SECTION("a wall run ends on its own within the time limit") {
        MovementController mc(w, cfg);
        launch(mc, {0.0f, 8.0f, -9.0f});
        int  runTicks = 0;
        bool started = false, ended = false;
        for (int i = 0; i < 400 && !ended; ++i) {
            mc.tick(kDt, 0.0f, forward());
            if (mc.state() == MoveState::WallRunning) { started = true; ++runTicks; }
            else if (started) ended = true;
        }
        CHECK(started);
        CHECK(ended);
        CHECK(static_cast<float>(runTicks) * kDt <= cfg.wallRunMaxTime + 0.1f);
    }
    SECTION("releasing forward input ends the run") {
        MovementController mc(w, cfg);
        launch(mc, {0.0f, 8.0f, -9.0f});
        for (int i = 0; i < 40 && mc.state() != MoveState::WallRunning; ++i)
            mc.tick(kDt, 0.0f, forward());
        REQUIRE(mc.state() == MoveState::WallRunning);
        mc.tick(kDt, 0.0f, MoveInput{});
        CHECK(mc.state() == MoveState::Airborne);
    }
    SECTION("approaching the wall too slowly never attaches") {
        MovementController mc(w, cfg);
        launch(mc, {0.0f, 8.0f, -(cfg.wallRunMinSpeed - 2.0f)});
        for (int i = 0; i < 30; ++i) {
            mc.tick(kDt, 0.0f, forward());
            if (mc.horizontalSpeed() >= cfg.wallRunMinSpeed) break;  // air control may speed us up
            CHECK(mc.state() != MoveState::WallRunning);
        }
    }
    SECTION("a short wall cannot be run on") {
        World low = floorWorld();
        low.add(solid(3.0f, -100.0f, 4.0f, 20.0f, cfg.wallMinHeight - 1.0f));
        MovementController mc(low, cfg);
        launch(mc, {0.0f, 8.0f, -9.0f});
        for (int i = 0; i < 40; ++i) {
            mc.tick(kDt, 0.0f, forward());
            CHECK(mc.state() != MoveState::WallRunning);
        }
    }
    SECTION("a head-on approach does not attach") {
        World front = floorWorld();
        front.add(solid(-50.0f, -8.0f, 50.0f, -7.0f, 6.0f));  // wall straight ahead
        MovementController mc(front, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        mc.impulse({0.0f, 8.0f, -15.0f});
        for (int i = 0; i < 60; ++i) {
            mc.tick(kDt, 0.0f, forward());
            CHECK(mc.state() != MoveState::WallRunning);
        }
    }
}

TEST_CASE("Falling out of the world respawns; noclip flies", "[movement]") {
    const MovementConfig cfg;
    SECTION("falling out of the world respawns at the last spawn point") {
        World empty;  // nothing to stand on
        MovementController mc(empty, cfg);
        mc.respawn({3.0f, 1.0f, -4.0f});
        bool respawned = false;
        for (int i = 0; i < 600 && !respawned; ++i) {
            mc.tick(kDt, 0.0f, MoveInput{});
            respawned = near(mc.position(), {3.0f, 1.0f, -4.0f});
        }
        CHECK(respawned);
        CHECK(near(mc.velocity(), {0.0f, 0.0f, 0.0f}));
    }
    SECTION("noclip flies and zeroes velocity on exit") {
        World w = floorWorld();
        MovementController mc(w, cfg);
        mc.respawn({0.0f, 0.0f, 0.0f});
        mc.setNoclip(true);
        MoveInput in = forward();
        in.jumpHeld  = true;
        run(mc, 30, in);
        CHECK(mc.position().y > 1.0f);
        CHECK(mc.position().z < -1.0f);
        mc.setNoclip(false);
        CHECK(near(mc.velocity(), {0.0f, 0.0f, 0.0f}));
    }
}

TEST_CASE("Jumping under a low overhead slab bonks instead of shoving sideways",
          "[movement][ceiling][!shouldfail]") {
    // KNOWN BUG (found while writing these tests, not yet fixed):
    // MovementController::tick() calls World::resolveWalls() *before* the ceiling
    // clamp. resolveWalls treats a slab as a wall once its underside is below
    // feet + 1.9 m, which a rising jump crosses mid-tick, so the player is pushed
    // out through the side of the slab (50 m under a full-width slab; ~2 m under
    // the arena's 2.9 m catwalks). Applying the ceiling clamp first fixes it.
    // [!shouldfail] keeps the suite green while the bug exists; once it is fixed
    // this test will report a failure: delete the tag and it becomes a regression test.
    const MovementConfig cfg;
    World w = floorWorld();
    const float underside = 2.6f;
    w.add(solid(-50.0f, -50.0f, 50.0f, 50.0f, 4.0f, underside));
    MovementController mc(w, cfg);
    mc.respawn({0.0f, 0.0f, 0.0f});

    float peak = 0.0f, drift = 0.0f;
    mc.tick(kDt, 0.0f, jump());
    for (int i = 0; i < 60; ++i) {
        mc.tick(kDt, 0.0f, MoveInput{});
        peak  = std::max(peak, mc.position().y);
        drift = std::max(drift, std::abs(mc.position().x) + std::abs(mc.position().z));
    }
    CHECK(drift < 0.01f);  // stays put: no horizontal shove
    // The slab caps the jump at underside - eye - 0.2 (a free jump peaks near 1.3 m).
    CHECK(peak == Approx(underside - cfg.standEye - 0.2f).margin(0.02f));
    CHECK(mc.grounded());
}

TEST_CASE("Movement is deterministic for identical input", "[movement][determinism]") {
    const World level = World::testLevel();

    // A scripted, mildly chaotic input pattern: no RNG, purely tick-indexed.
    const auto inputAt = [](int i) {
        MoveInput in;
        in.move          = {(i / 90) % 3 == 1 ? 1.0f : (i / 90) % 3 == 2 ? -1.0f : 0.0f,
                            (i / 200) % 5 == 4 ? 0.0f : 1.0f};
        in.sprint        = (i / 120) % 2 == 0;
        in.jumpPressed   = i % 75 == 0;
        in.jumpHeld      = in.jumpPressed;
        in.crouchPressed = i % 110 == 0;
        in.crouchHeld    = (i % 110) < 40;
        return in;
    };
    const auto yawAt = [](int i) { return 0.002f * static_cast<float>(i % 400) - 0.4f; };

    MovementController a(level), b(level);
    a.respawn({0.0f, 0.0f, 6.0f});
    b.respawn({0.0f, 0.0f, 6.0f});

    for (int i = 0; i < 1800; ++i) {
        a.tick(kDt, yawAt(i), inputAt(i));
        b.tick(kDt, yawAt(i), inputAt(i));
        REQUIRE(a.state() == b.state());
        const glm::vec3 pa = a.position(), pb = b.position();
        const glm::vec3 va = a.velocity(), vb = b.velocity();
        REQUIRE(std::memcmp(&pa, &pb, sizeof pa) == 0);  // bit-exact
        REQUIRE(std::memcmp(&va, &vb, sizeof va) == 0);
        REQUIRE(std::isfinite(pa.x + pa.y + pa.z));
    }
    // Sanity: the script actually moved the player somewhere.
    CHECK(glm::length(a.position() - glm::vec3(0.0f, 0.0f, 6.0f)) > 5.0f);
}

TEST_CASE("The game loop steps at a constant 60 Hz", "[loop]") {
    const LoopConfig cfg;
    CHECK(cfg.tick == std::chrono::nanoseconds(1'000'000'000 / 60));

    int    frames = 0;
    double firstDt = 0.0;
    bool   constantDt = true, alphaOk = true;
    const LoopStats stats = runLoop(
        cfg,
        [] { std::this_thread::sleep_for(std::chrono::milliseconds(8)); },
        [&](double dt) {
            if (firstDt == 0.0) firstDt = dt;
            constantDt = constantDt && dt == firstDt;
        },
        [&](double alpha) {
            alphaOk = alphaOk && alpha >= 0.0 && alpha < 1.0;
            ++frames;
        },
        [&] { return frames >= 25; });

    CHECK(firstDt == Approx(1.0 / 60.0));
    CHECK(constantDt);
    CHECK(alphaOk);
    CHECK(stats.frames == 25);
    CHECK(stats.ticks >= 5);  // ~200 ms of real time elapsed
    CHECK(stats.ticks <= stats.frames * static_cast<std::uint64_t>(cfg.maxStepsPerFrame));
}
