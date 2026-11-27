#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "core/Environment.hpp"
#include "core/Shadow.hpp"
#include "game/Enemy.hpp"
#include "game/Level.hpp"
#include "game/LevelGraph.hpp"
#include "game/Movement.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

namespace {

// An in-memory file system for includes.
struct Files {
    std::map<std::string, std::string> files;
    IncludeReader reader() const {
        return [this](std::string_view path) -> std::optional<std::string> {
            const auto it = files.find(std::string(path));
            if (it == files.end()) return std::nullopt;
            return it->second;
        };
    }
    LevelLoad load(const std::string& top, const std::string& self = {}) const { return parseLevel(top, reader(), self); }
};

const char* kBase = "level name=t\nspawn pos=0,0,0\nblock min=-1,-1 max=1,1 top=1\n";

bool mentions(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
bool anyIssue(const LevelLoad& r, const std::string& needle) {
    return std::any_of(r.issues.begin(), r.issues.end(), [&](const rec::Issue& i) { return mentions(i.message, needle); });
}

// A POI with one of everything, off-centre so that turning it really moves things.
const char* kPoi =
    "block min=2,3 max=6,5 top=4 material=steel\n"
    "block min=10,0 max=14,6 h0=0 h1=3 axis=x\n"           // a ramp rising towards +X
    "block min=0,10 max=2,16 h0=1 h1=5 axis=z base=0.5\n"  // a ramp rising towards +Z, with an underside
    "block min=-3,-3 max=-1,-1 top=4.8 visible=0 door=0\n"
    "light pos=1,2,3 radius=9\n"
    "target pos=4,0,2 patrol=2\n"
    "enemy pos=5,0,7 type=ranged sector=0\n"
    "enemy pos=6,0,8 type=melee\n"
    "pickup kind=bat pos=3,0.06,9 yaw=0.5\n"
    "spawn name=gate pos=8,0,2 yaw=90\n"
    "trigger when=enter min=1,-1,1 max=4,3,6 do=checkpoint point=2,0,2\n"
    "trigger when=sector_cleared sector=0 do=open_door door=0\n"
    "trigger when=enter min=5,-1,5 max=7,3,7 do=portal level=other spawn=in\n";

} // namespace

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

TEST_CASE("Placement turns points in quarter turns with the engine's yaw convention", "[include][placement]") {
    Placement p;
    CHECK(p.turn({3.0f, 4.0f}) == glm::vec2(3.0f, 4.0f));

    p.turns = 1;   // +90: what faced -Z now faces -X
    CHECK(p.turn({0.0f, -1.0f}) == glm::vec2(-1.0f, 0.0f));
    CHECK(p.turn({1.0f, 0.0f}) == glm::vec2(0.0f, -1.0f));
    p.turns = 2;
    CHECK(p.turn({3.0f, 4.0f}) == glm::vec2(-3.0f, -4.0f));
    p.turns = 3;
    CHECK(p.turn({0.0f, -1.0f}) == glm::vec2(1.0f, 0.0f));

    for (const int t : {-9, -5, -4, -1, 0, 3, 4, 7, 100}) {
        Placement q;
        q.turns = t;
        CHECK(q.quarter() >= 0);
        CHECK(q.quarter() <= 3);
        CHECK(q.quarter() == ((t % 4) + 4) % 4);
    }
}

TEST_CASE("Placement and its inverse round-trip exactly", "[include][placement]") {
    for (int turns = 0; turns < 4; ++turns) {
        Placement p;
        p.turns  = turns;
        p.offset = {120.0f, 3.0f, -80.5f};
        for (const glm::vec3 v : {glm::vec3(0, 0, 0), glm::vec3(1, 2, 3), glm::vec3(-7.25f, 1, 9), glm::vec3(1000, -5, -1000)}) {
            CHECK(p.unplace(p.point(v)) == v);
            CHECK(p.point(p.unplace(v)) == v);
        }
        // Four quarter turns are the identity.
        Placement full = p;
        full.turns = 4;
        full.offset = {};
        CHECK(full.point({3, 4, 5}) == glm::vec3(3, 4, 5));
    }
}

TEST_CASE("Placement composes the way nesting does", "[include][placement]") {
    for (int outer = 0; outer < 4; ++outer) {
        for (int inner = 0; inner < 4; ++inner) {
            Placement a, b;
            a.turns = outer; a.offset = {10, 1, -20};
            b.turns = inner; b.offset = {3, 0.5f, 7};
            const Placement ab = a.compose(b);
            for (const glm::vec3 v : {glm::vec3(0, 0, 0), glm::vec3(2, 1, -3), glm::vec3(-5, 0, 4)})
                CHECK(ab.point(v) == a.point(b.point(v)));   // exact
        }
    }
}

TEST_CASE("A placed block gives the same answers as the original, seen through the placement", "[include][placement][property]") {
    // The strongest check available: for every quarter turn and offset, build the original block and
    // the placed block as Worlds, then compare real queries (height under a point, a ray) transformed
    // through the placement. Covers flat boxes and ramps along both axes, rising either way.
    const Block cases[] = {
        [] { Block b = solid(2, 3, 6, 5, 4.0f); return b; }(),
        [] { Block b = slope(10, 0, 14, 6, 0, 0.0f, 3.0f); return b; }(),     // rises towards +X
        [] { Block b = slope(10, 0, 14, 6, 0, 3.0f, 0.0f); return b; }(),     // rises towards -X
        [] { Block b = slope(0, 10, 2, 16, 1, 1.0f, 5.0f); b.base = 0.5f; return b; }(),
        [] { Block b = slope(-4, -9, 3, -2, 1, 6.0f, 2.0f); return b; }(),
    };
    for (const Block& orig : cases) {
        for (int turns = 0; turns < 4; ++turns) {
            Placement p;
            p.turns  = turns;
            p.offset = {-120.0f, 2.0f, 75.0f};

            World a, b;
            a.add(orig);
            b.add(p.block(orig));

            int checked = 0;
            for (float x = orig.min.x - 1.5f; x <= orig.max.x + 1.5f; x += 0.5f) {
                for (float z = orig.min.y - 1.5f; z <= orig.max.y + 1.5f; z += 0.5f) {
                    const auto ga = a.ground({x, z}, 100.0f);
                    const glm::vec3 q = p.point({x, 0.0f, z});
                    const auto gb = b.ground({q.x, q.z}, 100.0f);
                    REQUIRE(ga.has_value() == gb.has_value());
                    if (!ga) continue;
                    REQUIRE(gb->height == Approx(ga->height + p.offset.y).margin(1e-5));
                    const glm::vec3 nTurned = p.direction(ga->normal);
                    REQUIRE(gb->normal.x == Approx(nTurned.x).margin(1e-5));
                    REQUIRE(gb->normal.y == Approx(nTurned.y).margin(1e-5));
                    REQUIRE(gb->normal.z == Approx(nTurned.z).margin(1e-5));
                    ++checked;
                }
            }
            REQUIRE(checked > 20);

            // Rays through the piece hit at the same distance, with the turned normal.
            for (float h : {0.3f, 0.8f}) {
                for (const glm::vec3 d : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}) {
                    const glm::vec3 centre{(orig.min.x + orig.max.x) * 0.5f, 0.0f, (orig.min.y + orig.max.y) * 0.5f};
                    const glm::vec3 o = centre - d * 20.0f + glm::vec3(0.0f, h + 2.0f, 0.0f);
                    const auto ha = a.raycast(o, d, 100.0f);
                    const auto hb = b.raycast(p.point(o), p.direction(d), 100.0f);
                    REQUIRE(ha.has_value() == hb.has_value());
                    if (ha) {
                        REQUIRE(hb->t == Approx(ha->t).margin(1e-4));
                        const glm::vec3 n = p.direction(ha->normal);
                        REQUIRE(glm::length(hb->normal - n) < 1e-5f);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// include: merging
// ---------------------------------------------------------------------------

TEST_CASE("include moves everything in a POI by the offset", "[include]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] = kPoi;
    const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/p.lvl offset=100,5,-50 prefix=a sector_offset=7 door_offset=3\n");
    for (const rec::Issue& i : r.issues) FAIL(i.file << ":" << i.line << ": " << i.message);
    const LevelData& L = r.level;

    REQUIRE(L.world.blocks().size() == 1 + 4);
    const Block& steel = L.world.blocks()[1];
    CHECK(steel.min == glm::vec2(102.0f, -47.0f));
    CHECK(steel.max == glm::vec2(106.0f, -45.0f));
    CHECK(steel.h0 == Approx(9.0f));                 // 4 + offset.y 5
    CHECK(steel.material == Material::Steel);
    CHECK(L.world.blocks()[4].door == 3);            // door 0 + door_offset
    CHECK(L.world.blocks()[3].base == Approx(5.5f)); // 0.5 + 5

    REQUIRE(L.world.lights().size() == 1);
    CHECK(near(L.world.lights()[0].pos, {101.0f, 7.0f, -47.0f}));
    CHECK(L.world.lights()[0].radius == 9.0f);

    REQUIRE(L.targets.size() == 1);
    CHECK(near(L.targets[0].pos, {104.0f, 5.0f, -48.0f}));

    REQUIRE(L.enemies.size() == 2);
    CHECK(near(L.enemies[0].pos, {105.0f, 5.0f, -43.0f}));
    CHECK(L.enemies[0].sector == 7);                 // sector 0 + sector_offset
    CHECK(L.enemies[1].sector == -1);                // free roaming stays free roaming

    REQUIRE(L.pickups.size() == 1);
    CHECK(near(L.pickups[0].pos, {103.0f, 5.06f, -41.0f}));
    CHECK(L.pickups[0].yaw == Approx(0.5f));

    REQUIRE(L.spawns.size() == 1);
    CHECK(L.spawns[0].name == "a_gate");             // prefixed
    CHECK(near(L.spawns[0].point.pos, {108.0f, 5.0f, -48.0f}));
    CHECK(L.spawns[0].point.yaw == Approx(glm::radians(90.0f)));

    REQUIRE(L.triggers.size() == 3);
    CHECK(near(L.triggers[0].min, {101.0f, 4.0f, -49.0f}));
    CHECK(near(L.triggers[0].max, {104.0f, 8.0f, -44.0f}));
    CHECK(near(L.triggers[0].point, {102.0f, 5.0f, -48.0f}));
    CHECK(L.triggers[1].sector == 7);
    CHECK(L.triggers[1].param == 3);                 // the door it opens, shifted too
    CHECK(L.triggers[2].target == "other");
    CHECK(L.triggers[2].targetSpawn == "in");        // names inside a portal are not rewritten

    CHECK(L.includedFiles == std::vector<std::string>{"levels/pois/p.lvl"});
    CHECK(L.world.hasDoor(3));
    CHECK_FALSE(L.world.hasDoor(0));
}

TEST_CASE("include with a quarter turn rotates everything consistently", "[include][rotation]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] = kPoi;
    const std::string top = std::string(kBase);

    // The piece placed with each turn must match the unrotated piece seen through the same placement.
    const LevelLoad plain = fs.load(top + "include path=levels/pois/p.lvl\n");
    REQUIRE(plain.level.world.blocks().size() == 5);

    for (const int yaw : {0, 90, 180, 270}) {
        INFO("yaw " << yaw);
        const LevelLoad r = fs.load(top + "include path=levels/pois/p.lvl offset=50,0,-30 yaw=" + std::to_string(yaw) + " door_offset=1\n");
        for (const rec::Issue& i : r.issues) FAIL(i.message);
        Placement p;
        p.turns  = yaw / 90;
        p.offset = {50.0f, 0.0f, -30.0f};

        const LevelData& a = plain.level;
        const LevelData& b = r.level;
        REQUIRE(a.world.blocks().size() == b.world.blocks().size());
        for (std::size_t i = 1; i < a.world.blocks().size(); ++i) {
            const Block expect = p.block(a.world.blocks()[i]);
            const Block& got   = b.world.blocks()[i];
            CHECK(got.min == expect.min);
            CHECK(got.max == expect.max);
            CHECK(got.h0 == expect.h0);
            CHECK(got.h1 == expect.h1);
            CHECK(got.axis == expect.axis);
        }
        CHECK(near(b.enemies[0].pos, p.point(a.enemies[0].pos)));
        CHECK(near(b.targets[0].pos, p.point(a.targets[0].pos)));
        CHECK(near(b.pickups[0].pos, p.point(a.pickups[0].pos)));
        CHECK(b.pickups[0].yaw == Approx(0.5f + glm::radians(static_cast<float>(yaw))));
        CHECK(b.spawns[0].point.yaw == Approx(glm::radians(90.0f + static_cast<float>(yaw))));
        CHECK(near(b.spawns[0].point.pos, p.point(a.spawns[0].point.pos)));
        glm::vec3 lo, hi;
        p.box(a.triggers[0].min, a.triggers[0].max, lo, hi);
        CHECK(near(b.triggers[0].min, lo));
        CHECK(near(b.triggers[0].max, hi));
        CHECK(near(b.triggers[0].point, p.point(a.triggers[0].point)));
        CHECK(b.targets[0].axis == p.direction(glm::vec3(1, 0, 0)));   // patrol direction turns too
        CHECK(b.triggers[0].min.x <= b.triggers[0].max.x);             // boxes stay well-formed
        CHECK(b.triggers[0].min.z <= b.triggers[0].max.z);
    }
}

TEST_CASE("A rotated and offset POI behaves like the original when walked through", "[include][rotation][property]") {
    // The end-to-end proof: walk a player through a doorway in a small building, once as written
    // and once placed with each turn, and require the same outcome.
    Files fs;
    fs.files["levels/pois/hut.lvl"] =
        "block min=-40,-40 max=40,40 top=0\n"                // ground, inside and well outside the hut
        "block min=-10,-10 max=-1,-9 top=4\nblock min=1,-10 max=10,-9 top=4\n"   // front wall with a 2 m doorway at x -1..1
        "block min=-10,9 max=10,10 top=4\nblock min=-10,-9 max=-9,9 top=4\nblock min=9,-9 max=10,9 top=4\n";

    for (const int yaw : {0, 90, 180, 270}) {
        INFO("yaw " << yaw);
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/hut.lvl offset=200,0,300 yaw=" + std::to_string(yaw) + "\n");
        REQUIRE(r.issues.empty());
        Placement p;
        p.turns  = yaw / 90;
        p.offset = {200.0f, 0.0f, 300.0f};

        const glm::vec3 start = p.point({0.0f, 0.0f, -20.0f});    // outside the doorway
        const glm::vec3 d = p.direction({0.0f, 0.0f, 1.0f});       // "into the hut"
        const float heading = std::atan2(-d.x, -d.z);               // engine yaw that walks along d

        MovementController mc(r.level.world);
        mc.respawn(start);
        MoveInput in;
        in.move = {0.0f, 1.0f};
        for (int i = 0; i < 360; ++i) mc.tick(kDt, heading, in);
        // Walked straight in through the doorway and on until the back wall (inner face at z = 9) stopped it.
        const glm::vec3 backWall = p.point({0.0f, 0.0f, 9.0f - 0.4f});
        CHECK(glm::length(mc.position() - backWall) < 0.5f);
        CHECK(mc.position().y == Approx(0.0f).margin(1e-3));

        // Aim at the wall instead (5 m from the doorway): the front wall's outer face (z = -10) must stop it.
        mc.respawn(p.point({5.0f, 0.0f, -20.0f}));
        for (int i = 0; i < 360; ++i) mc.tick(kDt, heading, in);
        const glm::vec3 stopped = p.point({5.0f, 0.0f, -10.0f - 0.4f});
        CHECK(glm::length(mc.position() - stopped) < 0.5f);
    }
}

TEST_CASE("include: two copies of one piece sit side by side", "[include]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] = kPoi;
    const LevelLoad r = fs.load(std::string(kBase) +
        "include path=levels/pois/p.lvl offset=100,0,0 prefix=east door_offset=10 sector_offset=10\n"
        "include path=levels/pois/p.lvl offset=-100,0,0 yaw=180 prefix=west door_offset=20 sector_offset=20\n");
    for (const rec::Issue& i : r.issues) FAIL(i.message);
    CHECK(r.level.world.blocks().size() == 1 + 4 + 4);
    CHECK(r.level.world.doorIds() == std::vector<int>{10, 20});
    CHECK(r.level.hasSpawn("east_gate"));
    CHECK(r.level.hasSpawn("west_gate"));
    CHECK(r.level.enemies.size() == 4);
    std::set<int> sectors;
    for (const EnemySpawn& e : r.level.enemies) if (e.sector >= 0) sectors.insert(e.sector);
    CHECK(sectors == std::set<int>{10, 20});
    CHECK(r.level.includedFiles.size() == 1);        // listed once however many copies
}

TEST_CASE("Nested includes compose their placements", "[include][nesting]") {
    Files fs;
    fs.files["levels/pois/inner.lvl"] = "block min=0,0 max=2,2 top=1\nenemy pos=1,0,1 type=melee\nspawn name=door pos=1,0,3 yaw=0\n";
    fs.files["levels/pois/outer.lvl"] = "block min=-5,-5 max=5,5 top=0\ninclude path=levels/pois/inner.lvl offset=10,0,0 yaw=90 prefix=in\n";
    const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/outer.lvl offset=100,0,100 yaw=90 prefix=out\n");
    for (const rec::Issue& i : r.issues) FAIL(i.message);

    // inner is turned 90 then moved by (10,0,0); outer is then turned 90 and moved by (100,0,100).
    Placement inner; inner.turns = 1; inner.offset = {10, 0, 0};
    Placement outer; outer.turns = 1; outer.offset = {100, 0, 100};
    const Placement both = outer.compose(inner);

    REQUIRE(r.level.enemies.size() == 1);
    CHECK(near(r.level.enemies[0].pos, both.point({1.0f, 0.0f, 1.0f})));
    CHECK(r.level.hasSpawn("out_in_door"));          // prefixes nest, outermost first
    const SpawnPoint sp = r.level.spawnPoint("out_in_door");
    CHECK(near(sp.pos, both.point({1.0f, 0.0f, 3.0f})));
    CHECK(sp.yaw == Approx(glm::radians(180.0f)));    // two quarter turns
    CHECK(r.level.includedFiles.size() == 2);
}

// ---------------------------------------------------------------------------
// include: errors
// ---------------------------------------------------------------------------

TEST_CASE("Include cycles are detected and reported with the files involved", "[include][cycle]") {
    Files fs;
    SECTION("a file including itself") {
        fs.files["levels/a.lvl"] = "block min=0,0 max=1,1 top=1\ninclude path=levels/a.lvl\n";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/a.lvl\n");
        REQUIRE(anyIssue(r, "include cycle: levels/a.lvl -> levels/a.lvl"));
        CHECK(r.level.world.blocks().size() == 2);    // base + a's own block, once: no runaway
    }
    SECTION("A -> B -> A") {
        fs.files["levels/a.lvl"] = "block min=0,0 max=1,1 top=1\ninclude path=levels/b.lvl\n";
        fs.files["levels/b.lvl"] = "block min=5,0 max=6,1 top=1\ninclude path=levels/a.lvl\n";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/a.lvl\n");
        CHECK(anyIssue(r, "include cycle: levels/a.lvl -> levels/b.lvl -> levels/a.lvl"));
        const auto it = std::find_if(r.issues.begin(), r.issues.end(), [](const rec::Issue& i) { return mentions(i.message, "cycle"); });
        REQUIRE(it != r.issues.end());
        CHECK(it->file == "levels/b.lvl");            // the include that closes the loop is in b.lvl
        CHECK(it->line == 2);
        CHECK(r.level.world.blocks().size() == 3);
    }
    SECTION("the top-level file including itself (found through selfPath)") {
        const std::string self = "levels/island.lvl";
        fs.files[self] = "";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/island.lvl\n", self);
        CHECK(anyIssue(r, "include cycle: levels/island.lvl -> levels/island.lvl"));
    }
    SECTION("the same file twice is NOT a cycle") {
        fs.files["levels/a.lvl"] = "block min=0,0 max=1,1 top=1\n";
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/a.lvl offset=10,0,0\ninclude path=levels/a.lvl offset=20,0,0\n");
        CHECK(r.issues.empty());
        CHECK(r.level.world.blocks().size() == 3);
    }
    SECTION("a diamond (A -> B and C, both -> D) is not a cycle") {
        fs.files["levels/d.lvl"] = "block min=0,0 max=1,1 top=1\n";
        fs.files["levels/b.lvl"] = "include path=levels/d.lvl offset=10,0,0\n";
        fs.files["levels/c.lvl"] = "include path=levels/d.lvl offset=20,0,0\n";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/b.lvl\ninclude path=levels/c.lvl\n");
        CHECK(r.issues.empty());
        CHECK(r.level.world.blocks().size() == 3);
    }
}

TEST_CASE("Include limits stop runaway nesting and fan-out", "[include][limits]") {
    Files fs;
    SECTION("a chain that is too deep") {
        for (int i = 0; i < 12; ++i)
            fs.files["levels/n" + std::to_string(i) + ".lvl"] = "block min=0,0 max=1,1 top=1\ninclude path=levels/n" + std::to_string(i + 1) + ".lvl offset=3,0,0\n";
        fs.files["levels/n12.lvl"] = "block min=0,0 max=1,1 top=1\n";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/n0.lvl\n");
        CHECK(anyIssue(r, "nested more than 8 levels deep"));
        CHECK(r.level.world.blocks().size() <= 1 + 9);
    }
    SECTION("an exponential fan-out is cut off") {
        // Each level includes the next one four times: without a limit this is 4^8 expansions.
        for (int i = 0; i < 8; ++i) {
            std::string body = "block min=0,0 max=1,1 top=1\n";
            for (int k = 0; k < 4; ++k) body += "include path=levels/f" + std::to_string(i + 1) + ".lvl offset=" + std::to_string(k * 3) + ",0,0\n";
            fs.files["levels/f" + std::to_string(i) + ".lvl"] = body;
        }
        fs.files["levels/f8.lvl"] = "block min=0,0 max=1,1 top=1\n";
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/f0.lvl\n");
        CHECK(anyIssue(r, "too many includes"));
        CHECK(r.level.world.blocks().size() < 5000);   // bounded, and it returned
    }
}

TEST_CASE("Bad include records are reported and skipped", "[include][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"include",                                                   "missing 'path'"},
        {"include path=levels/pois/p.lvl yaw=45",                     "multiple of 90"},
        {"include path=levels/pois/p.lvl yaw=95",                     "multiple of 90"},
        {"include path=levels/pois/p.lvl yaw=north",                  "not a number"},
        {"include path=levels/pois/p.lvl yaw=nan",                    "not a number"},
        {"include path=levels/pois/p.lvl offset=1,2",                 "needs 3 comma-separated"},
        {"include path=levels/pois/p.lvl offset=1,x,3",               "bad number"},
        {"include path=levels/pois/p.lvl prefix=Bad",                 "'prefix' must be an id"},
        {"include path=levels/pois/p.lvl door_offset=-1",             "must not be negative"},
        {"include path=levels/pois/p.lvl sector_offset=-2",           "must not be negative"},
        {"include path=levels/pois/p.lvl door_offset=x",              "not an integer"},
        {"include path=levels/pois/p.lvl colour=red",                 "unknown key 'colour'"},
        {"include path=../secret.lvl",                                "'path' must be a plain relative path"},
        {"include path=levels/../../etc/passwd.lvl",                  "'path' must be a plain relative path"},
        {"include path=/etc/passwd.lvl",                              "'path' must be a plain relative path"},
        {"include path=levels/p.txt",                                 "'path' must be a plain relative path"},
        {"include path=levels/a/./b.lvl",                             "'path' must be a plain relative path"},
        {"include path=levels//b.lvl",                                "'path' must be a plain relative path"},
        {"include path=other/p.lvl",                                  "'path' must be a plain relative path"},
        {"include path=levels/.lvl",                                  "'path' must be a plain relative path"},
        {"include path=levels/missing.lvl",                           "cannot open included file"},
    };
    Files fs;
    fs.files["levels/pois/p.lvl"] = "block min=0,0 max=1,1 top=1\n";
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = fs.load(std::string(kBase) + c.line + "\n");
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(), [](const rec::Issue& i) { return i.line == 4; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(hit->message, c.expect));
        CHECK(r.level.world.blocks().size() == 1);   // nothing was merged
    }

    SECTION("no way to read files at all") {
        const LevelLoad r = parseLevel(std::string(kBase) + "include path=levels/pois/p.lvl\n");
        CHECK(anyIssue(r, "includes are not available"));
    }
    SECTION("an 'assets/' prefix is accepted") {
        const LevelLoad r = fs.load(std::string(kBase) + "include path=assets/levels/pois/p.lvl offset=9,0,0\n");
        CHECK(r.issues.empty());
        CHECK(r.level.world.blocks().size() == 2);
    }
}

TEST_CASE("Collisions between POIs are reported", "[include][ids]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] = kPoi;

    SECTION("the same door id twice") {
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/pois/p.lvl offset=100,0,0 prefix=a\ninclude path=levels/pois/p.lvl offset=200,0,0 prefix=b\n");
        CHECK(anyIssue(r, "door id 0 is already used"));
        CHECK(anyIssue(r, "door_offset"));
    }
    SECTION("the same spawn name twice") {
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/pois/p.lvl offset=100,0,0 door_offset=1\ninclude path=levels/pois/p.lvl offset=200,0,0 door_offset=2\n");
        CHECK(anyIssue(r, "spawn name 'gate' is already defined"));
        CHECK(anyIssue(r, "prefix="));
        CHECK(r.level.spawns.size() == 1);            // the first one wins
    }
    SECTION("a door id clashing with the parent's own door") {
        const LevelLoad r = fs.load(std::string(kBase) + "block min=0,0 max=1,1 top=3 door=0\ninclude path=levels/pois/p.lvl offset=100,0,0\n");
        CHECK(anyIssue(r, "door id 0 is already used"));
    }
    SECTION("offsets fix the collisions") {
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/pois/p.lvl offset=100,0,0 prefix=a door_offset=1 sector_offset=1\n"
            "include path=levels/pois/p.lvl offset=200,0,0 prefix=b door_offset=2 sector_offset=2\n");
        CHECK(r.issues.empty());
    }
}

TEST_CASE("Problems inside an included file name that file and its line", "[include][diagnostics]") {
    Files fs;
    fs.files["levels/pois/bad.lvl"] =
        "# a comment\n"
        "block min=0,0 max=4,4 top=2\n"
        "block min=5,5 max=5,9 top=2\n"           // line 3: zero width
        "wibble x=1\n"                            // line 4: unknown record
        "trigger when=sector_cleared sector=9 do=complete\n"   // line 5: sector with no enemies
        "trigger when=enter min=0,0,0 max=1,1,1 do=open_door door=4\n";   // line 6: no such door
    const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/bad.lvl offset=50,0,0\n");

    const auto inFile = [&](int line, const std::string& needle) {
        return std::any_of(r.issues.begin(), r.issues.end(), [&](const rec::Issue& i) {
            return i.file == "levels/pois/bad.lvl" && i.line == line && mentions(i.message, needle);
        });
    };
    CHECK(inFile(3, "'max' must be greater"));
    CHECK(inFile(4, "unknown record type"));
    CHECK(inFile(5, "no enemy belongs to sector 9"));
    CHECK(inFile(6, "no block has door=4"));
    CHECK(r.level.world.blocks().size() == 2);        // base + the one good block

    // And the message a player or author reads names the file.
    const auto it = std::find_if(r.issues.begin(), r.issues.end(), [](const rec::Issue& i) { return i.line == 4; });
    REQUIRE(it != r.issues.end());
    CHECK(rec::describe(*it, "levels/island.lvl") == "levels/pois/bad.lvl:4: unknown record type 'wibble'");
    rec::Issue top{7, "oops", {}};
    CHECK(rec::describe(top, "levels/island.lvl") == "levels/island.lvl:7: oops");
    rec::Issue general{0, "no blocks", {}};
    CHECK(rec::describe(general, "levels/island.lvl") == "levels/island.lvl: no blocks");
}

TEST_CASE("Level-wide records are only read from the top-level file", "[include]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] =
        "level name=sneaky next_level=elsewhere\n"
        "environment visibility=100 far=200\n"
        "spawn pos=5,5,5\n"
        "block min=0,0 max=1,1 top=1\n";
    const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/p.lvl offset=9,0,0\n");
    CHECK(anyIssue(r, "only the top-level level file sets this"));
    CHECK(anyIssue(r, "cannot set the default spawn"));
    CHECK(r.level.name == "t");                       // not overwritten
    CHECK(r.level.nextLevel.empty());
    CHECK_FALSE(r.level.hasEnvironment);
    CHECK(near(r.level.spawn, {0.0f, 0.0f, 0.0f}));
    CHECK(r.level.world.blocks().size() == 2);        // but its blocks were still merged
}

TEST_CASE("A POI's triggers are checked in the merged level", "[include][triggers]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] =
        "block min=0,0 max=2,2 top=3 door=0\n"
        "enemy pos=1,0,1 type=melee sector=0\n"
        "trigger when=sector_cleared sector=0 do=open_door door=0\n";
    SECTION("with offsets the trigger keeps working") {
        const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/p.lvl door_offset=5 sector_offset=3\n");
        CHECK(r.issues.empty());
        REQUIRE(r.level.triggers.size() == 1);
        CHECK(r.level.triggers[0].param == 5);
        CHECK(r.level.triggers[0].sector == 3);
        CHECK(r.level.world.hasDoor(5));
    }
    SECTION("a parent trigger can use the POI's shifted door") {
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/pois/p.lvl door_offset=5 sector_offset=3\n"
            "enemy pos=0,0,0 type=melee sector=8\n"
            "trigger when=sector_cleared sector=8 do=open_door door=5\n");
        CHECK(r.issues.empty());
        CHECK(r.level.triggers.size() == 2);
    }
    SECTION("a parent trigger for an unshifted id is dropped") {
        const LevelLoad r = fs.load(std::string(kBase) +
            "include path=levels/pois/p.lvl door_offset=5\ntrigger when=enter min=0,0,0 max=1,1,1 do=open_door door=0\n");
        CHECK(anyIssue(r, "no block has door=0"));
    }
}

TEST_CASE("Patrolling targets keep their direction when a POI is turned", "[include][rotation]") {
    Files fs;
    fs.files["levels/pois/p.lvl"] = "block min=0,0 max=1,1 top=1\ntarget pos=0,0,0 patrol=3\n";
    const LevelLoad r = fs.load(std::string(kBase) + "include path=levels/pois/p.lvl yaw=90 offset=40,0,0\n");
    REQUIRE(r.level.targets.size() == 1);
    CHECK(r.level.targets[0].axis == glm::vec3(0.0f, 0.0f, -1.0f));   // +X turned a quarter: now along -Z

    World w = floorWorld();
    Combat combat(w);
    spawnTargets(r.level, combat);
    REQUIRE(combat.targets().size() == 1);
    CHECK(combat.targets()[0].patrolAxis == glm::vec3(0.0f, 0.0f, -1.0f));
}

// ---------------------------------------------------------------------------
// Include paths and the disk
// ---------------------------------------------------------------------------

TEST_CASE("Include paths are normalised or refused", "[include][path]") {
    CHECK(normalizeIncludePath("levels/pois/docks.lvl") == "levels/pois/docks.lvl");
    CHECK(normalizeIncludePath("assets/levels/pois/docks.lvl") == "levels/pois/docks.lvl");
    CHECK(normalizeIncludePath("levels/a-b_c.1.lvl") == "levels/a-b_c.1.lvl");
    CHECK(normalizeIncludePath("levels/A.lvl") == "levels/A.lvl");
    for (const char* bad : {"", "levels", "levels/", "levels/.lvl", "docks.lvl", "levels/docks.txt", "levels/docks",
                            "../levels/x.lvl", "levels/../x.lvl", "levels/./x.lvl", "levels//x.lvl", "/levels/x.lvl",
                            "levels/x y.lvl", "levels/x\\y.lvl", "levels/x:y.lvl", "briefings/x.lvl", "levels/x.lvl/"}) {
        INFO("path: '" << bad << "'");
        CHECK_FALSE(normalizeIncludePath(bad).has_value());
    }
    CHECK_FALSE(normalizeIncludePath(std::string(200, 'a') + ".lvl").has_value());
}

TEST_CASE("Includes load from disk, relative to the assets directory", "[include][io]") {
    namespace fs = std::filesystem;
    const fs::path root = "carnage_test_include_assets";
    fs::remove_all(root);
    fs::create_directories(root / "levels" / "pois");
    std::ofstream(root / "levels" / "pois" / "hut.lvl") << "block min=0,0 max=2,2 top=2\nspawn name=door pos=1,0,3\n";
    std::ofstream(root / "levels" / "main.lvl") << "level name=main\nspawn pos=0,0,0\nblock min=-5,-5 max=5,5 top=0\n"
                                                    "include path=levels/pois/hut.lvl offset=10,0,10 prefix=hut\n";
    std::ofstream(root / "levels" / "loop.lvl") << "level name=loop\nspawn pos=0,0,0\nblock min=-5,-5 max=5,5 top=0\n"
                                                    "include path=levels/loop.lvl\n";
    const std::string assets = root.string();

    SECTION("the directory is found from the path") {
        const LevelLoad r = loadLevelFile(levelPath(assets, "main"));
        CHECK(r.clean());
        CHECK(r.level.world.blocks().size() == 2);
        CHECK(r.level.hasSpawn("hut_door"));
        CHECK(r.level.includedFiles == std::vector<std::string>{"levels/pois/hut.lvl"});
    }
    SECTION("or given explicitly") {
        const LevelLoad r = loadLevelFile(levelPath(assets, "main"), assets);
        CHECK(r.clean());
    }
    SECTION("a level that includes itself is caught as a cycle, not as an endless read") {
        const LevelLoad r = loadLevelFile(levelPath(assets, "loop"));
        CHECK(anyIssue(r, "include cycle: levels/loop.lvl -> levels/loop.lvl"));
        CHECK(r.level.world.blocks().size() == 1);
    }
    SECTION("a missing included file is an issue, not a failure to load") {
        std::ofstream(root / "levels" / "gap.lvl") << "level name=gap\nspawn pos=0,0,0\nblock min=-5,-5 max=5,5 top=0\ninclude path=levels/pois/none.lvl\n";
        const LevelLoad r = loadLevelFile(levelPath(assets, "gap"));
        CHECK(r.opened);
        CHECK(anyIssue(r, "cannot open included file 'levels/pois/none.lvl'"));
    }
    fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// The link checker follows included files
// ---------------------------------------------------------------------------

TEST_CASE("LevelGraph sees portals and spawns that come from included files", "[include][graph]") {
    namespace fs = std::filesystem;
    const fs::path root = "carnage_test_graph_include";
    fs::remove_all(root);
    fs::create_directories(root / "levels" / "pois");
    std::ofstream(root / "levels" / "pois" / "gate.lvl")
        << "block min=0,0 max=4,4 top=2\nspawn name=arrive pos=2,0,5\n"
           "trigger when=enter min=0,-1,0 max=4,3,4 do=portal level=outpost spawn=home\n";
    std::ofstream(root / "levels" / "island.lvl")
        << "level name=island\nspawn pos=0,0,0\nblock min=-20,-20 max=20,20 top=0\n"
           "include path=levels/pois/gate.lvl offset=10,0,0 prefix=north\n";
    std::ofstream(root / "levels" / "outpost.lvl")
        << "level name=outpost\nspawn pos=0,0,0\nspawn name=home pos=3,0,3\nblock min=-9,-9 max=9,9 top=0\n"
           "trigger when=enter min=-1,-1,-1 max=1,2,1 do=portal level=island spawn=north_arrive\n";
    const std::string assets = root.string();

    SECTION("a consistent network, with the portal living in an included file") {
        const LevelGraphReport r = checkLevelGraph(assets, "island");
        for (const std::string& p : r.problems) FAIL(p);
        CHECK(r.ok());
        CHECK(r.levels == std::vector<std::string>{"island", "outpost"});
    }
    SECTION("a prefixed spawn that does not exist is caught") {
        std::ofstream(root / "levels" / "outpost.lvl")
            << "level name=outpost\nspawn pos=0,0,0\nspawn name=home pos=3,0,3\nblock min=-9,-9 max=9,9 top=0\n"
               "trigger when=enter min=-1,-1,-1 max=1,2,1 do=portal level=island spawn=arrive\n";   // forgot the prefix
        const LevelGraphReport r = checkLevelGraph(assets, "island");
        CHECK(std::any_of(r.problems.begin(), r.problems.end(), [](const std::string& p) { return mentions(p, "spawn 'arrive' in 'island'"); }));
    }
    SECTION("a problem inside an included file names that file") {
        std::ofstream(root / "levels" / "pois" / "gate.lvl")
            << "block min=0,0 max=4,4 top=2\nwibble x=1\nspawn name=arrive pos=2,0,5\n"
               "trigger when=enter min=0,-1,0 max=4,3,4 do=portal level=outpost spawn=home\n";
        const LevelGraphReport r = checkLevelGraph(assets, "island");
        CHECK(std::any_of(r.problems.begin(), r.problems.end(), [](const std::string& p) {
            return mentions(p, "levels/pois/gate.lvl") && mentions(p, "line 2") && mentions(p, "unknown record type");
        }));
    }
    fs::remove_all(root);
}

// ---------------------------------------------------------------------------
// environment
// ---------------------------------------------------------------------------

TEST_CASE("The environment record sets fog, far plane and shadow reach", "[environment][parse]") {
    const LevelLoad r = parseLevel(std::string(kBase) + "environment visibility=1200 far=1500 shadow=80\n");
    REQUIRE(r.issues.empty());
    CHECK(r.level.hasEnvironment);
    CHECK(r.level.environment.farPlane == 1500.0f);
    CHECK(r.level.environment.shadowRadius == 80.0f);
    CHECK(r.level.environment.haze == Approx(3.0f / 1200.0f));
    CHECK(r.level.environment.visibility() == Approx(1200.0f));

    SECTION("every key is optional") {
        const LevelLoad d = parseLevel(std::string(kBase) + "environment far=800 visibility=600\n");
        CHECK(d.level.environment.shadowRadius == Environment::kDefaultShadowRadius);
    }
    SECTION("no environment record: the old values") {
        const LevelLoad none = parseLevel(kBase);
        CHECK_FALSE(none.level.hasEnvironment);
        CHECK(none.level.environment == Environment{});
        CHECK(none.level.environment.haze == 0.0045f);
        CHECK(none.level.environment.farPlane == 300.0f);
        CHECK(none.level.environment.shadowRadius == 50.0f);
    }
}

TEST_CASE("Bad environment records are reported and ignored", "[environment][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"environment visibility=abc",               "not a number"},
        {"environment visibility=5",                 "'visibility' must be between"},
        {"environment visibility=99999",             "'visibility' must be between"},
        {"environment visibility=nan",               "not a number"},
        {"environment far=10",                       "'far' must be between"},
        {"environment far=100000",                   "'far' must be between"},
        {"environment shadow=1",                     "'shadow' must be between"},
        {"environment shadow=5000",                  "'shadow' must be between"},
        {"environment shadow=wide",                  "not a number"},
        {"environment fog=3",                        "unknown key 'fog'"},
    };
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = parseLevel(std::string(kBase) + c.line + "\n");
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(), [](const rec::Issue& i) { return i.line == 4; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(hit->message, c.expect));
        CHECK_FALSE(r.level.hasEnvironment);
        CHECK(r.level.environment == Environment{});
    }
    SECTION("a duplicate is ignored") {
        const LevelLoad r = parseLevel(std::string(kBase) + "environment far=400 visibility=300\nenvironment far=900 visibility=800\n");
        CHECK(anyIssue(r, "duplicate 'environment'"));
        CHECK(r.level.environment.farPlane == 400.0f);
    }
}

TEST_CASE("A far plane that shows the edge of the world is warned about", "[environment]") {
    // Fog at the far plane should be nearly opaque, or distant geometry pops in at the clip distance.
    const LevelLoad foggy = parseLevel(std::string(kBase) + "environment visibility=400 far=600\n");
    CHECK(foggy.issues.empty());
    const LevelLoad clear = parseLevel(std::string(kBase) + "environment visibility=4000 far=500\n");
    REQUIRE(anyIssue(clear, "will pop into view"));
    CHECK(clear.level.hasEnvironment);                // a warning, not a rejection: the level still loads
    CHECK(clear.level.environment.farPlane == 500.0f);
}

TEST_CASE("Environment maths", "[environment]") {
    const Environment e;
    CHECK(e.fogAt(0.0f) == 0.0f);
    CHECK(e.fogAt(-5.0f) == 0.0f);
    CHECK(e.fogAt(e.visibility()) == Approx(1.0f - std::exp(-3.0f)));   // 95% opaque
    CHECK(e.fogAt(100000.0f) == Approx(1.0f));
    float last = 0.0f;
    for (float d = 0.0f; d < 3000.0f; d += 25.0f) { REQUIRE(e.fogAt(d) >= last); last = e.fogAt(d); }

    // The old fixed values hid 74% of the scene at their own far plane: fine for a corridor.
    CHECK(Environment{}.fogAtFarPlane() == Approx(1.0f - std::exp(-0.0045f * 300.0f)));
    CHECK_FALSE(Environment{}.farPlaneIsHidden());

    Environment island;
    island.haze     = Environment::hazeForVisibility(1000.0f);
    island.farPlane = 1500.0f;
    CHECK(island.farPlaneIsHidden());
}

TEST_CASE("sanitized() forces every value into its allowed range", "[environment]") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const float bad : {nan, -1.0f, 0.0f}) {
        Environment e;
        e.haze = bad; e.farPlane = bad; e.shadowRadius = bad;
        CHECK(sanitized(e) == Environment{});          // garbage falls back to the defaults
    }
    Environment huge;
    huge.haze = inf; huge.farPlane = inf; huge.shadowRadius = inf;
    const Environment s = sanitized(huge);
    CHECK(s.farPlane == Environment::kMaxFar);
    CHECK(s.shadowRadius == Environment::kMaxShadow);
    CHECK(s.visibility() >= Environment::kMinVisibility - 0.01f);

    Environment tiny;
    tiny.haze = 1e-9f; tiny.farPlane = 1.0f; tiny.shadowRadius = 0.5f;
    const Environment t = sanitized(tiny);
    CHECK(t.farPlane == Environment::kMinFar);
    CHECK(t.shadowRadius == Environment::kMinShadow);
    CHECK(t.visibility() <= Environment::kMaxVisibility + 1.0f);

    const Environment ok{0.002f, 800.0f, 90.0f};
    CHECK(sanitized(ok) == ok);                         // good values are untouched
}

