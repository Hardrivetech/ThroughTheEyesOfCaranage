#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/Material.hpp"
#include "game/Camera.hpp"
#include "game/Level.hpp"
#include "game/LevelAudit.hpp"
#include "game/LevelGraph.hpp"
#include "test_helpers.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

namespace {

const std::string kAssets = CARNAGE_ASSET_DIR;

LevelLoad loadShipped(const std::string& relative) { return loadLevelFile(kAssets + "/" + relative); }

std::string describeBlock(const Block& b) {
    std::ostringstream o;
    o << "block min=" << b.min.x << "," << b.min.y << " max=" << b.max.x << "," << b.max.y << " top=" << b.topHeight()
      << " mat=" << materialName(b.material);
    return o.str();
}

Block withMaterial(Block b, Material m) {
    b.material = m;
    return b;
}

} // namespace

// ---------------------------------------------------------------------------
// Coplanar tops (z-fighting)
// ---------------------------------------------------------------------------

TEST_CASE("A road laid flush on a floor is reported as coplanar", "[audit][coplanar]") {
    const std::vector<Block> level = {solid(-50, -50, 50, 50, 0.0f), solid(-5, -50, 5, 50, 0.0f)};
    const auto hits = findCoplanarTops(level);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].a == 0);
    CHECK(hits[0].b == 1);
    CHECK(hits[0].min == glm::vec2(-5.0f, -50.0f));
    CHECK(hits[0].max == glm::vec2(5.0f, 50.0f));
    CHECK(hits[0].height == Approx(0.0f));
}

TEST_CASE("Lifting the overlay a couple of centimetres clears the report", "[audit][coplanar]") {
    for (const float lift : {0.015f, 0.02f, 0.05f}) {
        INFO("lift " << lift);
        const std::vector<Block> level = {solid(-50, -50, 50, 50, 0.0f), solid(-5, -50, 5, 50, lift)};
        CHECK(findCoplanarTops(level).empty());
    }
    // Below the tolerance it is still reported, so a lift too small to matter is not accepted.
    const std::vector<Block> tiny = {solid(-50, -50, 50, 50, 0.0f), solid(-5, -50, 5, 50, 0.002f)};
    CHECK(findCoplanarTops(tiny).size() == 1);
}

TEST_CASE("Blocks that only touch, or sit at different heights, are not reported", "[audit][coplanar]") {
    CHECK(findCoplanarTops({solid(0, 0, 5, 5, 1.0f), solid(5, 0, 10, 5, 1.0f)}).empty());      // share an edge
    CHECK(findCoplanarTops({solid(0, 0, 5, 5, 1.0f), solid(5, 5, 10, 10, 1.0f)}).empty());     // share a corner
    CHECK(findCoplanarTops({solid(0, 0, 5, 5, 1.0f), solid(20, 0, 25, 5, 1.0f)}).empty());     // apart
    CHECK(findCoplanarTops({solid(0, 0, 5, 5, 1.0f), solid(2, 2, 4, 4, 3.0f)}).empty());       // a step
    CHECK(findCoplanarTops({solid(0, 0, 5, 5, 1.0f), solid(4.995f, 0, 10, 5, 1.0f)}).empty()); // sliver under 1 cm
}

TEST_CASE("Sloped tops in the same plane are reported, crossing planes are not", "[audit][coplanar]") {
    // Two ramps rising the same way at the same rate, overlapping.
    CHECK(findCoplanarTops({slope(0, 0, 10, 4, 0, 0.0f, 5.0f), slope(0, 0, 10, 4, 0, 0.0f, 5.0f)}).size() == 1);
    // A ramp laid over a flat floor whose heights only meet along a line: not coplanar over an area.
    CHECK(findCoplanarTops({solid(0, 0, 10, 4, 0.0f), slope(0, 0, 10, 4, 0, 0.0f, 5.0f)}).empty());
    // A ramp that crosses a flat top exactly at the middle of the overlap: equal there, different everywhere else.
    CHECK(findCoplanarTops({solid(0, 0, 10, 4, 1.0f), slope(0, 0, 10, 4, 0, 0.0f, 2.0f)}).empty());
    // The same ramp with a different slope.
    CHECK(findCoplanarTops({slope(0, 0, 10, 4, 0, 0.0f, 5.0f), slope(0, 0, 10, 4, 0, 0.0f, 6.0f)}).empty());
}

