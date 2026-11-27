#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Camera.hpp"
#include "game/LevelGraph.hpp"
#include "game/Movement.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

constexpr const char* kAssets = CARNAGE_ASSET_DIR;

bool mentions(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }
bool anyMentions(const std::vector<std::string>& v, const std::string& needle) {
    return std::any_of(v.begin(), v.end(), [&](const std::string& s) { return mentions(s, needle); });
}

// A scratch assets directory with levels/ and briefings/, removed on destruction.
struct TempAssets {
    fs::path root;
    explicit TempAssets(const std::string& name) : root("carnage_test_" + name) {
        fs::remove_all(root);
        fs::create_directories(root / "levels");
        fs::create_directories(root / "briefings");
    }
    ~TempAssets() { fs::remove_all(root); }
    std::string dir() const { return root.string(); }

    // A minimal valid level `id`; `header` adds keys to the level record, `extra` adds lines.
    void level(const std::string& id, const std::string& extra = {}, const std::string& header = {},
               const std::string& nameOverride = {}) const {
        std::ofstream(root / "levels" / (id + ".lvl"))
            << "level name=" << (nameOverride.empty() ? id : nameOverride) << (header.empty() ? "" : " " + header) << "\n"
            << "spawn pos=0,0,0\nblock min=-5,-5 max=5,5 top=0\n" << extra << "\n";
    }
    void file(const std::string& relative, const std::string& body) const {
        fs::create_directories((root / relative).parent_path());
        std::ofstream(root / relative) << body;
    }
};

std::string portal(const std::string& to, const std::string& spawn = {}) {
    return "trigger when=enter min=1,-1,1 max=2,3,2 do=portal level=" + to + (spawn.empty() ? "" : " spawn=" + spawn) + "\n";
}

// Walks the player through a real level file with its real triggers and doors.
struct Walker {
    LevelLoad       load;
    TriggerSystem   triggers;
    MovementController player;
    std::optional<TriggerEvent> portal;  // the first portal event seen
    int             doorsOpened = 0;
    float           seconds = 0.0f;

    explicit Walker(const std::string& id, const std::string& spawnName = {})
        : load(loadLevelFile(levelPath(kAssets, id))), player(load.level.world) {
        REQUIRE(load.opened);
        installTriggers(load.level, triggers);
        const SpawnPoint sp = load.level.spawnPoint(spawnName);
        player.respawn(sp.pos);
    }
    Walker(const Walker&) = delete;
    Walker& operator=(const Walker&) = delete;

    void step(float yaw, bool applyTriggers = true) {
        MoveInput in;
        in.move = {0.0f, 1.0f};
        player.tick(kDt, yaw, in);
        seconds += kDt;
        if (!applyTriggers) return;
        for (const TriggerEvent& e : triggers.update(player.position(), [](int) { return 1; })) {
            if (e.action == TriggerAction::OpenDoor) { load.level.world.setDoorOpen(e.param, true); ++doorsOpened; }
            if (e.action == TriggerAction::Portal && !portal) portal = e;
        }
    }
    // Walks until `done` or `maxSeconds`.
    template <class Done>
    void walk(float yaw, float maxSeconds, Done&& done, bool applyTriggers = true) {
        while (seconds < maxSeconds && !done(*this)) step(yaw, applyTriggers);
    }
};

float yawToward(glm::vec2 from, glm::vec2 to) {  // engine yaw that walks from `from` to `to` on the XZ plane
    const glm::vec2 d = to - from;
    return std::atan2(-d.x, -d.y);
}

} // namespace

// ---------------------------------------------------------------------------
// Spawn points
// ---------------------------------------------------------------------------

TEST_CASE("Named spawn points parse, with yaw in degrees", "[hub][spawn][parse]") {
    const LevelLoad r = parseLevel(
        "level name=x\n"
        "spawn pos=1,0,2 yaw=90\n"
        "spawn name=from_a pos=3,0.5,4 yaw=-90\n"
        "spawn name=from_b pos=5,0,6\n"
        "block min=-9,-9 max=9,9 top=0\n");
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(near(L.spawn, {1.0f, 0.0f, 2.0f}));
    CHECK(L.spawnYaw == Approx(glm::radians(90.0f)));
    REQUIRE(L.spawns.size() == 2);
    CHECK(L.spawns[0].name == "from_a");
    CHECK(near(L.spawns[0].point.pos, {3.0f, 0.5f, 4.0f}));
    CHECK(L.spawns[0].point.yaw == Approx(glm::radians(-90.0f)));
    CHECK(L.spawns[1].point.yaw == 0.0f);  // default facing
}

