#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Level.hpp"
#include "game/Movement.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
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
    CHECK(L.triggers.size() == 16);  // 3 objectives + 2 checkpoints + 11 prompts

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

    // Triggers: three sector objectives, two checkpoints and the tutorial prompts.
    int cleared = 0, checkpoints = 0, completes = 0, opens = 0, prompts = 0;
    for (const TriggerVolume& t : L.triggers) {
        cleared     += t.condition == TriggerCondition::SectorCleared;
        checkpoints += t.action == TriggerAction::Checkpoint;
        completes   += t.action == TriggerAction::Complete;
        opens       += t.action == TriggerAction::OpenDoor;
        prompts     += t.action == TriggerAction::Prompt;
    }
    CHECK(cleared == 3);
    CHECK(checkpoints == 2);
    CHECK(completes == 1);
    CHECK(opens == 2);
    CHECK(prompts == 11);

    // Until a briefing/hub exists, completing the level returns to the menu.
    CHECK(L.transition == TransitionType::Menu);
    CHECK(L.nextLevel.empty());
}

TEST_CASE("The shipped level's tutorial prompts are well placed", "[level][prompts]") {
    const LevelLoad r = loadLevelFile(kShipped);
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    std::vector<const TriggerVolume*> prompts;
    for (const TriggerVolume& t : L.triggers)
        if (t.action == TriggerAction::Prompt) prompts.push_back(&t);
    REQUIRE(prompts.size() == 11);

    const auto overlap = [](const TriggerVolume& a, const TriggerVolume& b) {
        return a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y && b.min.y <= a.max.y &&
               a.min.z <= b.max.z && b.min.z <= a.max.z;
    };
    for (std::size_t i = 0; i < prompts.size(); ++i) {
        const TriggerVolume& a = *prompts[i];
        CHECK(a.condition == TriggerCondition::PlayerEnters);
        CHECK_FALSE(a.text.empty());
        CHECK(checkPromptText(a.text).empty());
        // A player standing on the floor in the middle of the volume must be inside it.
        const glm::vec3 mid = (a.min + a.max) * 0.5f;
        const auto g = L.world.ground({mid.x, mid.z}, a.max.y);
        REQUIRE(g.has_value());
        CHECK(g->height >= a.min.y);
        CHECK(g->height <= a.max.y);
        // Volumes must not overlap: two prompts firing together would drown one out.
        for (std::size_t j = i + 1; j < prompts.size(); ++j)
            CHECK_FALSE(overlap(a, *prompts[j]));
    }
    CHECK(prompts.front()->contains(L.spawn));  // the first hint shows as soon as play starts

    // Each teaching prompt names a real action the player can do, and each mechanic is covered.
    bool vault = false, slide = false, wallRun = false, grab = false, thrown = false;
    for (const TriggerVolume* t : prompts) {
        vault   = vault   || t->clearOn == PromptClear::Vault;
        slide   = slide   || t->clearOn == PromptClear::Slide;
        wallRun = wallRun || t->clearOn == PromptClear::WallRun;
        grab    = grab    || t->clearOn == PromptClear::Grab;
        thrown  = thrown  || t->clearOn == PromptClear::Throw;
    }
    CHECK(vault);
    CHECK(slide);
    CHECK(wallRun);
    CHECK(grab);
    CHECK(thrown);
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
    CHECK(triggers.volumes().size() == 16);
    CHECK(triggers.activeSector() == 0);

    const auto alive = [&](int s) { return enemies.aliveInSector(combat, s); };
    const auto killSector = [&](int s) {
        for (const Enemy& e : enemies.enemies())
            if (e.sector == s) combat.targetAt(e.target)->hp = 0.0f;
    };
    const glm::vec3 spawn = L.spawn;

    // Prompts fire as the player walks around; these tests care about objectives.
    const auto objectives = [](const std::vector<TriggerEvent>& all) {
        std::vector<TriggerEvent> out;
        for (const TriggerEvent& e : all)
            if (e.action != TriggerAction::Prompt) out.push_back(e);
        return out;
    };

    SECTION("objectives fire in order: door, gate, then level complete") {
        // Starting play shows the first tutorial prompt and nothing else.
        const auto start = triggers.update(spawn, alive);
        REQUIRE(start.size() == 1);
        CHECK(start[0].action == TriggerAction::Prompt);
        CHECK(start[0].text.find("{forward}") != std::string::npos);  // unexpanded: the game expands it

        killSector(0);
        auto ev = objectives(triggers.update(spawn, alive));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::OpenDoor);
        CHECK(ev[0].param == 0);
        CHECK(triggers.activeSector() == 1);

        killSector(1);
        ev = objectives(triggers.update(spawn, alive));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::OpenDoor);
        CHECK(ev[0].param == 1);

        killSector(2);
        ev = objectives(triggers.update(spawn, alive));
        REQUIRE(ev.size() == 1);
        CHECK(ev[0].action == TriggerAction::Complete);
        CHECK(triggers.activeSector() == -1);
        CHECK(objectives(triggers.update(spawn, alive)).empty());  // fires once
    }
    SECTION("walking into the arena and pit ledge sets checkpoints, once each") {
        const auto arena = objectives(triggers.update({0.0f, 0.0f, -80.0f}, alive));
        REQUIRE(arena.size() == 1);
        CHECK(arena[0].action == TriggerAction::Checkpoint);
        CHECK(near(arena[0].point, {0.0f, 0.0f, -80.0f}));
        CHECK(objectives(triggers.update({0.0f, 0.0f, -80.0f}, alive)).empty());

        const auto pit = objectives(triggers.update({5.0f, 0.0f, -133.0f}, alive));
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
        // prompts
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt",                         "missing 'text'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"\"",               "prompt text is empty"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"PRESS {nope}\"",   "unknown placeholder '{nope}'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"PRESS {jump\"",    "unterminated '{'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"JUMP}\"",          "stray '}'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"COST $5\"",        "cannot be drawn"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"JUMP",             "unterminated quote"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"JUMP\"x",          "after the closing quote"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=HELLO time=0",       "'time' must be between"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=HELLO time=99",      "'time' must be between"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=HELLO clear_on=dance", "unknown 'clear_on'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=complete text=HELLO",            "unknown key 'text'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=checkpoint point=0,0,0 time=3",  "unknown key 'time'"},
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

// ---------------------------------------------------------------------------
// Prompt triggers
// ---------------------------------------------------------------------------

TEST_CASE("Prompt triggers parse their text, timing and clear action", "[level][prompts][parse]") {
    const LevelLoad r = parseLevel(withLine(
        "trigger when=enter min=0,0,0 max=2,2,2 do=prompt text=\"JUMP: {jump}  # not a comment\" time=6 clear_on=jump  # a comment\n"
        "trigger when=enter min=5,0,0 max=7,2,2 do=prompt text=SHORT\n"
        "trigger when=enter min=9,0,0 max=11,2,2 do=prompt text=\"A\" clear_on=none\n"
        "enemy pos=0,0,0 type=melee sector=0\n"
        "trigger when=sector_cleared sector=0 do=prompt text=\"WELL DONE\" time=3"));
    REQUIRE(r.clean());
    REQUIRE(r.level.triggers.size() == 4);

    const TriggerVolume& a = r.level.triggers[0];
    CHECK(a.action == TriggerAction::Prompt);
    CHECK(a.text == "JUMP: {jump}  # not a comment");  // '#' inside quotes is text
    CHECK(a.time == 6.0f);
    CHECK(a.clearOn == PromptClear::Jump);

    const TriggerVolume& b = r.level.triggers[1];
    CHECK(b.text == "SHORT");
    CHECK(b.time == 5.0f);                       // default
    CHECK(b.clearOn == PromptClear::Timer);      // default
    CHECK(r.level.triggers[2].clearOn == PromptClear::Timer);  // explicit none

    const TriggerVolume& c = r.level.triggers[3];  // prompts work on any condition
    CHECK(c.condition == TriggerCondition::SectorCleared);
    CHECK(c.text == "WELL DONE");
}

TEST_CASE("Every clear_on name maps to its action", "[level][prompts][parse]") {
    struct Row { const char* name; PromptClear want; };
    const Row rows[] = {
        {"none", PromptClear::Timer},     {"jump", PromptClear::Jump},   {"slide", PromptClear::Slide},
        {"vault", PromptClear::Vault},    {"wall_run", PromptClear::WallRun}, {"fire", PromptClear::Fire},
        {"grab", PromptClear::Grab},      {"throw", PromptClear::Throw},
    };
    for (const Row& row : rows) {
        INFO(row.name);
        const LevelLoad r = parseLevel(withLine(
            std::string("trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=HI clear_on=") + row.name));
        REQUIRE(r.clean());
        REQUIRE(r.level.triggers.size() == 1);
        CHECK(r.level.triggers[0].clearOn == row.want);
    }
}

TEST_CASE("Prompt text is limited to what fits on screen", "[level][prompts][malformed]") {
    const std::string ok(kMaxPromptChars, 'A');
    const std::string tooLong(kMaxPromptChars + 1, 'A');
    CHECK(parseLevel(withLine("trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=" + ok)).clean());

    const LevelLoad r = parseLevel(withLine("trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=" + tooLong));
    REQUIRE(r.issues.size() == 1);
    CHECK(mentions(r.issues[0], "too long"));
    CHECK(r.level.triggers.empty());

    // The limit applies to what is shown, i.e. after {keys} are filled in.
    const std::string expands = "{rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb} {rmb}";  // 71 chars raw
    CHECK(expands.size() < kMaxPromptChars);
    const LevelLoad r2 = parseLevel(withLine("trigger when=enter min=0,0,0 max=1,1,1 do=prompt text=\"" + expands + "\""));
    REQUIRE(r2.issues.size() == 1);
    CHECK(mentions(r2.issues[0], "too long"));
}

// ---------------------------------------------------------------------------
// Level transitions
// ---------------------------------------------------------------------------

TEST_CASE("Level ids and paths", "[level][transition]") {
    CHECK(validLevelId("level01_escape"));
    CHECK(validLevelId("kodiak_hub"));
    CHECK(validLevelId("a"));
    CHECK_FALSE(validLevelId(""));
    CHECK_FALSE(validLevelId("Level01"));          // upper case
    CHECK_FALSE(validLevelId("../etc/passwd"));    // no path traversal
    CHECK_FALSE(validLevelId("level 01"));
    CHECK_FALSE(validLevelId("level01.lvl"));
    CHECK_FALSE(validLevelId("a/b"));
    CHECK_FALSE(validLevelId(std::string(65, 'a')));
    CHECK(validLevelId(std::string(64, 'a')));

    CHECK(levelPath("/game/assets", "level02") == "/game/assets/levels/level02.lvl");
}

TEST_CASE("Transition metadata parses and defaults sensibly", "[level][transition][parse]") {
    SECTION("no metadata: back to the menu") {
        const LevelLoad r = parseLevel("level name=x\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE(r.clean());
        CHECK(r.level.transition == TransitionType::Menu);
        CHECK(r.level.nextLevel.empty());
        const LevelTransition t = completionTransition(r.level);
        CHECK(t.type == TransitionType::Menu);
        CHECK(t.next.empty());
    }
    SECTION("next_level alone implies a level transition") {
        const LevelLoad r = parseLevel("level name=x next_level=level02\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE(r.clean());
        CHECK(r.level.transition == TransitionType::Level);
        CHECK(r.level.nextLevel == "level02");
        const LevelTransition t = completionTransition(r.level);
        CHECK(t.type == TransitionType::Level);
        CHECK(t.next == "level02");
    }
    SECTION("explicit transition_type") {
        const LevelLoad a = parseLevel("level name=x next_level=hub transition_type=level\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE(a.clean());
        CHECK(a.level.transition == TransitionType::Level);
        const LevelLoad b = parseLevel("level name=x transition_type=menu\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE(b.clean());
        CHECK(b.level.transition == TransitionType::Menu);
    }
}

TEST_CASE("Bad transition metadata is reported and ignored", "[level][transition][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"level name=x next_level=Level02",                          "must be a level id"},
        {"level name=x next_level=../secrets",                       "must be a level id"},
        {"level name=x next_level=a/b",                              "must be a level id"},
        {"level name=x transition_type=level",                       "needs 'next_level'"},
        {"level name=x next_level=hub transition_type=menu",         "would ignore it"},
        {"level name=x transition_type=teleport",                    "unknown transition_type"},
        {"level next_level=hub",                                     "missing 'name'"},
    };
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = parseLevel(std::string(c.line) + "\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(r.issues[0].line == 1);
        CHECK(mentions(r.issues[0], c.expect));
        // The whole record was rejected: nothing half-applied.
        CHECK(r.level.name == "unnamed");
        CHECK(r.level.nextLevel.empty());
        CHECK(completionTransition(r.level).type == TransitionType::Menu);
    }
}

TEST_CASE("completionTransition never points at an invalid level", "[level][transition]") {
    LevelData d;
    d.transition = TransitionType::Level;
    d.nextLevel  = "../bad";
    CHECK(completionTransition(d).type == TransitionType::Menu);
    d.nextLevel.clear();
    CHECK(completionTransition(d).type == TransitionType::Menu);
    d.nextLevel = "ok_level";
    CHECK(completionTransition(d).type == TransitionType::Level);
}

TEST_CASE("Completing a level chains into the next one from disk", "[level][transition][io]") {
    namespace fs = std::filesystem;
    const fs::path root = "carnage_test_assets";
    fs::create_directories(root / "levels");
    const auto write = [&](const char* id, const char* body) {
        std::ofstream(root / "levels" / (std::string(id) + ".lvl")) << body;
    };
    write("first",  "level name=first next_level=second\nspawn pos=0,0,0\nblock min=-5,-5 max=5,5 top=0\n"
                    "enemy pos=0,0,0 type=melee sector=0\ntrigger when=sector_cleared sector=0 do=complete\n");
    write("second", "level name=second\nspawn pos=3,0,3\nblock min=-9,-9 max=9,9 top=2\n");

    const std::string assets = root.string();
    LevelLoad current = loadLevelFile(levelPath(assets, "first"));
    REQUIRE(current.clean());
    CHECK(current.level.name == "first");

    const LevelTransition t = completionTransition(current.level);
    REQUIRE(t.type == TransitionType::Level);
    LevelLoad next = loadLevelFile(levelPath(assets, t.next));
    REQUIRE(next.clean());
    CHECK(next.level.name == "second");
    CHECK(completionTransition(next.level).type == TransitionType::Menu);  // the chain ends

    SECTION("a missing next level is detectable, so the game can fall back to the menu") {
        write("dead_end", "level name=dead_end next_level=does_not_exist\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        const LevelLoad d = loadLevelFile(levelPath(assets, "dead_end"));
        REQUIRE(d.clean());
        const LevelTransition dt = completionTransition(d.level);
        CHECK(dt.type == TransitionType::Level);
        CHECK_FALSE(loadLevelFile(levelPath(assets, dt.next)).opened);
    }
    fs::remove_all(root);
}

TEST_CASE("Replacing a loaded level in place keeps existing references valid", "[level][transition]") {
    // main.cpp keeps `World&`, Combat and MovementController bound to loaded.level.world
    // and swaps levels by assigning to `loaded`. That only works if the address is stable.
    LevelLoad loaded = parseLevel("level name=a\nspawn pos=0,0,0\nblock min=-30,-5 max=30,5 top=0\n"
                                  "block min=10,-5 max=11,5 top=4\n");
    REQUIRE(loaded.clean());
    World& world = loaded.level.world;
    const World* before = &world;
    Combat combat(world);
    MovementController player(world);
    CHECK(world.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 50.0f)->t == Approx(10.0f));

    loaded = parseLevel("level name=b\nspawn pos=0,0,0\nblock min=-30,-5 max=30,5 top=0\n"
                        "block min=20,-5 max=21,5 top=4\n");
    REQUIRE(loaded.clean());
    CHECK(loaded.level.name == "b");
    CHECK(&world == before);
    CHECK(&combat.world() == before);
    CHECK(world.blocks().size() == 2);
    CHECK(world.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 50.0f)->t == Approx(20.0f));  // new geometry

    player.respawn({0.0f, 0.0f, 0.0f});
    MoveInput in;
    in.move = {1.0f, 0.0f};
    for (int i = 0; i < 600; ++i) player.tick(kDt, 0.0f, in);
    CHECK(player.position().x > 12.0f);  // walked through where the old wall (x 10..11) was
}