// ---------------------------------------------------------------------------
// Shadows
// ---------------------------------------------------------------------------

TEST_CASE("At the default radius the shadow settings reproduce the old fixed ones exactly", "[shadow]") {
    CHECK(shadowTexel(50.0f, 4096) == Approx(2.0f * 50.0f / 4096.0f));
    CHECK(shadowDepthHalf(50.0f) == 120.0f);
    CHECK(shadowBiasNdc(50.0f, 4096) == Approx(0.0006f).margin(1e-7));
}

TEST_CASE("Shadow depth range and bias scale with the box", "[shadow]") {
    for (const float radius : {10.0f, 30.0f, 50.0f, 80.0f, 120.0f, 200.0f}) {
        INFO("radius " << radius);
        CHECK(shadowDepthHalf(radius) >= 120.0f);
        CHECK(shadowDepthHalf(radius) >= radius * 2.0f);       // tall things at the edge still cast
        // The bias is a fixed number of texels of depth: constant in world space however big the box.
        const float worldBias = shadowBiasNdc(radius, 4096) * 2.0f * shadowDepthHalf(radius);
        CHECK(worldBias == Approx(kShadowBiasTexels * shadowTexel(radius, 4096)).epsilon(1e-5));
        CHECK(shadowBiasNdc(radius, 4096) > 0.0f);
        CHECK(shadowBiasNdc(radius, 4096) < 0.01f);
    }
    // A wider box has bigger texels, so its bias grows with them (the opposite is wasted peter-panning).
    CHECK(shadowBiasNdc(200.0f, 4096) * shadowDepthHalf(200.0f) > shadowBiasNdc(50.0f, 4096) * shadowDepthHalf(50.0f));
}