TEST_CASE("spawnPoint picks the named spawn, else the default", "[hub][spawn]") {
    const LevelLoad r = parseLevel(
        "level name=x\nspawn pos=1,0,1 yaw=10\nspawn name=door pos=9,0,9 yaw=20\nblock min=-9,-9 max=9,9 top=0\n");
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(L.hasSpawn("door"));
    CHECK_FALSE(L.hasSpawn("nope"));
    CHECK_FALSE(L.hasSpawn(""));
    CHECK_FALSE(L.hasSpawn("default"));   // the unnamed spawn is not addressable by name

    const SpawnPoint named = L.spawnPoint("door");
    CHECK(near(named.pos, {9.0f, 0.0f, 9.0f}));
    CHECK(named.yaw == Approx(glm::radians(20.0f)));

    for (const char* fallback : {"", "nope", "default", "DOOR"}) {
        INFO("name: '" << fallback << "'");
        const SpawnPoint p = L.spawnPoint(fallback);
        CHECK(near(p.pos, {1.0f, 0.0f, 1.0f}));
        CHECK(p.yaw == Approx(glm::radians(10.0f)));
    }
    const SpawnPoint dflt = L.spawnPoint();
    CHECK(near(dflt.pos, {1.0f, 0.0f, 1.0f}));
}

TEST_CASE("Bad spawn records are reported and ignored", "[hub][spawn][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"spawn",                              "missing 'pos'"},
        {"spawn pos=0,0",                      "needs 3 comma-separated"},
        {"spawn pos=0,0,0 yaw=left",           "not a number"},
        {"spawn pos=0,0,0 yaw=nan",            "not a number"},
        {"spawn name=Bad_Name pos=0,0,0",      "'name' must be an id"},
        {"spawn name=a/b pos=0,0,0",           "'name' must be an id"},
        {"spawn name=default pos=0,0,0",       "reserved"},
        {"spawn pos=0,0,0 colour=red",         "unknown key 'colour'"},
        {"spawn name=a pos=1,1,1",             "duplicate spawn name 'a'"},   // `a` is defined on line 4
        {"spawn pos=7,7,7",                    "duplicate default spawn"},
    };
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = parseLevel(std::string("level name=x\nspawn pos=1,0,1\nblock min=-9,-9 max=9,9 top=0\n"
                                                   "spawn name=a pos=2,0,2\n") + c.line + "\n");
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(), [](const LevelIssue& i) { return i.line == 5; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(hit->message, c.expect));
        CHECK(near(r.level.spawn, {1.0f, 0.0f, 1.0f}));  // the good default survived
        CHECK(r.level.spawns.size() == 1);               // and so did the one good named spawn
    }
}

TEST_CASE("A level with only named spawns is reported but still loads", "[hub][spawn][malformed]") {
    const LevelLoad r = parseLevel("level name=x\nspawn name=a pos=1,0,1\nblock min=-9,-9 max=9,9 top=0\n");
    REQUIRE(r.issues.size() == 1);
    CHECK(mentions(r.issues[0].message, "no 'spawn'"));
    CHECK(r.level.hasSpawn("a"));
    CHECK(near(r.level.spawnPoint("").pos, {0.0f, 0.0f, 10.0f}));  // the engine default
}

// ---------------------------------------------------------------------------
// Portals and arrival spawns
// ---------------------------------------------------------------------------

TEST_CASE("Portal triggers parse", "[hub][portal][parse]") {
    const LevelLoad r = parseLevel(
        "level name=x\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n"
        "trigger when=enter min=0,0,0 max=1,2,1 do=portal level=kodiak_range\n"
        "trigger when=enter min=2,0,0 max=3,2,1 do=portal level=kodiak_hub spawn=from_x\n");
    REQUIRE(r.clean());
    REQUIRE(r.level.triggers.size() == 2);
    const TriggerVolume& a = r.level.triggers[0];
    CHECK(a.action == TriggerAction::Portal);
    CHECK(a.target == "kodiak_range");
    CHECK(a.targetSpawn.empty());
    CHECK(r.level.triggers[1].targetSpawn == "from_x");
}

TEST_CASE("Bad portals are reported and dropped", "[hub][portal][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"trigger when=enter min=0,0,0 max=1,1,1 do=portal",                              "missing 'level'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=portal level=Hub",                   "'level' must be a level id"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=portal level=../hub",                "'level' must be a level id"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=portal level=hub spawn=A",           "'spawn' must be a spawn name"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=portal level=hub text=HI",           "unknown key 'text'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=complete level=hub",                 "unknown key 'level'"},
        {"trigger when=enter min=0,0,0 max=1,1,1 do=checkpoint point=0,0,0 spawn=a",     "unknown key 'spawn'"},
    };
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const LevelLoad r = parseLevel(std::string("level name=x\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n") + c.line + "\n");
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(), [](const LevelIssue& i) { return i.line == 4; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(hit->message, c.expect));
        CHECK(r.level.triggers.empty());
    }
}

