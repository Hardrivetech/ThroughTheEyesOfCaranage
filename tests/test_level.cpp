#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Level.hpp"
#include "game/Movement.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

namespace {

const std::string kShipped = std::string(CARNAGE_ASSET_DIR) + "/levels/level01_escape.lvl";

// A tiny but complete level, used as the base for the malformed-input tests.
constexpr const char* kValidBase =
    "level name=test\n"
    "spawn pos=1,0,2\n"
    "block min=-10,-10 max=10,10 top=0\n";

std::string withLine(const std::string& line) { return std::string(kValidBase) + line + "\n"; }

bool mentions(const LevelIssue& i, const std::string& needle) {
    return i.message.find(needle) != std::string::npos;
}

int countDoor(const World& w, int id) {
    int n = 0;
    for (const Block& b : w.blocks()) n += b.door == id ? 1 : 0;
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// The shipped level
// ---------------------------------------------------------------------------

TEST_CASE("The shipped level loads without a single issue", "[level]") {
    const LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.opened);
    for (const LevelIssue& i : r.issues) FAIL("line " << i.line << ": " << i.message);
    CHECK(r.clean());
}

TEST_CASE("The shipped level has the expected three-sector layout", "[level]") {
    const LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(L.name == "level01_escape");
    CHECK(near(L.spawn, {0.0f, 0.0f, 10.0f}));
    CHECK(L.world.blocks().size() == 92);
    CHECK(L.world.lights().size() == 15);
    CHECK(L.targets.size() == 8);
    CHECK(L.pickups.size() == 6);
    CHECK(L.enemies.size() == 11);
    CHECK(L.triggers.size() == 5);

    int perSector[3] = {0, 0, 0};
    int melee = 0, ranged = 0;
    for (const EnemySpawn& e : L.enemies) {
        REQUIRE(e.sector >= 0);
        REQUIRE(e.sector < 3);
        ++perSector[e.sector];
        (e.type == EnemyType::Melee ? melee : ranged)++;
    }
    CHECK(perSector[0] == 4);  // corridor
    CHECK(perSector[1] == 4);  // arena
    CHECK(perSector[2] == 3);  // pit
    CHECK(melee == 6);
    CHECK(ranged == 5);

    int pipes = 0, bats = 0, barrels = 0;
    for (const PickupSpawn& p : L.pickups) {
        pipes   += p.kind == PickupKind::Pipe;
        bats    += p.kind == PickupKind::Bat;
        barrels += p.kind == PickupKind::Barrel;
    }
    CHECK(pipes == 2);
    CHECK(bats == 2);
    CHECK(barrels == 2);

    // Doors 0 (corridor -> arena) and 1 (arena -> pit) exist, once each.
    CHECK(countDoor(L.world, 0) == 1);
    CHECK(countDoor(L.world, 1) == 1);
    CHECK(countDoor(L.world, 2) == 0);

    // Triggers: three sector objectives and two checkpoints.
    int cleared = 0, checkpoints = 0, completes = 0, opens = 0;
    for (const TriggerVolume& t : L.triggers) {
        cleared     += t.condition == TriggerCondition::SectorCleared;
        checkpoints += t.action == TriggerAction::Checkpoint;
        completes   += t.action == TriggerAction::Complete;
        opens       += t.action == TriggerAction::OpenDoor;
    }
    CHECK(cleared == 3);
    CHECK(checkpoints == 2);
    CHECK(completes == 1);
    CHECK(opens == 2);
}

TEST_CASE("Everything in the shipped level stands on solid ground", "[level]") {
    const LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.clean());
    const LevelData& L = r.level;
    const auto groundedAt = [&](glm::vec3 p) {
        const auto g = L.world.ground({p.x, p.z}, p.y + 0.1f);
        return g && std::abs(g->height - p.y) < 0.02f;
    };

    CHECK(groundedAt(L.spawn));
    for (const EnemySpawn& e : L.enemies)  CHECK(groundedAt(e.pos));
    for (const TargetSpawn& t : L.targets) CHECK(groundedAt(t.pos));
    for (const TriggerVolume& t : L.triggers)
        if (t.action == TriggerAction::Checkpoint) CHECK(groundedAt(t.point));
    for (const PickupSpawn& p : L.pickups)  // resting just above a surface
        CHECK(L.world.ground({p.pos.x, p.pos.z}, p.pos.y).has_value());
}