TEST_CASE("Invisible blocks are ignored, but doors are drawn and count", "[audit][coplanar]") {
    Block hidden = solid(-5, -50, 5, 50, 0.0f);
    hidden.visible = false;
    CHECK(findCoplanarTops({solid(-50, -50, 50, 50, 0.0f), hidden}).empty());

    Block door = hidden;
    door.door = 0;
    CHECK(findCoplanarTops({solid(-50, -50, 50, 50, 0.0f), door}).size() == 1);
}

TEST_CASE("A shared top that is buried under a third block is not a visible problem", "[audit][coplanar]") {
    // Two wall tops that overlap at a corner, both under a roof slab that sits on them.
    Block roof = solid(0, 0, 10, 10, 7.0f, 6.0f);
    const std::vector<Block> walls = {solid(0, 0, 10, 1, 6.0f, 0.0f), solid(0, 0, 1, 10, 6.0f, 0.0f)};
    CHECK(findCoplanarTops(walls).size() == 1);   // without the roof it is a real overlap
    std::vector<Block> covered = walls;
    covered.push_back(roof);
    CHECK(findCoplanarTops(covered).empty());     // with it, nobody can see the tops
}

TEST_CASE("The tolerance is a parameter", "[audit][coplanar]") {
    const std::vector<Block> level = {solid(-50, -50, 50, 50, 0.0f), solid(-5, -50, 5, 50, 0.03f)};
    CHECK(findCoplanarTops(level, 0.005f).empty());
    CHECK(findCoplanarTops(level, 0.05f).size() == 1);
}

// ---------------------------------------------------------------------------
// Snow on interior floors
// ---------------------------------------------------------------------------

TEST_CASE("Only the exterior materials accumulate snow", "[audit][snow]") {
    const std::set<Material> weathered = {Material::Concrete, Material::PaintedMetal, Material::ConcreteWorn,
                                          Material::MetalRusted, Material::AsphaltWet};
    for (const MaterialName& m : kMaterialNames) {
        INFO(m.name);
        CHECK(accumulatesSnow(m.material) == (weathered.count(m.material) == 1));
    }
    // The interior and safety materials the levels use indoors must never accumulate it.
    for (const Material m : {Material::Tile, Material::GrateIndustrial, Material::HazardTrim, Material::SnowPacked,
                             Material::Steel, Material::Portal, Material::Emissive})
        CHECK_FALSE(accumulatesSnow(m));
}

TEST_CASE("scene.frag's isWeathered() lists exactly the materials accumulatesSnow() does", "[audit][snow][shader]") {
    std::ifstream f(kAssets + "/shaders/scene.frag");
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string src = ss.str();
    REQUIRE_FALSE(src.empty());

    const std::regex fn(R"(bool\s+isWeathered\s*\(\s*int\s+mat\s*\)\s*\{([^}]*)\})");
    std::smatch m;
    REQUIRE(std::regex_search(src, m, fn));
    const std::string body = m[1];

    std::set<std::string> inShader;
    const std::regex name(R"(M_([A-Z_]+))");
    for (std::sregex_iterator it(body.begin(), body.end(), name), end; it != end; ++it) inShader.insert((*it)[1]);

    std::set<std::string> inCpp;
    for (const MaterialName& e : kMaterialNames) {
        if (!accumulatesSnow(e.material)) continue;
        std::string key;
        for (const char c : e.name) key += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (key == "PAINTED_METAL") key = "PAINTED";
        inCpp.insert(key);
    }
    CHECK(inShader == inCpp);
}

TEST_CASE("A weathered floor under a roof is reported as a snowy interior", "[audit][snow]") {
    const std::vector<Block> level = {
        withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
        solid(0, 0, 10, 10, 7.0f, 6.0f),           // the roof, 6 m up
    };
    const auto hits = findSnowyInteriors(level);
    REQUIRE_FALSE(hits.empty());
    for (const SnowyInterior& h : hits) {
        CHECK(h.block == 0);
        CHECK(h.roofBase == Approx(6.0f));
    }
}

