#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/World.hpp"
#include "test_helpers.hpp"

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

TEST_CASE("Block surface, containment and normals follow the slope", "[world]") {
    const Block flat = solid(0.0f, 0.0f, 2.0f, 2.0f, 1.5f);
    CHECK(flat.surface({1.0f, 1.0f}) == Approx(1.5f));
    CHECK(flat.topHeight() == Approx(1.5f));
    CHECK(near(flat.normal(), {0.0f, 1.0f, 0.0f}));

    // Rises from 0 at z = 0 to 2 at z = 4 (slope 0.5).
    const Block ramp = slope(-1.0f, 0.0f, 1.0f, 4.0f, 1, 0.0f, 2.0f);
    CHECK(ramp.surface({0.0f, 2.0f}) == Approx(1.0f));
    CHECK(ramp.surface({0.0f, -5.0f}) == Approx(0.0f));  // clamped to the low end
    CHECK(ramp.surface({0.0f, 10.0f}) == Approx(2.0f));  // clamped to the high end
    CHECK(ramp.topHeight() == Approx(2.0f));
    CHECK(ramp.contains({0.0f, 2.0f}));
    CHECK_FALSE(ramp.contains({2.0f, 2.0f}));

    const glm::vec3 n = ramp.normal();
    CHECK(glm::length(n) == Approx(1.0f));
    CHECK(n.y > 0.0f);
    CHECK(n.z < 0.0f);  // a surface rising toward +Z tilts its normal toward -Z
    CHECK(glm::dot(n, glm::vec3(0.0f, 0.5f, 1.0f)) == Approx(0.0f).margin(1e-5));  // normal is perpendicular to the slope

    const Block xRamp = slope(0.0f, -1.0f, 4.0f, 1.0f, 0, 0.0f, 2.0f);
    CHECK(xRamp.normal().x < 0.0f);
    CHECK(xRamp.normal().z == Approx(0.0f).margin(1e-6));
}

TEST_CASE("World raycast hits, misses and respects max distance", "[world][raycast]") {
    World w = floorWorld();
    const glm::vec3 down{0.0f, -1.0f, 0.0f};

    SECTION("straight down onto the floor") {
        const auto hit = w.raycast({0.0f, 5.0f, 0.0f}, down, 10.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->t == Approx(5.0f));
        CHECK(near(hit->normal, {0.0f, 1.0f, 0.0f}));
        CHECK(hit->block == &w.blocks()[0]);
    }
    SECTION("max distance clamps the query") {
        CHECK_FALSE(w.raycast({0.0f, 5.0f, 0.0f}, down, 4.0f).has_value());
        CHECK(w.raycast({0.0f, 5.0f, 0.0f}, down, 5.5f).has_value());
    }
    SECTION("rays pointing away or parallel to the floor miss") {
        CHECK_FALSE(w.raycast({0.0f, 5.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f).has_value());
        CHECK_FALSE(w.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f).has_value());
    }
    SECTION("walls report the face normal on either side") {
        w.add(solid(5.0f, -5.0f, 6.0f, 5.0f, 3.0f));
        const auto west = w.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f);
        REQUIRE(west.has_value());
        CHECK(west->t == Approx(5.0f));
        CHECK(near(west->normal, {-1.0f, 0.0f, 0.0f}));

        const auto east = w.raycast({10.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, 100.0f);
        REQUIRE(east.has_value());
        CHECK(east->t == Approx(4.0f));
        CHECK(near(east->normal, {1.0f, 0.0f, 0.0f}));
    }
    SECTION("the nearest of several blocks wins") {
        w.add(solid(10.0f, -5.0f, 11.0f, 5.0f, 3.0f));  // farther, added first
        w.add(solid(5.0f, -5.0f, 6.0f, 5.0f, 3.0f));
        const auto hit = w.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->t == Approx(5.0f));
        CHECK(hit->block == &w.blocks()[2]);
    }
    SECTION("rays starting inside a block report no hit") {
        w.add(solid(5.0f, -5.0f, 6.0f, 5.0f, 3.0f));
        CHECK_FALSE(w.raycast({5.5f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f).has_value());
    }
    SECTION("sloped surfaces report the slope normal") {
        w.add(slope(10.0f, 0.0f, 14.0f, 8.0f, 1, 0.0f, 2.0f));
        const auto hit = w.raycast({12.0f, 10.0f, 4.0f}, down, 50.0f);
        REQUIRE(hit.has_value());
        CHECK(hit->t == Approx(10.0f - 1.0f));  // ramp is 0.25 m/m, so 1 m high at z = 4
        CHECK(near(hit->normal, w.blocks()[1].normal()));
    }
}

TEST_CASE("Doors block rays, ground and walls until opened", "[world][doors]") {
    World w = floorWorld();
    Block door = solid(-3.0f, -1.0f, 3.0f, 0.0f, 4.8f);
    door.door  = 0;
    w.add(door);

    const glm::vec3 o{0.0f, 1.0f, 5.0f}, d{0.0f, 0.0f, -1.0f};

    const auto closed = w.raycast(o, d, 20.0f);
    REQUIRE(closed.has_value());
    CHECK(closed->t == Approx(5.0f));
    CHECK(closed->block->door == 0);
    CHECK(w.ground({0.0f, -0.5f}, 10.0f)->height == Approx(4.8f));

    w.setDoorOpen(0, true);
    CHECK_FALSE(w.raycast(o, d, 20.0f).has_value());
    CHECK(w.ground({0.0f, -0.5f}, 10.0f)->height == Approx(0.0f));
    glm::vec2 p{0.0f, 0.2f}, vel{0.0f, -3.0f};
    CHECK_FALSE(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));

    w.setDoorOpen(1, true);  // unknown id: harmless
    w.resetDoors();
    CHECK(w.raycast(o, d, 20.0f).has_value());
}