TEST_CASE("The shipped level spawns into the live systems and runs its objectives", "[level][triggers]") {
    LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    Combat combat(r.level.world);
    Enemies enemies;
    Interact interact;
    TriggerSystem triggers;
    spawnTargets(L, combat);
    spawnEnemies(L, enemies, combat);
    spawnPickups(L, interact);
    installTriggers(L, triggers);

    CHECK(combat.targets().size() == L.targets.size() + L.enemies.size());  // enemies are targets too
    CHECK(enemies.enemies().size() == 11);
    CHECK(enemies.totalInSector(0) == 4);
    CHECK(enemies.totalInSector(1) == 4);
    CHECK(enemies.totalInSector(2) == 3);
    CHECK(interact.pickups().size() == 6);
    CHECK(triggers.volumes().size() == 5);
    CHECK(triggers.activeSector() == 0);

    const auto alive = [&](int s) { return enemies.aliveInSector(combat, s); };
    const auto killSector = [&](int s) {
        for (const Enemy& e : enemies.enemies())
            if (e.sector == s) combat.targetAt(e.target)->hp = 0.0f;
    };
    const glm::vec3 spawn = L.spawn;

    SECTION("objectives fire in order: door, gate, then level complete") {
        CHECK(triggers.update(spawn, alive).empty());

        killSector(0);
        auto ev = triggers.update(spawn, alive);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::OpenDoor);
        CHECK(ev[0].param == 0);
        CHECK(triggers.activeSector() == 1);

        killSector(1);
        ev = triggers.update(spawn, alive);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::OpenDoor);
        CHECK(ev[0].param == 1);

        killSector(2);
        ev = triggers.update(spawn, alive);
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::Complete);
        CHECK(triggers.activeSector() == -1);
        CHECK(triggers.update(spawn, alive).empty());  // fires once
    }
    SECTION("walking into the arena and pit ledge sets checkpoints, once each") {
        const auto arena = triggers.update({0.0f, 0.0f, -80.0f}, alive);
        REQUIRE(arena.size() == 1);
        CHECK(arena[0].action == TriggerAction::Checkpoint);
        CHECK(near(arena[0].point, {0.0f, 0.0f, -80.0f}));
        CHECK(triggers.update({0.0f, 0.0f, -80.0f}, alive).empty());

        const auto pit = triggers.update({5.0f, 0.0f, -133.0f}, alive);
        REQUIRE(pit.size() == 1);
        CHECK(pit[0].action == TriggerAction::Checkpoint);
        CHECK(near(pit[0].point, {5.0f, 0.0f, -133.0f}));
    }
    SECTION("objective enemies never respawn") {
        for (const Enemy& e : enemies.enemies()) CHECK_FALSE(combat.targets()[e.target].respawns);
    }
}

TEST_CASE("Doors from the level file block the player until opened", "[level][doors]") {
    LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.clean());
    World& w = r.level.world;

    const auto walk = [&](glm::vec3 start, int ticks) {
        MovementController mc(w);
        mc.respawn(start);
        MoveInput in;
        in.move = {0.0f, 1.0f};
        for (int i = 0; i < ticks; ++i) mc.tick(kDt, 0.0f, in);
        return mc.position();
    };

    SECTION("door 0 at the end of the corridor") {
        const glm::vec3 closed = walk({0.0f, 0.0f, -70.0f}, 360);
        CHECK(closed.z >= -75.0f + 0.4f - 1e-3f);  // stopped by the door
        w.setDoorOpen(0, true);
        const glm::vec3 open = walk({0.0f, 0.0f, -70.0f}, 360);
        CHECK(open.z < -80.0f);                    // through, into the arena
        w.resetDoors();
        CHECK(walk({0.0f, 0.0f, -70.0f}, 360).z >= -75.0f + 0.4f - 1e-3f);
    }
    SECTION("gate 1 between the arena and the pit") {
        const glm::vec3 closed = walk({0.0f, 0.0f, -120.0f}, 480);
        CHECK(closed.z >= -130.0f + 0.4f - 1e-3f);
        w.setDoorOpen(1, true);
        CHECK(walk({0.0f, 0.0f, -120.0f}, 480).z < -132.0f);
    }
}