TEST_CASE("Open ground, interior-safe materials, buried floors and high overhangs are fine", "[audit][snow]") {
    const Block roof = solid(0, 0, 10, 10, 7.0f, 6.0f);

    SECTION("open ground") {
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::AsphaltWet)}).empty());
    }
    SECTION("tile, grating and hazard trim under a roof") {
        for (const Material m : {Material::Tile, Material::GrateIndustrial, Material::HazardTrim})
            CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), m), roof}).empty());
    }
    SECTION("a platform 26 m up is not a roof") {
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::MetalRusted),
                                  solid(0, 0, 10, 10, 26.6f, 26.0f)}).empty());
    }
    SECTION("a floor buried under another block is not a floor") {
        // The weathered slab is covered by a taller foundation, so nobody stands on it.
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
                                  withMaterial(solid(0, 0, 10, 10, 0.8f, 0.0f), Material::Tile), roof}).empty());
    }
    SECTION("a roof only over part of the floor reports only that part") {
        const auto hits = findSnowyInteriors({withMaterial(solid(0, 0, 20, 10, 0.0f), Material::ConcreteWorn),
                                              solid(0, 0, 10, 10, 7.0f, 6.0f)});
        REQUIRE_FALSE(hits.empty());
        for (const SnowyInterior& h : hits) CHECK(h.at.x <= 10.0f);
    }
    SECTION("a gate lintel or beam is not a roof") {
        // 1 m wide over the floor, 4.2 m up: a gateway, not a room.
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
                                  solid(0, 2, 1, 8, 4.8f, 4.2f)}).empty());
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
                                  solid(0, 2, 6, 3, 4.8f, 4.2f)}).empty());
        // The same slab made 2 m wide is a roof.
        CHECK_FALSE(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
                                        solid(0, 2, 2, 8, 4.8f, 4.2f)}).empty());
    }
    SECTION("a low ceiling under 1 m is a step, not a room") {
        CHECK(findSnowyInteriors({withMaterial(solid(0, 0, 10, 10, 0.0f), Material::ConcreteWorn),
                                  solid(0, 0, 10, 10, 1.5f, 0.5f)}).empty());
    }
}

TEST_CASE("A door counts as solid when deciding what is an interior floor", "[audit][snow]") {
    // A weathered doorway sill, with a lintel above and the closed door filling the opening.
    Block door = solid(0, 0, 4, 1, 4.2f, 0.0f);
    door.visible = false;
    door.door    = 0;
    const std::vector<Block> level = {withMaterial(solid(0, 0, 4, 1, 0.0f), Material::ConcreteWorn), door,
                                      solid(0, 0, 4, 1, 6.0f, 4.2f)};
    CHECK(findSnowyInteriors(level).empty());   // the floor under the door is hidden by it
}

// ---------------------------------------------------------------------------
// The shipped levels
// ---------------------------------------------------------------------------

TEST_CASE("The shipped Kodiak levels have no coplanar floor tops", "[audit][coplanar][level]") {
    for (const char* file : {"levels/kodiak_hub.lvl", "levels/kodiak_island.lvl", "levels/kodiak_range.lvl",
                             "levels/pois/docks.lvl", "levels/pois/relay_tower.lvl"}) {
        INFO(file);
        const LevelLoad r = loadShipped(file);
        REQUIRE(r.opened);
        const auto blocks = r.level.world.blocks();
        REQUIRE_FALSE(blocks.empty());
        for (const CoplanarTops& c : findCoplanarTops(blocks))
            FAIL_CHECK(std::string(file) + ": [" + describeBlock(blocks[c.a]) + "] and [" + describeBlock(blocks[c.b]) +
                       "] share a top at y=" + std::to_string(c.height));
    }
}

TEST_CASE("Level 01 has no coplanar floor tops either", "[audit][coplanar][level]") {
    const LevelLoad r = loadShipped("levels/level01_escape.lvl");
    REQUIRE(r.opened);
    const auto blocks = r.level.world.blocks();
    for (const CoplanarTops& c : findCoplanarTops(blocks))
        FAIL_CHECK("[" + describeBlock(blocks[c.a]) + "] and [" + describeBlock(blocks[c.b]) + "] share a top at y=" +
                   std::to_string(c.height));
}