TEST_CASE("World ground and ceiling queries", "[world]") {
    World w = floorWorld();
    w.add(solid(0.0f, 0.0f, 4.0f, 4.0f, 3.0f));                  // raised platform
    w.add(solid(10.0f, 10.0f, 14.0f, 14.0f, 5.0f, 3.0f));        // overhead slab: underside at 3

    SECTION("ground picks the highest surface under the limit") {
        CHECK(w.ground({2.0f, 2.0f}, 10.0f)->height == Approx(3.0f));
        CHECK(w.ground({2.0f, 2.0f}, 0.5f)->height == Approx(0.0f));  // platform is above the limit
        CHECK(near(w.ground({2.0f, 2.0f}, 10.0f)->normal, {0.0f, 1.0f, 0.0f}));
        CHECK_FALSE(w.ground({60.0f, 0.0f}, 10.0f).has_value());     // off the map
    }
    SECTION("ceiling finds the lowest underside in the head window") {
        CHECK(w.ceiling({12.0f, 12.0f}, 0.0f, 3.5f).value() == Approx(3.0f));
        CHECK_FALSE(w.ceiling({12.0f, 12.0f}, 0.0f, 2.5f).has_value());  // too high to matter
        CHECK_FALSE(w.ceiling({12.0f, 12.0f}, 2.8f, 4.0f).has_value());  // too close to the feet
        CHECK_FALSE(w.ceiling({2.0f, 2.0f}, 0.0f, 10.0f).has_value());   // floors are not ceilings
    }
}

TEST_CASE("probeWall finds tall walls within reach only", "[world]") {
    World w = floorWorld();
    w.add(solid(1.0f, -5.0f, 2.0f, 5.0f, 4.0f));                  // tall wall
    w.add(solid(10.0f, -5.0f, 11.0f, 5.0f, 1.0f));                // low wall
    w.add(solid(20.0f, -5.0f, 21.0f, 5.0f, 6.0f, 2.0f));          // overhead slab

    const auto hit = w.probeWall({0.8f, 0.0f}, 0.0f, 0.35f, 2.5f);
    REQUIRE(hit.has_value());
    CHECK(hit->normal.x == Approx(-1.0f));
    CHECK(hit->normal.y == Approx(0.0f).margin(1e-6));
    CHECK(hit->distance == Approx(0.2f));

    CHECK_FALSE(w.probeWall({0.5f, 0.0f}, 0.0f, 0.35f, 2.5f).has_value());   // out of reach
    CHECK_FALSE(w.probeWall({9.8f, 0.0f}, 0.0f, 0.35f, 2.5f).has_value());   // too short to run on
    CHECK(w.probeWall({9.8f, 0.0f}, 0.0f, 0.35f, 0.5f).has_value());         // ...unless the bar is lower
    CHECK_FALSE(w.probeWall({19.8f, 0.0f}, 0.0f, 0.35f, 2.5f).has_value());  // overhead, not a wall
}

TEST_CASE("resolveWalls pushes the circle out and cancels inward velocity", "[world]") {
    World w = floorWorld();
    w.add(solid(1.0f, -5.0f, 2.0f, 5.0f, 3.0f));

    SECTION("a circle overlapping a wall is pushed out") {
        glm::vec2 p{0.8f, 0.0f}, vel{3.0f, 2.0f};
        CHECK(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
        CHECK(p.x == Approx(0.6f));          // wall face at 1.0 minus the radius
        CHECK(p.y == Approx(0.0f));
        CHECK(vel.x == Approx(0.0f).margin(1e-6));  // inward component removed
        CHECK(vel.y == Approx(2.0f));               // sliding component kept
    }
    SECTION("velocity away from the wall is untouched") {
        glm::vec2 p{0.8f, 0.0f}, vel{-3.0f, 0.0f};
        CHECK(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
        CHECK(vel.x == Approx(-3.0f));
    }
    SECTION("no contact leaves everything alone") {
        glm::vec2 p{0.0f, 0.0f}, vel{3.0f, 0.0f};
        CHECK_FALSE(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
        CHECK(p == glm::vec2(0.0f, 0.0f));
        CHECK(vel == glm::vec2(3.0f, 0.0f));
    }
    SECTION("a centre inside the block exits through the nearest side") {
        glm::vec2 p{1.2f, 0.0f}, vel{0.0f, 0.0f};
        CHECK(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
        CHECK(p.x == Approx(0.6f));
    }
    SECTION("steps within stepHeight and overhead slabs are not walls") {
        w.add(solid(3.0f, -5.0f, 4.0f, 5.0f, 0.3f));                // walkable step
        w.add(solid(5.0f, -5.0f, 6.0f, 5.0f, 6.0f, 2.0f));          // overhead slab
        glm::vec2 step{2.8f, 0.0f}, over{4.8f, 0.0f}, vel{1.0f, 0.0f};
        CHECK_FALSE(w.resolveWalls(step, 0.0f, 0.4f, 0.4f, vel));
        CHECK_FALSE(w.resolveWalls(over, 0.0f, 0.4f, 0.4f, vel));
    }
}