// ---------------------------------------------------------------------------
// Parsing: what each record produces
// ---------------------------------------------------------------------------

TEST_CASE("Records parse into the right data", "[level][parse]") {
    const LevelLoad r = parseLevel(
        "level name=unit\n"
        "spawn pos=1.5,0,-2\n"
        "block min=-4,-8 max=4,8 top=2.5 base=1 color=0.1,0.2,0.3 material=steel\n"
        "block min=0,0 max=2,6 h0=1 h1=3 axis=z material=painted_metal\n"
        "block min=0,0 max=6,2 h0=0 h1=1 axis=x\n"
        "block min=-1,-1 max=1,1 top=4.8 visible=0 door=7\n"
        "light pos=0,4,0 color=9,7,5 radius=16\n"
        "light pos=1,2,3\n"
        "target pos=0,0,2 patrol=2\n"
        "target pos=1,0,2\n"
        "enemy pos=0,0,-6 type=ranged sector=3\n"
        "enemy pos=1,0,-6 type=melee\n"
        "pickup kind=barrel pos=2,0.45,-18\n"
        "pickup kind=bat pos=-2,0.06,-8 yaw=1.2\n"
        "trigger when=sector_cleared sector=3 do=open_door door=7\n"
        "trigger when=enter min=-1,-1,-1 max=1,1,1 do=checkpoint point=0.5,0,0.5\n"
        "trigger when=sector_cleared sector=3 do=complete\n");
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(L.name == "unit");
    CHECK(near(L.spawn, {1.5f, 0.0f, -2.0f}));

    const auto blocks = L.world.blocks();
    REQUIRE(blocks.size() == 4);
    const Block& flat = blocks[0];
    CHECK(flat.min == glm::vec2(-4.0f, -8.0f));
    CHECK(flat.max == glm::vec2(4.0f, 8.0f));
    CHECK(flat.h0 == 2.5f);
    CHECK(flat.h1 == 2.5f);
    CHECK(flat.base == 1.0f);
    CHECK(near(flat.color, {0.1f, 0.2f, 0.3f}));
    CHECK(flat.material == Material::Steel);
    CHECK(flat.visible);
    CHECK(flat.door == -1);

    const Block& rampZ = blocks[1];
    CHECK(rampZ.axis == 1);
    CHECK(rampZ.h0 == 1.0f);
    CHECK(rampZ.h1 == 3.0f);
    CHECK(rampZ.base == Approx(-0.2f));  // default underside
    CHECK(rampZ.material == Material::PaintedMetal);
    CHECK(rampZ.surface({1.0f, 3.0f}) == Approx(2.0f));
    CHECK(blocks[2].axis == 0);

    CHECK_FALSE(blocks[3].visible);
    CHECK(blocks[3].door == 7);
    CHECK(blocks[3].enabled);

    REQUIRE(L.world.lights().size() == 2);
    CHECK(near(L.world.lights()[0].pos, {0.0f, 4.0f, 0.0f}));
    CHECK(near(L.world.lights()[0].color, {9.0f, 7.0f, 5.0f}));
    CHECK(L.world.lights()[0].radius == 16.0f);
    CHECK(L.world.lights()[1].radius == 12.0f);  // WorldLight default

    REQUIRE(L.targets.size() == 2);
    CHECK(L.targets[0].patrol == 2.0f);
    CHECK(L.targets[1].patrol == 0.0f);

    REQUIRE(L.enemies.size() == 2);
    CHECK(L.enemies[0].type == EnemyType::Ranged);
    CHECK(L.enemies[0].sector == 3);
    CHECK(L.enemies[1].type == EnemyType::Melee);
    CHECK(L.enemies[1].sector == -1);  // free roaming by default

    REQUIRE(L.pickups.size() == 2);
    CHECK(L.pickups[0].kind == PickupKind::Barrel);
    CHECK(L.pickups[1].kind == PickupKind::Bat);
    CHECK(L.pickups[1].yaw == Approx(1.2f));

    REQUIRE(L.triggers.size() == 3);
    CHECK(L.triggers[0].condition == TriggerCondition::SectorCleared);
    CHECK(L.triggers[0].action == TriggerAction::OpenDoor);
    CHECK(L.triggers[0].sector == 3);
    CHECK(L.triggers[0].param == 7);
    CHECK(L.triggers[1].condition == TriggerCondition::PlayerEnters);
    CHECK(L.triggers[1].action == TriggerAction::Checkpoint);
    CHECK(near(L.triggers[1].min, {-1.0f, -1.0f, -1.0f}));
    CHECK(near(L.triggers[1].max, {1.0f, 1.0f, 1.0f}));
    CHECK(near(L.triggers[1].point, {0.5f, 0.0f, 0.5f}));
    CHECK(L.triggers[2].action == TriggerAction::Complete);
    CHECK_FALSE(L.triggers[2].fired);
}