TEST_CASE("The sun shadow box is snapped to whole texels so edges do not crawl", "[shadow]") {
    const glm::vec3 sun = glm::normalize(glm::vec3(0.50f, 0.55f, 0.40f));
    const glm::vec3 fwd{0.0f, 0.0f, -1.0f};
    const float radius = 80.0f;
    const SunShadow ref = computeSunShadow({0.0f, 1.7f, 0.0f}, fwd, sun, radius, 4096);

    // A world point's position in shadow-map texels.
    const auto texelsOf = [&](const SunShadow& s, glm::vec3 p) {
        const glm::vec4 c = s.viewProj * glm::vec4(p, 1.0f);
        return glm::vec2(c.x, c.y) * 0.5f * 4096.0f;   // NDC [-1,1] -> texels
    };
    const glm::vec3 probe{12.3f, 0.0f, -45.6f};
    const glm::vec2 base = texelsOf(ref, probe);

    // Moving the camera by less than a texel must not move the shadow map in texel space by a
    // fractional amount: the frame-to-frame shift is a whole number of texels (usually zero).
    for (const float step : {0.001f, 0.01f, 0.05f, 0.11f, 0.3f, 1.7f, 25.0f}) {
        const SunShadow s = computeSunShadow({step * 0.6f, 1.7f, -step * 0.8f}, fwd, sun, radius, 4096);
        const glm::vec2 shift = texelsOf(s, probe) - base;
        CHECK(shift.x == Approx(std::round(shift.x)).margin(0.02));
        CHECK(shift.y == Approx(std::round(shift.y)).margin(0.02));
    }
}