TEST_CASE("next_spawn names the arrival point after a level ends", "[hub][transition][parse]") {
    const LevelLoad r = parseLevel(
        "level name=x next_level=hub next_spawn=from_x\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n");
    REQUIRE(r.clean());
    CHECK(r.level.nextSpawn == "from_x");
    const LevelTransition t = completionTransition(r.level);
    CHECK(t.type == TransitionType::Level);
    CHECK(t.next == "hub");
    CHECK(t.spawn == "from_x");

    SECTION("it is optional") {
        const LevelLoad r2 = parseLevel("level name=x next_level=hub\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n");
        REQUIRE(r2.clean());
        CHECK(completionTransition(r2.level).spawn.empty());
    }
    SECTION("it does not outlive a transition that goes to the menu") {
        LevelData d;
        d.nextSpawn = "x";
        CHECK(completionTransition(d).spawn.empty());
    }
}

TEST_CASE("Bad next_spawn values are rejected", "[hub][transition][malformed]") {
    for (const char* bad : {"level name=x next_spawn=a", "level name=x next_level=hub next_spawn=A",
                            "level name=x next_level=hub next_spawn=a/b"}) {
        INFO(bad);
        const LevelLoad r = parseLevel(std::string(bad) + "\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n");
        REQUIRE_FALSE(r.issues.empty());
        CHECK(r.issues[0].line == 1);
        CHECK(r.level.nextSpawn.empty());
        CHECK(r.level.name == "unnamed");  // the whole record was rejected
    }
}

TEST_CASE("levelLinks lists next_level then portals in file order", "[hub][portal]") {
    const LevelLoad r = parseLevel(
        "level name=x next_level=c next_spawn=s\nspawn pos=0,0,0\nblock min=-9,-9 max=9,9 top=0\n"
        "trigger when=enter min=0,0,0 max=1,1,1 do=complete\n"
        "trigger when=enter min=0,0,0 max=1,1,1 do=portal level=a\n"
        "trigger when=enter min=0,0,0 max=1,1,1 do=portal level=b spawn=t\n");
    REQUIRE(r.clean());
    const auto links = levelLinks(r.level);
    REQUIRE(links.size() == 3);
    CHECK(links[0].kind == LevelLink::Kind::Next);
    CHECK(links[0].to == "c");
    CHECK(links[0].spawn == "s");
    CHECK(links[1].kind == LevelLink::Kind::Portal);
    CHECK(links[1].to == "a");
    CHECK(links[2].to == "b");
    CHECK(links[2].spawn == "t");

    CHECK(levelLinks(parseLevel("level name=x\nspawn pos=0,0,0\nblock min=-1,-1 max=1,1 top=0\n").level).empty());
}

// ---------------------------------------------------------------------------
// The link checker
// ---------------------------------------------------------------------------

TEST_CASE("A consistent level network has no problems", "[hub][graph]") {
    TempAssets t("graph_ok");
    t.level("alpha", portal("beta", "from_alpha"), "next_level=beta");
    t.level("beta", portal("alpha") + "spawn name=from_alpha pos=3,0,3\n");

    const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
    for (const std::string& p : r.problems) FAIL(p);
    CHECK(r.ok());
    CHECK(r.levels == std::vector<std::string>{"alpha", "beta"});
}

TEST_CASE("The link checker reports every kind of broken link", "[hub][graph][malformed]") {
    TempAssets t("graph_bad");

    SECTION("a portal to a level that does not exist") {
        t.level("alpha", portal("ghost") + portal("alpha"));
        const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
        CHECK_FALSE(r.ok());
        CHECK(anyMentions(r.problems, "level 'ghost' cannot be opened"));
        CHECK(anyMentions(r.problems, "linked from 'alpha'"));
    }
    SECTION("an arrival spawn that the target does not have") {
        t.level("alpha", portal("beta", "no_such_spawn") + portal("alpha"));
        t.level("beta", portal("alpha"));
        const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
        REQUIRE(r.problems.size() == 1);
        CHECK(mentions(r.problems[0], "no_such_spawn"));
        CHECK(mentions(r.problems[0], "'alpha'"));
        CHECK(mentions(r.problems[0], "'beta'"));
    }
    SECTION("next_spawn is checked too") {
        t.level("alpha", "", "next_level=beta next_spawn=lost");
        t.level("beta", portal("alpha"));
        const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
        CHECK(anyMentions(r.problems, "spawn 'lost' in 'beta'"));
    }
    SECTION("a level that is a dead end (no way back to the start)") {
        t.level("alpha", portal("stranded"));
        t.level("stranded");
        const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
        CHECK(anyMentions(r.problems, "level 'stranded' has no way back to 'alpha'"));
        CHECK(checkLevelGraph(t.dir(), "alpha", /*requireReturn=*/false).ok());
    }
    SECTION("the level file declares a different name than its id") {
        t.level("alpha", "", "", "something_else");
        CHECK(anyMentions(checkLevelGraph(t.dir(), "alpha").problems, "declares name 'something_else'"));
    }
    SECTION("a malformed line in any reachable level surfaces with its line number") {
        t.level("alpha", portal("beta") + portal("alpha"));
        t.level("beta", portal("alpha") + "block min=0,0 max=0,0 top=1\n");  // line 5: max not greater than min
        const LevelGraphReport r = checkLevelGraph(t.dir(), "alpha");
        CHECK(anyMentions(r.problems, "level 'beta', line 5"));
    }
    SECTION("an invalid start id") {
        const LevelGraphReport r = checkLevelGraph(t.dir(), "../etc");
        REQUIRE(r.problems.size() == 1);
        CHECK(r.levels.empty());
    }
    SECTION("a missing start level") {
        CHECK(anyMentions(checkLevelGraph(t.dir(), "nowhere").problems, "cannot be opened"));
    }
}