TEST_CASE("Comments, blank lines, tabs and Windows line endings are fine", "[level][parse]") {
    const LevelLoad r = parseLevel(
        "# a comment\r\n"
        "\r\n"
        "   \t  \r\n"
        "level   name=win   # trailing comment\r\n"
        "spawn\tpos=0,0,1\r\n"
        "  block   min=-1,-1\tmax=1,1   top=1  \r\n"
        "#block min=bad\r\n");
    CHECK(r.clean());
    CHECK(r.level.name == "win");
    CHECK(r.level.world.blocks().size() == 1);
    CHECK(near(r.level.spawn, {0.0f, 0.0f, 1.0f}));
}

TEST_CASE("A file without a trailing newline still parses its last line", "[level][parse]") {
    const LevelLoad r = parseLevel("spawn pos=0,0,0\nblock min=0,0 max=1,1 top=1");
    CHECK(r.clean());
    CHECK(r.level.world.blocks().size() == 1);
}

// ---------------------------------------------------------------------------
// Malformed input: skipped with a precise diagnostic, never a crash
// ---------------------------------------------------------------------------

TEST_CASE("Each kind of malformed line is skipped with a useful message", "[level][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"wibble x=1",                                           "unknown record type"},
        {"block min=0,0 max=1,1 top",                            "expected key=value"},
        {"block min=0,0 max=1,1 top=",                           "expected key=value"},
        {"block =3 min=0,0 max=1,1 top=1",                       "expected key=value"},
        {"block min=0,0 max=1,1 top=1 top=2",                    "duplicate key"},
        {"block min=0,0 max=1,1 top=abc",                        "not a number"},
        {"block min=0,0 max=1,1 top=nan",                        "not a number"},
        {"block min=0,0 max=1,1 top=inf",                        "not a number"},
        {"block min=0,0,0 max=1,1 top=1",                        "needs 2 comma-separated"},
        {"block min=0,x max=1,1 top=1",                          "bad number"},
        {"block min=0,0 top=1",                                  "missing 'max'"},
        {"block max=1,1 top=1",                                  "missing 'min'"},
        {"block min=0,0 max=1,1",                                "missing 'h0'"},
        {"block min=0,0 max=1,1 h0=1 h1=2",                      "needs 'axis'"},
        {"block min=0,0 max=1,1 h0=1 h1=2 axis=y",               "'axis' must be x or z"},
        {"block min=0,0 max=1,1 top=1 h0=1",                     "either 'top' or"},
        {"block min=0,0 max=1,1 top=1 materal=steel",            "unknown key 'materal'"},
        {"block min=0,0 max=1,1 top=1 material=marshmallow",     "unknown material"},
        {"block min=0,0 max=1,1 top=1 visible=maybe",            "must be 0 or 1"},
        {"block min=0,0 max=1,1 top=1 door=x",                   "not an integer"},
        {"block min=1,0 max=0,1 top=1",                          "'max' must be greater"},
        {"block min=0,0 max=0,1 top=1",                          "'max' must be greater"},
        {"light pos=0,0",                                        "needs 3 comma-separated"},
        {"light pos=0,0,0 radius=0",                             "'radius' must be positive"},
        {"light radius=5",                                       "missing 'pos'"},
        {"target pos=0,0,0 patrol=-1",                           "must not be negative"},
        {"enemy pos=0,0,0",                                      "missing 'type'"},
        {"enemy pos=0,0,0 type=zombie",                          "unknown enemy type"},
        {"enemy pos=0,0,0 type=melee sector=-5",                 "'sector' must be"},
        {"enemy type=melee",                                     "missing 'pos'"},
        {"pickup pos=0,0,0",                                     "missing 'kind'"},
        {"pickup kind=body pos=0,0,0",                           "unknown pickup kind"},
        {"trigger do=complete",                                  "missing 'when'"},
        {"trigger when=sector_cleared sector=0",                 "missing 'do'"},
        {"trigger when=sometimes do=complete",                   "unknown 'when'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=explode",    "unknown 'do'"},
        {"trigger when=enter min=1,1,1 max=0,0,0 do=complete",   "must not be below"},
        {"trigger when=enter do=complete",                       "missing 'min'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=checkpoint", "missing 'point'"},
        {"trigger when=sector_cleared do=complete",              "missing 'sector'"},
        {"trigger when=sector_cleared sector=-1 do=complete",    "'sector' must be >= 0"},
        {"trigger when=sector_cleared sector=0 do=open_door",    "missing 'door'"},
    };

    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = parseLevel(withLine(c.line));
        REQUIRE(r.opened);
        // The base level has 3 lines, so the bad line is line 4.
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(),
                                      [](const LevelIssue& i) { return i.line == 4; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(*hit, c.expect));
        // Nothing from the bad line leaked into the level.
        CHECK(r.level.world.blocks().size() == 1);
        CHECK(r.level.world.lights().empty());
        CHECK(r.level.targets.empty());
        CHECK(r.level.enemies.empty());
        CHECK(r.level.pickups.empty());
        CHECK(r.level.triggers.empty());
    }
}