TEST_CASE("The sun shadow box follows the player and covers what is near", "[shadow]") {
    const glm::vec3 sun = glm::normalize(glm::vec3(0.50f, 0.55f, 0.40f));
    for (const float radius : {30.0f, 50.0f, 120.0f}) {
        for (const glm::vec3 eye : {glm::vec3(0, 1.7f, 0), glm::vec3(900, 1.7f, -1400), glm::vec3(-500, 40, 800)}) {
            for (const glm::vec3 fwd : {glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 0, 1))}) {
                const SunShadow s = computeSunShadow(eye, fwd, sun, radius, 4096);
                // Things at the player (feet, head, a floor patch around them) are inside the box and its depth range.
                for (const glm::vec3 d : {glm::vec3(0, 0, 0), glm::vec3(0, -1.7f, 0), glm::vec3(0, 2, 0), glm::vec3(5, -1.7f, 5), glm::vec3(-5, -1.7f, -5)}) {
                    const glm::vec4 c = s.viewProj * glm::vec4(eye + d, 1.0f);
                    REQUIRE(std::abs(c.x) <= 1.0f);
                    REQUIRE(std::abs(c.y) <= 1.0f);
                    REQUIRE(c.z >= -1.0f);
                    REQUIRE(c.z <= 1.0f);
                }
            }
        }
    }
}