TEST_CASE("The link checker checks briefings", "[hub][graph][briefing]") {
    TempAssets t("graph_brief");
    t.level("alpha", portal("alpha"), "briefing=intro");

    SECTION("missing briefing file") {
        CHECK(anyMentions(checkLevelGraph(t.dir(), "alpha").problems, "briefing 'intro' cannot be opened"));
    }
    SECTION("a briefing with no beats") {
        t.file("briefings/intro.brf", "briefing name=intro\n");
        CHECK(anyMentions(checkLevelGraph(t.dir(), "alpha").problems, "has no beats"));
    }
    SECTION("a briefing with a missing image") {
        t.file("briefings/intro.brf", "beat text=HI image=briefings/gone.png\n");
        CHECK(anyMentions(checkLevelGraph(t.dir(), "alpha").problems, "gone.png"));
    }
    SECTION("a good briefing") {
        t.file("briefings/intro.brf", "beat text=HI\n");
        CHECK(checkLevelGraph(t.dir(), "alpha").ok());
    }
}

TEST_CASE("The link checker handles cycles and runaway networks", "[hub][graph]") {
    TempAssets t("graph_cycle");

    SECTION("a level that links to itself and to a neighbour that links back") {
        t.level("a", portal("a") + portal("b"));
        t.level("b", portal("a") + portal("b"));
        const LevelGraphReport r = checkLevelGraph(t.dir(), "a");
        CHECK(r.ok());
        CHECK(r.levels.size() == 2);
    }
    SECTION("a very long chain stops instead of looping forever") {
        for (int i = 0; i < 300; ++i)
            t.level("l" + std::to_string(i), portal("l" + std::to_string(i + 1)));
        const LevelGraphReport r = checkLevelGraph(t.dir(), "l0", /*requireReturn=*/false);
        CHECK(r.levels.size() <= 256);
        CHECK(anyMentions(r.problems, "more than 256 levels"));
    }
}

// ---------------------------------------------------------------------------
// The shipped Kodiak network
// ---------------------------------------------------------------------------

TEST_CASE("The shipped levels form one consistent, fully connected network", "[hub][graph]") {
    const LevelGraphReport r = checkLevelGraph(kAssets, "level01_escape");
    for (const std::string& p : r.problems) FAIL(p);
    CHECK(r.ok());
    CHECK(std::set<std::string>(r.levels.begin(), r.levels.end()) ==
          std::set<std::string>{"level01_escape", "kodiak_hub", "kodiak_range"});

    // The same network seen from the hub (every level can reach every other one).
    const LevelGraphReport fromHub = checkLevelGraph(kAssets, "kodiak_hub");
    CHECK(fromHub.ok());
    CHECK(fromHub.levels.size() == 3);
}