TEST_CASE("Good lines around a bad one still load, with correct line numbers", "[level][malformed]") {
    const LevelLoad r = parseLevel(
        "level name=mixed\n"                           // 1
        "spawn pos=0,0,0\n"                            // 2
        "block min=-5,-5 max=5,5 top=0\n"              // 3
        "enemy pos=1,0,1 type=melee sector=0\n"        // 4
        "enemy pos=2,0,2 type=zombie sector=0\n"       // 5  bad
        "enemy pos=3,0,3 type=ranged sector=0\n"       // 6
        "this is not a record\n"                       // 7  bad
        "target pos=0,0,4\n");                         // 8
    REQUIRE(r.opened);
    REQUIRE(r.issues.size() == 2);
    CHECK(r.issues[0].line == 5);
    CHECK(r.issues[1].line == 7);
    CHECK(r.level.enemies.size() == 2);
    CHECK(r.level.targets.size() == 1);
    CHECK(r.level.name == "mixed");
    CHECK_FALSE(r.clean());
}

TEST_CASE("Cross-reference checks drop triggers that could never work", "[level][malformed]") {
    SECTION("open_door for a door that does not exist") {
        const LevelLoad r = parseLevel(withLine(
            "enemy pos=0,0,0 type=melee sector=0\n"
            "trigger when=sector_cleared sector=0 do=open_door door=9"));
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "no block has door=9"));
        CHECK(r.level.triggers.empty());
    }
    SECTION("sector_cleared for a sector with no enemies would fire at once") {
        const LevelLoad r = parseLevel(withLine("trigger when=sector_cleared sector=4 do=complete"));
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "no enemy belongs to sector 4"));
        CHECK(r.issues[0].line == 4);
        CHECK(r.level.triggers.empty());
    }
    SECTION("valid references are kept") {
        const LevelLoad r = parseLevel(withLine(
            "block min=0,0 max=1,1 top=3 door=2\n"
            "enemy pos=0,0,0 type=melee sector=1\n"
            "trigger when=sector_cleared sector=1 do=open_door door=2\n"
            "trigger when=enter min=0,0,0 max=1,1,1 do=complete"));
        CHECK(r.clean());
        CHECK(r.level.triggers.size() == 2);
    }
    SECTION("a kept trigger keeps its slot when an earlier one is dropped") {
        const LevelLoad r = parseLevel(withLine(
            "enemy pos=0,0,0 type=melee sector=1\n"
            "trigger when=sector_cleared sector=7 do=complete\n"
            "trigger when=sector_cleared sector=1 do=complete"));
        REQUIRE(r.issues.size() == 1);
        REQUIRE(r.level.triggers.size() == 1);
        CHECK(r.level.triggers[0].sector == 1);
    }
}