TEST_CASE("A taller sun box has a deeper depth range, so tall things still cast", "[shadow]") {
    const glm::vec3 sun = glm::normalize(glm::vec3(0.50f, 0.55f, 0.40f));
    const glm::vec3 eye{0.0f, 1.7f, 0.0f}, fwd{0.0f, 0.0f, -1.0f};
    // A 200 m tall tower 150 m from the player, in the direction of the sun, still falls inside the depth range.
    for (const float radius : {50.0f, 120.0f, 200.0f}) {
        const SunShadow s = computeSunShadow(eye, fwd, sun, radius, 4096);
        const glm::vec3 base = eye + glm::vec3(0, -1.7f, -radius * 0.6f);
        const glm::vec4 top  = s.viewProj * glm::vec4(base + glm::vec3(0, 60, 0), 1.0f);
        CHECK(std::abs(top.x) <= 1.0f);
        CHECK(top.z >= -1.0f);
        CHECK(top.z <= 1.0f);
    }
}

// ---------------------------------------------------------------------------
// The shipped Kodiak Island
// ---------------------------------------------------------------------------

namespace {
const std::string kIsland = std::string(CARNAGE_ASSET_DIR) + "/levels/kodiak_island.lvl";
}

TEST_CASE("kodiak_island loads cleanly and composes its POIs", "[island][include]") {
    const LevelLoad r = loadLevelFile(kIsland);
    for (const rec::Issue& i : r.issues) FAIL(rec::describe(i, kIsland));
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(L.name == "kodiak_island");
    CHECK(L.includedFiles == std::vector<std::string>{"levels/pois/docks.lvl", "levels/pois/relay_tower.lvl"});
    CHECK(L.world.doorIds() == std::vector<int>{10, 20});            // each POI's door 0, shifted apart
    CHECK(L.hasSpawn("dock_landing"));
    CHECK(L.hasSpawn("relay_gate"));
    CHECK(L.hasSpawn("from_level01_escape"));
    CHECK(L.hasSpawn("from_kodiak_hub"));

    std::set<int> sectors;
    for (const EnemySpawn& e : L.enemies) if (e.sector >= 0) sectors.insert(e.sector);
    CHECK(sectors == std::set<int>{0, 1});                           // the docks' guards and the tower's
    CHECK(L.enemies.size() >= 12);                                   // a real population to put to sleep
    CHECK(L.world.lights().size() > 15);                             // more lights than the renderer can use at once

    // Each sector has an opener for the right door, and nothing shares an id by accident.
    int open10 = 0, open20 = 0, portals = 0;
    for (const TriggerVolume& t : L.triggers) {
        open10  += t.action == TriggerAction::OpenDoor && t.param == 10 && t.sector == 0;
        open20  += t.action == TriggerAction::OpenDoor && t.param == 20 && t.sector == 1;
        portals += t.action == TriggerAction::Portal;
    }
    CHECK(open10 == 1);
    CHECK(open20 == 1);
    CHECK(portals == 1);                                             // the gate back to the hub
}