TEST_CASE("No interior floor in a snowy level uses a snow-accumulating material", "[audit][snow][level]") {
    for (const char* id : {"kodiak_hub", "kodiak_island"}) {
        INFO(id);
        const LevelLoad r = loadShipped(std::string("levels/") + id + ".lvl");
        REQUIRE(r.clean());
        REQUIRE(r.level.environment.snow > 0.0f);   // the check only matters where it snows
        const auto blocks = r.level.world.blocks();
        std::set<std::size_t> reported;
        for (const SnowyInterior& s : findSnowyInteriors(blocks))
            if (reported.insert(s.block).second)
                FAIL_CHECK(std::string(id) + ": [" + describeBlock(blocks[s.block]) + "] is an interior floor (roof at y=" +
                           std::to_string(s.roofBase) + " over " + std::to_string(s.at.x) + "," + std::to_string(s.at.y) + ")");
    }
}

TEST_CASE("The interiors that were carved out are real rooms with real floors", "[audit][level]") {
    // The hub's command post and the docks warehouse have a roof; their floors must be a safe material.
    const auto hub = loadShipped("levels/kodiak_hub.lvl");
    REQUIRE(hub.clean());
    const auto island = loadShipped("levels/kodiak_island.lvl");
    REQUIRE(island.clean());

    const auto floorMaterialAt = [](const LevelData& level, glm::vec2 p) {
        // The highest surface you could stand on at p: below head height, so the roof does not count.
        float    best = -1e9f;
        Material mat  = Material::Concrete;
        for (const Block& b : level.world.blocks()) {
            if (!b.visible || !b.contains(p) || b.surface(p) > 2.5f) continue;
            if (b.surface(p) > best) { best = b.surface(p); mat = b.material; }
        }
        return mat;
    };
    CHECK_FALSE(accumulatesSnow(floorMaterialAt(hub.level, {0.0f, -45.0f})));       // inside the command post
    CHECK(accumulatesSnow(floorMaterialAt(hub.level, {0.0f, 10.0f})));             // the open plaza still weathers
    // The docks warehouse is a POI turned and offset into the island, so find it by its roof: a wide
    // slab 5-8 m up east of x = 300 (the docks are on the east coast).
    bool foundInterior = false;
    for (const Block& roof : island.level.world.blocks()) {
        if (roof.visible && roof.base > 5.0f && roof.base < 8.0f && roof.max.x - roof.min.x > 10.0f && roof.min.x > 300.0f) {
            const glm::vec2 centre = (roof.min + roof.max) * 0.5f;
            CHECK_FALSE(accumulatesSnow(floorMaterialAt(island.level, centre)));
            foundInterior = true;
        }
    }
    CHECK(foundInterior);
}

TEST_CASE("The Kodiak level network passes the link check from every entrance", "[audit][graph]") {
    for (const char* start : {"kodiak_hub", "kodiak_island", "kodiak_range", "level01_escape"}) {
        INFO(start);
        const LevelGraphReport r = checkLevelGraph(kAssets, start);
        for (const std::string& p : r.problems) FAIL_CHECK(p);
        CHECK(r.ok());
        CHECK(r.levels.size() == 4);
    }
}

// ---------------------------------------------------------------------------
// Reverse-Z depth
// ---------------------------------------------------------------------------

namespace {

// Depth the GPU would store for a point `z` metres in front of the camera, in float arithmetic.
// `zeroToOne` is the clip convention (the old projection maps NDC -1..1 to depth 0..1 itself).
float storedDepth(const glm::mat4& proj, float z, bool zeroToOne) {
    const glm::vec4 clip = proj * glm::vec4(0.0f, 0.0f, -z, 1.0f);
    const float ndc = clip.z / clip.w;
    return zeroToOne ? ndc : ndc * 0.5f + 0.5f;
}

// The smallest gap in front of `z` (in 1 mm steps up to 4 m) that stores a different depth.
float depthResolution(const glm::mat4& proj, float z, bool zeroToOne) {
    const float d = storedDepth(proj, z, zeroToOne);
    for (float gap = 0.001f; gap < 4.0f; gap += 0.001f)
        if (storedDepth(proj, z + gap, zeroToOne) != d) return gap;
    return 4.0f;
}

} // namespace