TEST_CASE("The routes between the shipped levels land on real spawn points", "[hub][graph][routing]") {
    const LevelLoad l01   = loadLevelFile(levelPath(kAssets, "level01_escape"));
    const LevelLoad hub   = loadLevelFile(levelPath(kAssets, "kodiak_hub"));
    const LevelLoad range = loadLevelFile(levelPath(kAssets, "kodiak_range"));
    REQUIRE(l01.clean());
    REQUIRE(hub.clean());
    REQUIRE(range.clean());

    // Level 01 -> (briefing) -> hub landing pad.
    const LevelTransition done = completionTransition(l01.level);
    CHECK(done.briefing == "level01_outro");
    CHECK(done.type == TransitionType::Level);
    CHECK(done.next == "kodiak_hub");
    REQUIRE(hub.level.hasSpawn(done.spawn));
    CHECK(done.spawn == "from_level01_escape");
    const SpawnPoint landing = hub.level.spawnPoint(done.spawn);
    CHECK(near(landing.pos, {0.0f, 0.2f, 40.0f}));
    CHECK(landing.yaw == Approx(0.0f));

    // Hub portals: two selectable missions.
    std::set<std::string> missions;
    for (const LevelLink& l : levelLinks(hub.level)) {
        CHECK(l.kind == LevelLink::Kind::Portal);
        missions.insert(l.to);
    }
    CHECK(missions == std::set<std::string>{"level01_escape", "kodiak_range"});

    // Range -> hub: arrives in the east yard, not on the landing pad.
    const auto back = levelLinks(range.level);
    REQUIRE(back.size() == 1);
    CHECK(back[0].to == "kodiak_hub");
    REQUIRE(hub.level.hasSpawn(back[0].spawn));
    const SpawnPoint yard = hub.level.spawnPoint(back[0].spawn);
    CHECK(yard.pos.x > 16.0f);                       // east of the yard wall
    CHECK(yard.yaw == Approx(glm::radians(90.0f)));  // facing west, away from the gate
    CHECK(glm::distance(yard.pos, landing.pos) > 40.0f);
}

TEST_CASE("The hub has the zones, doors, gates and checkpoints it promises", "[hub][shipped]") {
    const LevelLoad r = loadLevelFile(levelPath(kAssets, "kodiak_hub"));
    REQUIRE(r.clean());
    const LevelData& L = r.level;

    CHECK(L.name == "kodiak_hub");
    CHECK(L.world.doorIds() == std::vector<int>{0, 1});
    CHECK(L.enemies.empty());     // a safe staging area
    CHECK(L.spawns.size() == 2);

    int portals = 0, checkpoints = 0, prompts = 0, opens = 0;
    for (const TriggerVolume& t : L.triggers) {
        portals     += t.action == TriggerAction::Portal;
        checkpoints += t.action == TriggerAction::Checkpoint;
        prompts     += t.action == TriggerAction::Prompt;
        opens       += t.action == TriggerAction::OpenDoor;
    }
    CHECK(portals == 2);
    CHECK(checkpoints == 4);   // landing pad, plaza, command post, east yard
    CHECK(prompts == 4);
    CHECK(opens == 4);         // each door opens from both sides

    // Every door has an `enter` trigger that opens it.
    for (const int door : L.world.doorIds()) {
        INFO("door " << door);
        const bool opener = std::any_of(L.triggers.begin(), L.triggers.end(), [&](const TriggerVolume& t) {
            return t.action == TriggerAction::OpenDoor && t.param == door && t.condition == TriggerCondition::PlayerEnters;
        });
        CHECK(opener);
    }
}

TEST_CASE("Spawn points, gates and checkpoints in the shipped levels are on solid ground", "[hub][shipped]") {
    for (const char* id : {"level01_escape", "kodiak_hub", "kodiak_range"}) {
        INFO("level " << id);
        const LevelLoad r = loadLevelFile(levelPath(kAssets, id));
        REQUIRE(r.clean());
        const LevelData& L = r.level;
        const auto standable = [&](glm::vec3 p) {
            const auto g = L.world.ground({p.x, p.z}, p.y + 0.1f);
            return g && std::abs(g->height - p.y) < 0.05f;
        };

        std::vector<SpawnPoint> spawns{L.spawnPoint()};
        for (const NamedSpawn& s : L.spawns) spawns.push_back(s.point);
        for (const SpawnPoint& sp : spawns) {
            CHECK(standable(sp.pos));
            // Arriving must not drop the player straight into a portal (or they would bounce back).
            for (const TriggerVolume& t : L.triggers)
                if (t.action == TriggerAction::Portal) CHECK_FALSE(t.contains(sp.pos));
        }
        for (const TriggerVolume& t : L.triggers) {
            if (t.action == TriggerAction::Checkpoint) CHECK(standable(t.point));
            if (t.action == TriggerAction::Portal) {
                const glm::vec3 mid = (t.min + t.max) * 0.5f;
                const auto g = L.world.ground({mid.x, mid.z}, t.max.y);
                REQUIRE(g.has_value());
                CHECK(g->height >= t.min.y);
                CHECK(g->height <= t.max.y);
            }
        }
    }
}