TEST_CASE("kodiak_island has fog that hides its far plane", "[island][environment]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    const Environment& e = r.level.environment;
    CHECK(r.level.hasEnvironment);
    CHECK(e.farPlaneIsHidden());                                     // the edge of the world fades out, it does not pop
    CHECK(e.farPlane >= 1000.0f);
    CHECK(e.visibility() < e.farPlane);
    CHECK(e.shadowRadius > Environment::kDefaultShadowRadius);       // bigger than a corridor's, for open ground
    CHECK(sanitized(e) == e);                                        // already within every limit
}

TEST_CASE("The other shipped levels keep the old fog and shadow values", "[island][environment]") {
    for (const char* id : {"level01_escape", "kodiak_hub", "kodiak_range"}) {
        INFO(id);
        const LevelLoad r = loadLevelFile(std::string(CARNAGE_ASSET_DIR) + "/levels/" + id + ".lvl");
        REQUIRE(r.clean());
        Environment e = r.level.environment;
        e.snow = 0.0f;  // weather is the one thing the hub sets (see test_materials.cpp); fog and shadows are untouched
        CHECK(e == Environment{});
    }
}

TEST_CASE("Everything on kodiak_island stands on solid ground, in every POI too", "[island][include]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    const LevelData& L = r.level;
    int bad = 0;
    const auto standable = [&](glm::vec3 p, float slack) {
        const auto g = L.world.ground({p.x, p.z}, p.y + slack);
        return g && std::abs(g->height - p.y) <= slack;
    };
    const auto check = [&](const char* what, glm::vec3 p, float slack) {
        if (!standable(p, slack)) {
            ++bad;
            FAIL_CHECK(what << " at (" << p.x << ", " << p.y << ", " << p.z << ") has no ground within " << slack << " m");
        }
    };
    check("default spawn", L.spawn, 0.05f);
    for (const NamedSpawn& s : L.spawns) check(("spawn " + s.name).c_str(), s.point.pos, 0.05f);
    for (const EnemySpawn& e : L.enemies) check("enemy", e.pos, 0.3f);
    for (const TargetSpawn& t : L.targets) check("target", t.pos, 0.3f);
    for (const TriggerVolume& t : L.triggers)
        if (t.action == TriggerAction::Checkpoint) check("checkpoint", t.point, 0.05f);
    for (const PickupSpawn& k : L.pickups) {
        const auto g = L.world.ground({k.pos.x, k.pos.z}, k.pos.y + 0.05f);
        if (!g || k.pos.y - g->height > 0.8f) { ++bad; FAIL_CHECK("pickup floating or buried at (" << k.pos.x << ", " << k.pos.y << ", " << k.pos.z << ")"); }
    }
    CHECK(bad == 0);
}