TEST_CASE("The reverse-Z projection stores 1 at the near plane and 0 at the far plane", "[depth]") {
    const float n = 0.05f, f = 1500.0f;
    const glm::mat4 p = perspectiveReverseZ(glm::radians(90.0f), 16.0f / 9.0f, n, f);
    CHECK(storedDepth(p, n, true) == Approx(1.0f).margin(1e-4));
    CHECK(storedDepth(p, f, true) == Approx(0.0f).margin(1e-5));
    CHECK(storedDepth(p, 1.0f, true) > storedDepth(p, 10.0f, true));        // nearer is larger (GL_GREATER wins)
    CHECK(storedDepth(p, 10.0f, true) > storedDepth(p, 1000.0f, true));
    CHECK(storedDepth(p, 1000.0f, true) > storedDepth(p, 1400.0f, true));
    float last = 2.0f;
    for (float z = n; z <= f; z *= 1.07f) {   // strictly monotonic over the whole range
        const float d = storedDepth(p, z, true);
        CHECK(d < last);
        CHECK(d >= 0.0f);
        CHECK(d <= 1.0001f);
        last = d;
    }
}

TEST_CASE("The reverse-Z projection keeps the same view of the world as the old one", "[depth]") {
    const float fov = glm::radians(90.0f), aspect = 16.0f / 9.0f, n = 0.05f, f = 1500.0f;
    const glm::mat4 oldP = glm::perspective(fov, aspect, n, f);
    const glm::mat4 newP = perspectiveReverseZ(fov, aspect, n, f);
    // x, y and w are identical: only the depth row differs, so nothing moves on screen.
    for (int col = 0; col < 4; ++col) {
        CHECK(newP[col][0] == Approx(oldP[col][0]));
        CHECK(newP[col][1] == Approx(oldP[col][1]));
        CHECK(newP[col][3] == Approx(oldP[col][3]));
    }
    for (const glm::vec3 p : {glm::vec3(3, 2, -10), glm::vec3(-40, 7, -300), glm::vec3(0, 0, -0.5f)}) {
        const glm::vec4 a = oldP * glm::vec4(p, 1.0f), b = newP * glm::vec4(p, 1.0f);
        CHECK(a.x / a.w == Approx(b.x / b.w));
        CHECK(a.y / a.w == Approx(b.y / b.w));
    }
}

TEST_CASE("At 1 km reverse-Z resolves surfaces centimetres apart; the old depth could not", "[depth]") {
    const float fov = glm::radians(90.0f), aspect = 16.0f / 9.0f, n = 0.05f, f = 1500.0f;
    const glm::mat4 oldP = glm::perspective(fov, aspect, n, f);
    const glm::mat4 newP = perspectiveReverseZ(fov, aspect, n, f);

    const float oldRes = depthResolution(oldP, 1000.0f, false);
    const float newRes = depthResolution(newP, 1000.0f, true);
    INFO("old resolution at 1 km: " << oldRes << " m, reverse-Z: " << newRes << " m");
    CHECK(oldRes > 0.3f);                // the old buffer merges anything within a third of a metre or more
    CHECK(newRes < 0.05f);               // reverse-Z separates a 5 cm gap
    CHECK(newRes * 10.0f < oldRes);

    // And it holds across the island, not just at one distance.
    for (const float z : {200.0f, 500.0f, 800.0f, 1200.0f}) {
        INFO("z " << z);
        CHECK(depthResolution(newP, z, true) < 0.1f);
    }
}

TEST_CASE("Camera::projection is the reverse-Z projection and follows the far plane", "[depth][camera]") {
    Camera cam;
    cam.fovDegrees = 90.0f;
    cam.farPlane   = 1500.0f;
    const glm::mat4 p = cam.projection(16.0f / 9.0f);
    const glm::mat4 q = perspectiveReverseZ(glm::radians(90.0f), 16.0f / 9.0f, 0.05f, 1500.0f);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) CHECK(p[c][r] == Approx(q[c][r]));

    cam.farPlane = 300.0f;
    CHECK(storedDepth(cam.projection(1.0f), 300.0f, true) == Approx(0.0f).margin(1e-5));
    CHECK(storedDepth(cam.projection(1.0f), 0.05f, true) == Approx(1.0f).margin(1e-4));
}