TEST_CASE("Prompts in the hub and range don't overlap", "[hub][shipped][prompts]") {
    for (const char* id : {"kodiak_hub", "kodiak_range"}) {
        INFO("level " << id);
        const LevelLoad r = loadLevelFile(levelPath(kAssets, id));
        REQUIRE(r.clean());
        std::vector<const TriggerVolume*> prompts;
        for (const TriggerVolume& t : r.level.triggers)
            if (t.action == TriggerAction::Prompt) prompts.push_back(&t);
        REQUIRE_FALSE(prompts.empty());
        for (std::size_t i = 0; i < prompts.size(); ++i)
            for (std::size_t j = i + 1; j < prompts.size(); ++j) {
                const TriggerVolume &a = *prompts[i], &b = *prompts[j];
                const bool overlap = a.min.x <= b.max.x && b.min.x <= a.max.x && a.min.y <= b.max.y &&
                                     b.min.y <= a.max.y && a.min.z <= b.max.z && b.min.z <= a.max.z;
                CHECK_FALSE(overlap);
            }
    }
}

// ---------------------------------------------------------------------------
// Walking through the real hub: doors, zones and gates
// ---------------------------------------------------------------------------

TEST_CASE("From the landing pad, walking north opens the command post door and reaches the Escape gate", "[hub][walk]") {
    Walker w("kodiak_hub");
    w.walk(0.0f, 40.0f, [](Walker& s) { return s.portal.has_value(); });

    REQUIRE(w.portal.has_value());
    CHECK(w.portal->target == "level01_escape");
    CHECK(w.portal->targetSpawn.empty());
    CHECK(w.doorsOpened >= 1);                 // the door opened by itself as we approached
    CHECK(w.player.position().z < -55.0f);     // all the way inside the command post
    CHECK(w.seconds < 30.0f);
}

TEST_CASE("Without the trigger, the command post door really is solid", "[hub][walk][doors]") {
    Walker w("kodiak_hub");
    w.walk(0.0f, 30.0f, [](Walker&) { return false; }, /*applyTriggers=*/false);
    CHECK(w.player.position().z >= -30.0f + 0.4f - 1e-3f);   // stopped at the door (z -31..-30)
    CHECK(w.player.position().x == Approx(0.0f).margin(0.5f));

    // Opening it by hand (what the `open_door 0` console command does) lets the player through.
    w.load.level.world.setDoorOpen(0, true);
    w.walk(0.0f, 40.0f, [](Walker& s) { return s.player.position().z < -35.0f; }, false);
    CHECK(w.player.position().z < -35.0f);
}

TEST_CASE("The east yard door opens as you approach, and from the yard you reach the range gate", "[hub][walk][doors]") {
    SECTION("through door 1 from the plaza") {
        Walker w("kodiak_hub");
        w.player.respawn({10.0f, 0.0f, -8.0f});
        w.walk(-glm::half_pi<float>(), 20.0f, [](Walker& s) { return s.player.position().x > 21.0f; });  // face +X
        CHECK(w.player.position().x > 21.0f);
        CHECK(w.doorsOpened >= 1);
    }
    SECTION("the same walk is blocked while the door is closed") {
        Walker w("kodiak_hub");
        w.player.respawn({10.0f, 0.0f, -8.0f});
        w.walk(-glm::half_pi<float>(), 8.0f, [](Walker&) { return false; }, false);
        CHECK(w.player.position().x <= 16.0f - 0.4f + 1e-3f);
    }
    SECTION("arriving from the range, the gate is a short walk away") {
        Walker w("kodiak_hub", "from_kodiak_range");
        REQUIRE_FALSE(w.portal.has_value());
        const glm::vec3 p = w.player.position();
        const float yaw = yawToward({p.x, p.z}, {28.5f, -15.0f});
        w.walk(yaw, 10.0f, [](Walker& s) { return s.portal.has_value(); });
        REQUIRE(w.portal.has_value());
        CHECK(w.portal->target == "kodiak_range");
    }
}

TEST_CASE("The range's return gate leads back to the hub's east yard", "[hub][walk]") {
    Walker w("kodiak_range");
    w.walk(glm::pi<float>(), 10.0f, [](Walker& s) { return s.portal.has_value(); });  // face +Z, toward the gate
    REQUIRE(w.portal.has_value());
    CHECK(w.portal->target == "kodiak_hub");
    CHECK(w.portal->targetSpawn == "from_kodiak_range");
}

TEST_CASE("A fresh arrival never triggers a portal by itself", "[hub][walk][portal]") {
    for (const char* spawn : {"", "from_level01_escape", "from_kodiak_range"}) {
        INFO("hub spawn '" << spawn << "'");
        Walker w("kodiak_hub", spawn);
        for (int i = 0; i < 120; ++i) w.step(0.0f);          // stand around (walking into a wall / open ground)
        // (walking north from the east yard just hits a wall; from the pad it is far from the gate)
        CHECK_FALSE(w.portal.has_value());
    }
    Walker r("kodiak_range");
    for (int i = 0; i < 30; ++i) r.step(0.0f);  // walk north, away from the gate
    CHECK_FALSE(r.portal.has_value());
}

// ---------------------------------------------------------------------------
// Checkpoints and spawn selection across levels
// ---------------------------------------------------------------------------