TEST_CASE("kodiak_island's spawns are inside the basin and clear of every portal", "[island]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    const LevelData& L = r.level;
    std::vector<SpawnPoint> spawns{L.spawnPoint()};
    for (const NamedSpawn& s : L.spawns) spawns.push_back(s.point);
    for (const SpawnPoint& sp : spawns) {
        CHECK(sp.pos.x > -500.0f);
        CHECK(sp.pos.x < 500.0f);
        CHECK(sp.pos.z > -1300.0f);
        CHECK(sp.pos.z < 100.0f);
        for (const TriggerVolume& t : L.triggers)
            if (t.action == TriggerAction::Portal) CHECK_FALSE(t.contains(sp.pos));   // never arrive inside a gate
    }
}

TEST_CASE("The docks are turned east: its sea ends up on the east side and its door still works", "[island][rotation]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    // In docks.lvl the pier runs to z = -90 (towards the sea). Turned 270 degrees about (380, 0, -300) the
    // pier end must be east of the quay: x > 380.
    const SpawnPoint landing = L.spawnPoint("dock_landing");
    CHECK(landing.pos.x > 440.0f);
    CHECK(landing.pos.z == Approx(-300.0f).margin(1e-3));
    // It faced 180 degrees in the POI (towards the land); turned 270 more, that is 450 = 90: facing west, to shore.
    CHECK(std::cos(landing.yaw) == Approx(0.0f).margin(1e-4));
    CHECK(std::sin(landing.yaw) == Approx(1.0f).margin(1e-4));

    // Walk from the pier end straight to the road along the way the spawn faces.
    MovementController mc(L.world);
    mc.respawn(landing.pos);
    MoveInput in;
    in.move = {0.0f, 1.0f};
    for (int i = 0; i < 360; ++i) mc.tick(kDt, landing.yaw, in);
    CHECK(mc.position().x < landing.pos.x - 15.0f);                  // it walked inland (west) along the pier
    CHECK(mc.position().z == Approx(-300.0f).margin(3.0f));         // staying on the pier
}