TEST_CASE("Level-wide problems are reported without failing the load", "[level][malformed]") {
    SECTION("no spawn keeps the default and says so") {
        const LevelLoad r = parseLevel("block min=0,0 max=1,1 top=1\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(r.issues[0].line == 0);
        CHECK(mentions(r.issues[0], "no 'spawn'"));
        CHECK(near(r.level.spawn, {0.0f, 0.0f, 10.0f}));
    }
    SECTION("a level with no geometry is reported") {
        const LevelLoad r = parseLevel("spawn pos=0,0,0\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "no 'block'"));
    }
    SECTION("duplicate level and spawn records are ignored, the first wins") {
        const LevelLoad r = parseLevel(
            "level name=first\nlevel name=second\n"
            "spawn pos=1,1,1\nspawn pos=9,9,9\n"
            "block min=0,0 max=1,1 top=1\n");
        CHECK(r.issues.size() == 2);
        CHECK(r.level.name == "first");
        CHECK(near(r.level.spawn, {1.0f, 1.0f, 1.0f}));
    }
    SECTION("an empty file") {
        const LevelLoad r = parseLevel("");
        CHECK(r.opened);
        CHECK_FALSE(r.clean());
        CHECK(r.level.world.blocks().empty());
    }
    SECTION("a file that is only comments") {
        const LevelLoad r = parseLevel("# nothing here\n# at all\n");
        CHECK(r.opened);
        CHECK(r.issues.size() == 2);  // no spawn, no blocks
    }
}

TEST_CASE("Hostile and garbage input never crashes the loader", "[level][malformed]") {
    const std::string junk[] = {
        std::string("\0\0\0\xff\xfe=\n=\n==\n", 14),
        std::string(100000, 'x'),
        std::string(5000, '=') + "\n",
        "block " + std::string(20000, ',') + "\n",
        "block min=" + std::string(50, '9') + "99999999999999999999,0 max=1,1 top=1\n",
        "enemy pos=1e999,0,0 type=melee\n",
        "block min=0,0 max=1,1 top=1e39\n",
        "\n\n\n\n\n#\n#\n \t \n",
        "level\nspawn\nblock\nlight\ntarget\nenemy\npickup\ntrigger\n",
    };
    for (const std::string& text : junk) {
        const LevelLoad r = parseLevel(text);
        CHECK(r.opened);
        CHECK_FALSE(r.clean());
    }
}

TEST_CASE("Loading from disk", "[level][io]") {
    SECTION("a missing file is reported, not thrown") {
        const LevelLoad r = loadLevelFile("definitely/not/a/real/level.lvl");
        CHECK_FALSE(r.opened);
        CHECK_FALSE(r.clean());
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "cannot open"));
        CHECK(r.level.world.blocks().empty());
    }
    SECTION("a written file round-trips through the loader") {
        const std::string path = "carnage_test_level.lvl";
        {
            std::ofstream f(path);
            f << "level name=on_disk\nspawn pos=3,0,4\nblock min=0,0 max=2,2 top=1\n";
        }
        const LevelLoad r = loadLevelFile(path);
        std::remove(path.c_str());
        CHECK(r.clean());
        CHECK(r.level.name == "on_disk");
        CHECK(near(r.level.spawn, {3.0f, 0.0f, 4.0f}));
        CHECK(r.level.world.blocks().size() == 1);
    }
}