TEST_CASE("CheckpointTracker: arrival sets it, checkpoints replace it, a new level resets it", "[hub][checkpoint]") {
    CheckpointTracker cp;
    CHECK(near(cp.current().pos, {0.0f, 0.0f, 0.0f}));  // nothing set yet

    cp.arrive({{1.0f, 0.0f, 2.0f}, 0.5f});
    CHECK(near(cp.current().pos, {1.0f, 0.0f, 2.0f}));
    CHECK(cp.current().yaw == Approx(0.5f));

    cp.reached({10.0f, 1.0f, -20.0f}, 1.5f);
    CHECK(near(cp.current().pos, {10.0f, 1.0f, -20.0f}));
    CHECK(cp.current().yaw == Approx(1.5f));

    cp.reached({11.0f, 1.0f, -30.0f}, -1.0f);  // the latest one wins
    CHECK(near(cp.current().pos, {11.0f, 1.0f, -30.0f}));

    cp.arrive({{-5.0f, 0.0f, 5.0f}, 0.0f});    // entering another level forgets the old checkpoints
    CHECK(near(cp.current().pos, {-5.0f, 0.0f, 5.0f}));
    CHECK(cp.current().yaw == 0.0f);
}

TEST_CASE("Respawning after a death in the hub uses the hub's checkpoints, not another level's", "[hub][checkpoint]") {
    const LevelLoad l01 = loadLevelFile(levelPath(kAssets, "level01_escape"));
    const LevelLoad hub = loadLevelFile(levelPath(kAssets, "kodiak_hub"));
    const LevelLoad range = loadLevelFile(levelPath(kAssets, "kodiak_range"));
    REQUIRE(l01.clean());
    REQUIRE(hub.clean());
    REQUIRE(range.clean());

    CheckpointTracker cp;

    // Play level 01 and reach its arena checkpoint at z = -80.
    cp.arrive(l01.level.spawnPoint());
    CHECK(near(cp.current().pos, l01.level.spawn));
    for (const TriggerVolume& t : l01.level.triggers)
        if (t.action == TriggerAction::Checkpoint && t.point.z == -80.0f) cp.reached(t.point, 0.0f);
    CHECK(near(cp.current().pos, {0.0f, 0.0f, -80.0f}));

    // Finish it: the transition arrives at the hub's landing pad. The old checkpoint is gone.
    const LevelTransition done = completionTransition(l01.level);
    cp.arrive(hub.level.spawnPoint(done.spawn));
    CHECK(near(cp.current().pos, {0.0f, 0.2f, 40.0f}));

    // Walk to the plaza checkpoint in the hub; dying now returns there.
    TriggerSystem ts;
    installTriggers(hub.level, ts);
    for (const TriggerEvent& e : ts.update({0.0f, 0.0f, 0.0f}, [](int) { return 1; }))
        if (e.action == TriggerAction::Checkpoint) cp.reached(e.point, 0.0f);
    CHECK(near(cp.current().pos, {0.0f, 0.0f, 0.0f}));

    // Through the range gate and back out: arriving in the range resets, and so does coming home.
    cp.arrive(range.level.spawnPoint());
    CHECK(near(cp.current().pos, {0.0f, 0.0f, 4.0f}));
    cp.arrive(hub.level.spawnPoint(levelLinks(range.level)[0].spawn));
    CHECK(near(cp.current().pos, {24.0f, 0.0f, -8.5f}));
    CHECK(cp.current().yaw == Approx(glm::radians(90.0f)));
}

TEST_CASE("An unknown arrival spawn falls back to the level's default", "[hub][checkpoint]") {
    const LevelLoad hub = loadLevelFile(levelPath(kAssets, "kodiak_hub"));
    REQUIRE(hub.clean());
    CheckpointTracker cp;
    cp.arrive(hub.level.spawnPoint("a_spawn_that_does_not_exist"));
    CHECK(near(cp.current().pos, hub.level.spawn));
}

TEST_CASE("Checkpoint volumes in the hub are one per zone and fire once", "[hub][checkpoint]") {
    const LevelLoad hub = loadLevelFile(levelPath(kAssets, "kodiak_hub"));
    REQUIRE(hub.clean());
    TriggerSystem ts;
    installTriggers(hub.level, ts);
    const auto alive = [](int) { return 1; };

    const auto checkpointAt = [&](glm::vec3 where) -> std::optional<glm::vec3> {
        for (const TriggerEvent& e : ts.update(where, alive))
            if (e.action == TriggerAction::Checkpoint) return e.point;
        return std::nullopt;
    };

    const auto pad = checkpointAt({0.0f, 0.2f, 40.0f});
    REQUIRE(pad.has_value());
    CHECK(near(*pad, {0.0f, 0.2f, 40.0f}));
    CHECK_FALSE(checkpointAt({0.0f, 0.2f, 40.0f}).has_value());  // only once

    CHECK(checkpointAt({0.0f, 0.0f, 0.0f}).has_value());          // plaza
    CHECK(checkpointAt({0.0f, 0.0f, -45.0f}).has_value());        // command post
    CHECK(checkpointAt({24.0f, 0.0f, -8.5f}).has_value());        // east yard
    ts.reset();
    CHECK(checkpointAt({0.0f, 0.2f, 40.0f}).has_value());         // a fresh load re-arms them
}