TEST_CASE("A player can walk the whole road from the beach to the relay tower", "[island][walk]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    const LevelData& L = r.level;
    MovementController mc(L.world);
    mc.respawn(L.spawn);
    MoveInput in;
    in.move   = {0.0f, 1.0f};
    in.sprint = true;
    float furthest = L.spawn.z;
    for (int i = 0; i < 60 * 150; ++i) {              // 2.5 minutes of sprinting north along x = 0
        mc.tick(kDt, 0.0f, in);
        furthest = std::min(furthest, mc.position().z);
        if (mc.position().z < -1100.0f) break;
    }
    // The road is clear and flat: the player gets past the relay tower turn-off (z = -1000) without
    // falling, getting stuck on a prop, or leaving the basin.
    CHECK(furthest < -1050.0f);
    CHECK(std::abs(mc.position().x) < 60.0f);
    CHECK(mc.position().y == Approx(0.0f).margin(0.5));
}

TEST_CASE("The island's gate leads back to the hub", "[island][walk]") {
    const LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    TriggerSystem ts;
    installTriggers(r.level, ts);
    std::optional<TriggerEvent> portal;
    for (const TriggerEvent& e : ts.update({0.0f, 0.0f, 89.0f}, [](int) { return 1; }))
        if (e.action == TriggerAction::Portal) portal = e;
    REQUIRE(portal.has_value());
    CHECK(portal->target == "kodiak_hub");
    CHECK(portal->targetSpawn == "from_kodiak_island");
}

TEST_CASE("Both POIs can be completed: their guards open their doors", "[island][objectives]") {
    LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    World& w = r.level.world;
    Combat combat(w);
    Enemies enemies;
    TriggerSystem ts;
    spawnEnemies(r.level, enemies, combat);
    installTriggers(r.level, ts);
    CHECK(enemies.totalInSector(0) == 4);                            // the docks' guards
    CHECK(enemies.totalInSector(1) == 5);                            // the tower's guards

    const auto alive = [&](int s) { return enemies.aliveInSector(combat, s); };
    const auto kill = [&](int sector) {
        for (const Enemy& e : enemies.enemies())
            if (e.sector == sector) combat.targetAt(e.target)->hp = 0.0f;
    };
    kill(1);
    int opened = -1;
    for (const TriggerEvent& e : ts.update({0.0f, 0.0f, 60.0f}, alive))
        if (e.action == TriggerAction::OpenDoor) opened = e.param;
    CHECK(opened == 20);                                             // only the tower's door
    w.setDoorOpen(20, true);
    CHECK(w.hasDoor(10));
    kill(0);
    opened = -1;
    for (const TriggerEvent& e : ts.update({0.0f, 0.0f, 60.0f}, alive))
        if (e.action == TriggerAction::OpenDoor) opened = e.param;
    CHECK(opened == 10);
}

TEST_CASE("Far-away enemies on the island sleep, and the ones near the player wake", "[island][wake]") {
    LevelLoad r = loadLevelFile(kIsland);
    REQUIRE(r.clean());
    Combat combat(r.level.world);
    Enemies enemies;
    spawnEnemies(r.level, enemies, combat);
    const int total = static_cast<int>(enemies.enemies().size());

    // On the beach, almost everything is more than 65 m away.
    for (int i = 0; i < 4; ++i) enemies.tick(kDt, combat, r.level.world, {0.0f, 0.0f, 60.0f}, 1.7f);
    EnemyActivity a = enemies.activity();
    CHECK(a.awake + a.asleep == total);
    CHECK(a.asleep > total * 3 / 4);

    // In the middle of the basin, only the neighbours are awake.
    for (int i = 0; i < 4; ++i) enemies.tick(kDt, combat, r.level.world, {0.0f, 0.0f, -520.0f}, 1.7f);
    a = enemies.activity();
    CHECK(a.awake >= 1);
    CHECK(a.asleep > total / 2);
}

TEST_CASE("The full Kodiak network, including both POI files, passes the link check", "[island][graph]") {
    const LevelGraphReport r = checkLevelGraph(CARNAGE_ASSET_DIR, "kodiak_island");
    for (const std::string& p : r.problems) FAIL(p);
    CHECK(r.ok());
    CHECK(r.levels.size() == 4);
}