// ---------------------------------------------------------------------------
// Pieces the console commands rely on
// ---------------------------------------------------------------------------

TEST_CASE("World knows its doors", "[hub][doors]") {
    World w = floorWorld();
    CHECK(w.doorIds().empty());
    CHECK_FALSE(w.hasDoor(0));

    for (const int id : {5, 2, 5, 9}) {
        Block b = solid(0.0f, static_cast<float>(id), 1.0f, static_cast<float>(id) + 0.5f, 3.0f);
        b.door = id;
        w.add(b);
    }
    CHECK(w.doorIds() == std::vector<int>{2, 5, 9});   // sorted, without duplicates
    CHECK(w.hasDoor(5));
    CHECK_FALSE(w.hasDoor(3));
    CHECK_FALSE(w.hasDoor(-1));
    w.setDoorOpen(5, true);
    CHECK(w.hasDoor(5));                                // opening a door does not remove it
}

TEST_CASE("setDoorOpen with a non-door id leaves the level alone", "[hub][doors]") {
    // Ordinary blocks carry door == -1. Matching that id used to switch off every
    // one of them (e.g. `open_door -1` from the console deleted the whole level).
    World w = floorWorld();
    w.add(solid(5.0f, -5.0f, 6.0f, 5.0f, 3.0f));
    Block door = solid(10.0f, -5.0f, 11.0f, 5.0f, 3.0f);
    door.door  = 0;
    w.add(door);

    for (const int bad : {-1, -2, 7}) {
        INFO("id " << bad);
        w.setDoorOpen(bad, true);
        for (const Block& b : w.blocks()) CHECK(b.enabled);
        w.setDoorOpen(bad, false);
        for (const Block& b : w.blocks()) CHECK(b.enabled);
    }
    CHECK(w.raycast({0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 50.0f)->t == Approx(5.0f));  // the wall is still there
    w.setDoorOpen(0, true);
    CHECK(w.blocks()[2].enabled == false);   // a real door still opens
    CHECK(w.blocks()[1].enabled);
}

TEST_CASE("Camera::face points the view and agrees with how the player walks", "[hub][camera]") {
    Camera cam;
    CHECK(near(cam.forward(), {0.0f, 0.0f, -1.0f}));

    cam.face(glm::half_pi<float>());
    CHECK(near(cam.forward(), {-1.0f, 0.0f, 0.0f}, 1e-5f));    // 90 degrees faces -X
    cam.face(-glm::half_pi<float>());
    CHECK(near(cam.forward(), {1.0f, 0.0f, 0.0f}, 1e-5f));     // -90 faces +X
    cam.face(glm::pi<float>());
    CHECK(near(cam.forward(), {0.0f, 0.0f, 1.0f}, 1e-5f));     // 180 faces +Z

    cam.face(3.0f * glm::pi<float>());                          // wraps
    CHECK(near(cam.forward(), {0.0f, 0.0f, 1.0f}, 1e-4f));
    CHECK(std::abs(cam.yaw()) <= glm::pi<float>() + 1e-4f);

    // Looking up and then spawning levels the camera.
    cam.look({0.0f, -300.0f});
    REQUIRE(std::abs(cam.forward().y) > 0.1f);
    cam.face(0.0f);
    CHECK(cam.forward().y == Approx(0.0f).margin(1e-5f));

    // The movement controller, fed the camera's yaw, walks the way the camera faces.
    World w = floorWorld();
    for (const float yaw : {0.0f, 0.7f, glm::half_pi<float>(), -2.0f, glm::pi<float>()}) {
        INFO("yaw " << yaw);
        cam.face(yaw);
        MovementController mc(w);
        mc.respawn({0.0f, 0.0f, 0.0f});
        MoveInput in;
        in.move = {0.0f, 1.0f};
        for (int i = 0; i < 30; ++i) mc.tick(kDt, cam.yaw(), in);
        const glm::vec2 walked = glm::normalize(glm::vec2(mc.position().x, mc.position().z));
        const glm::vec2 facing = glm::normalize(glm::vec2(cam.forward().x, cam.forward().z));
        CHECK(glm::dot(walked, facing) == Approx(1.0f).margin(1e-3f));
    }
}
