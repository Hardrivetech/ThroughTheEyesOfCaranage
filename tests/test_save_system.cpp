#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/SaveSystem.hpp"
#include "game/Weapons.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace carnage;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

bool mentions(const std::string& s, const std::string& needle) { return s.find(needle) != std::string::npos; }

bool anyWarning(const SaveParse& p, const std::string& needle) {
    for (const std::string& w : p.warnings)
        if (mentions(w, needle)) return true;
    return false;
}

// A scratch directory removed when the test ends.
struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) {
        path = fs::temp_directory_path() / ("carnage_save_test_" + name);
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& rel) const { return (path / rel).string(); }
};

SaveData sample() {
    SaveData d;
    d.levelId     = "kodiak_hub";
    d.spawnPos    = {1.5f, 0.2f, -57.25f};
    d.spawnYawDeg = 135.0f;
    d.health      = 73.5f;
    d.armor       = 12.25f;
    d.weapons     = {"fists", "pistol", "smg", "shotgun", "rocket"};
    d.activeWeapon = "smg";
    d.ammo        = {{"pistol", 7}, {"smg", 21}, {"shotgun", 2}, {"rocket", 1}};
    d.offhand     = "pistol";
    d.offhandAmmo = 4;
    d.completedLevels = {"level01_escape", "kodiak_range"};
    return d;
}

WeaponSet freshWeapons() {
    WeaponSet w;
    for (std::size_t i = 0; i < kWeaponCount; ++i) w[i] = makeWeapon(i);
    return w;
}

const std::string kGood =
    "save version=1 level_id=kodiak_hub spawn_pos=1,2,3 spawn_yaw=90 health=80 armor=25 "
    "weapons=fists,pistol,smg active_weapon=pistol ammo=pistol:7,smg:30 completed_levels=level01_escape\n";

} // namespace

// ---------------------------------------------------------------------------
// Format
// ---------------------------------------------------------------------------

TEST_CASE("A save round-trips through its text exactly", "[save][format]") {
    const SaveData d = sample();
    const SaveParse p = parseSave(serializeSave(d));
    REQUIRE(p.data.has_value());
    CHECK(p.warnings.empty());
    CHECK(*p.data == d);
}

TEST_CASE("Round trips are exact for awkward floats", "[save][format]") {
    for (const float v : {0.1f, 0.2f, 1.0f / 3.0f, 1234.5678f, -0.0078125f, 99999.5f, 1e-5f, 37.5f}) {
        SaveData d = sample();
        d.spawnPos    = {v, -v, v * 0.25f};
        d.spawnYawDeg = std::fmod(v, 360.0f);
        d.health      = std::clamp(std::abs(v) * 50.0f + 1.0f, 1.0f, 100.0f);
        const SaveParse p = parseSave(serializeSave(d));
        INFO("value " << v);
        REQUIRE(p.data.has_value());
        CHECK(p.data->spawnPos == d.spawnPos);       // bit-for-bit, not approximately
        CHECK(p.data->spawnYawDeg == d.spawnYawDeg);
        CHECK(p.data->health == d.health);
    }
}

TEST_CASE("The save file is one readable record, and the same data gives the same bytes", "[save][format]") {
    const std::string a = serializeSave(sample()), b = serializeSave(sample());
    CHECK(a == b);
    CHECK(a.starts_with("# "));                      // a comment line explains the file
    CHECK(mentions(a, "\nsave version=1 level_id=kodiak_hub spawn_pos=1.5,0.2,-57.25 spawn_yaw=135 health=73.5 armor=12.25 "));
    CHECK(mentions(a, "weapons=fists,pistol,smg,shotgun,rocket active_weapon=smg ammo=pistol:7,smg:21,shotgun:2,rocket:1"));
    CHECK(mentions(a, "offhand=pistol offhand_ammo=4 completed_levels=level01_escape,kodiak_range\n"));
    CHECK(std::count(a.begin(), a.end(), '\n') == 2);   // comment + record
}

TEST_CASE("Optional fields are left out when empty", "[save][format]") {
    SaveData d = sample();
    d.ammo.clear();
    d.offhand.clear();
    d.offhandAmmo = -1;
    d.completedLevels.clear();
    const std::string text = serializeSave(d);
    CHECK_FALSE(mentions(text, "ammo="));
    CHECK_FALSE(mentions(text, "offhand"));
    CHECK_FALSE(mentions(text, "completed_levels"));
    const SaveParse p = parseSave(text);
    REQUIRE(p.data.has_value());
    CHECK(*p.data == d);
}

TEST_CASE("A hand-written save parses with the documented keys", "[save][format]") {
    const SaveParse p = parseSave(kGood);
    REQUIRE(p.data.has_value());
    CHECK(p.warnings.empty());
    CHECK(p.data->version == 1);
    CHECK(p.data->levelId == "kodiak_hub");
    CHECK(p.data->spawnPos == glm::vec3(1, 2, 3));
    CHECK(p.data->spawnYawDeg == 90.0f);
    CHECK(p.data->health == 80.0f);
    CHECK(p.data->armor == 25.0f);
    CHECK(p.data->weapons == std::vector<std::string>{"fists", "pistol", "smg"});
    CHECK(p.data->activeWeapon == "pistol");
    REQUIRE(p.data->ammo.size() == 2);
    CHECK(p.data->ammo[0] == WeaponAmmo{"pistol", 7});
    CHECK(p.data->ammo[1] == WeaponAmmo{"smg", 30});
    CHECK(p.data->completedLevels == std::vector<std::string>{"level01_escape"});
}

TEST_CASE("Comments, blank lines and CRLF line endings are fine", "[save][format]") {
    std::string text = "# a save\r\n\r\n" + kGood;
    for (std::size_t i = text.find('\n'); i != std::string::npos; i = text.find('\n', i + 2)) {
        if (i > 0 && text[i - 1] != '\r') text.insert(i, "\r");
    }
    const SaveParse p = parseSave(text);
    REQUIRE(p.data.has_value());
    CHECK(p.data->levelId == "kodiak_hub");
}

TEST_CASE("Only the required fields are needed", "[save][format]") {
    const SaveParse p = parseSave("save version=1 level_id=level01_escape spawn_pos=0,0,10 health=100 armor=50\n");
    REQUIRE(p.data.has_value());
    CHECK(p.data->spawnYawDeg == 0.0f);
    CHECK(p.data->activeWeapon == "fists");
    CHECK(p.data->weapons.size() == kWeaponCount);   // default: every weapon
    CHECK(p.data->ammo.empty());                     // absent = full magazines
    CHECK(p.data->offhand.empty());
    CHECK(p.data->completedLevels.empty());
}

// ---------------------------------------------------------------------------
// Corrupt, empty and hostile input
// ---------------------------------------------------------------------------

TEST_CASE("Unusable save text is rejected with a reason, never a crash", "[save][malformed]") {
    struct Bad { const char* name; std::string text; const char* expect; };
    const std::string big(kMaxSaveBytes + 10, 'x');
    const Bad cases[] = {
        {"empty",               "",                                                              "empty"},
        {"only whitespace",     "  \n\t\n  \n",                                                  "empty"},
        {"only a comment",      "# nothing here\n",                                              "no save record"},
        {"no save record",      "level name=x\n",                                                "unexpected record 'level'"},
        {"binary",              std::string("save\0version=1", 15),                              "binary"},
        {"too large",           big,                                                             "too large"},
        {"truncated",           "save version=1 level_id=kodiak_hub spawn_pos=0,0",              "spawn_pos"},
        {"cut mid key",         "save version=1 level_id=kodiak_hub spawn_",                     "key=value"},
        {"cut mid value",       "save version=1 level_id=",                                      "key=value"},
        {"two saves",           kGood + kGood,                                                   "more than one"},
        {"missing level_id",    "save version=1 spawn_pos=0,0,0 health=100 armor=0\n",           "level_id"},
        {"missing spawn_pos",   "save version=1 level_id=a health=100 armor=0\n",                "spawn_pos"},
        {"missing health",      "save version=1 level_id=a spawn_pos=0,0,0 armor=0\n",           "health"},
        {"missing armor",       "save version=1 level_id=a spawn_pos=0,0,0 health=1\n",          "armor"},
        {"missing version",     "save level_id=a spawn_pos=0,0,0 health=1 armor=0\n",            "version"},
        {"path as level id",    "save version=1 level_id=../etc spawn_pos=0,0,0 health=1 armor=0\n", "level id"},
        {"upper-case level id", "save version=1 level_id=Hub spawn_pos=0,0,0 health=1 armor=0\n",    "level id"},
        {"newer version",       "save version=2 level_id=a spawn_pos=0,0,0 health=1 armor=0\n",  "newer version"},
        {"old version",         "save version=0 level_id=a spawn_pos=0,0,0 health=1 armor=0\n",  "unsupported"},
        {"version not a number","save version=one level_id=a spawn_pos=0,0,0 health=1 armor=0\n", "not an integer"},
        {"nan health",          "save version=1 level_id=a spawn_pos=0,0,0 health=nan armor=0\n", "not a number"},
        {"inf armor",           "save version=1 level_id=a spawn_pos=0,0,0 health=1 armor=inf\n", "not a number"},
        {"bad coordinate",      "save version=1 level_id=a spawn_pos=0,x,0 health=1 armor=0\n",  "bad number"},
        {"two coordinates",     "save version=1 level_id=a spawn_pos=0,0 health=1 armor=0\n",    "3 comma-separated"},
        {"spawn far away",      "save version=1 level_id=a spawn_pos=0,9999999,0 health=1 armor=0\n", "out of range"},
        {"spawn far west",      "save version=1 level_id=a spawn_pos=-9999999,0,0 health=1 armor=0\n", "out of range"},
        {"spawn far south",     "save version=1 level_id=a spawn_pos=0,0,9999999 health=1 armor=0\n", "out of range"},
        {"yaw far away",        "save version=1 level_id=a spawn_pos=0,0,0 spawn_yaw=99999 health=1 armor=0\n", "spawn_yaw"},
        {"duplicate key",       "save version=1 version=1 level_id=a spawn_pos=0,0,0 health=1 armor=0\n", "duplicate key"},
        {"bare word",           "save version=1 level_id=a spawn_pos=0,0,0 health=1 armor=0 oops\n", "key=value"},
        {"bad ammo entry",      "save version=1 level_id=a spawn_pos=0,0,0 health=1 armor=0 ammo=pistol\n", "bad ammo entry"},
        {"ammo not a number",   "save version=1 level_id=a spawn_pos=0,0,0 health=1 armor=0 ammo=pistol:many\n", "bad ammo entry"},
        {"offhand ammo junk",   "save version=1 level_id=a spawn_pos=0,0,0 health=1 armor=0 offhand=smg offhand_ammo=lots\n", "not an integer"},
    };
    for (const Bad& c : cases) {
        INFO(c.name);
        const SaveParse p = parseSave(c.text);
        CHECK_FALSE(p.data.has_value());
        CHECK(mentions(p.error, c.expect));
        CHECK_FALSE(p.error.empty());
    }
}

TEST_CASE("Out-of-range values are repaired and reported, not trusted", "[save][malformed]") {
    const auto parse = [](const std::string& extra) {
        return parseSave("save version=1 level_id=a spawn_pos=0,0,0 " + extra + "\n");
    };
    SECTION("health and armor are clamped; a loaded game is alive") {
        const SaveParse p = parse("health=9999 armor=-5");
        REQUIRE(p.data.has_value());
        CHECK(p.data->health == PlayerHealth::kMaxHealth);
        CHECK(p.data->armor == 0.0f);
        CHECK(anyWarning(p, "'health'"));
        CHECK(anyWarning(p, "'armor'"));

        const SaveParse dead = parse("health=0 armor=0");
        REQUIRE(dead.data.has_value());
        CHECK(dead.data->health == 1.0f);
        const SaveParse neg = parse("health=-40 armor=0");
        REQUIRE(neg.data.has_value());
        CHECK(neg.data->health == 1.0f);
    }
    SECTION("unknown weapons are dropped") {
        const SaveParse p = parse("health=50 armor=0 weapons=fists,bazooka,pistol active_weapon=pistol");
        REQUIRE(p.data.has_value());
        CHECK(p.data->weapons == std::vector<std::string>{"fists", "pistol"});
        CHECK(anyWarning(p, "unknown weapon 'bazooka'"));
    }
    SECTION("fists are always held") {
        const SaveParse p = parse("health=50 armor=0 weapons=smg active_weapon=smg");
        REQUIRE(p.data.has_value());
        CHECK(p.data->weapons == std::vector<std::string>{"fists", "smg"});
    }
    SECTION("duplicate weapons are collapsed") {
        const SaveParse p = parse("health=50 armor=0 weapons=fists,pistol,pistol,fists");
        REQUIRE(p.data.has_value());
        CHECK(p.data->weapons == std::vector<std::string>{"fists", "pistol"});
    }
    SECTION("an active weapon that is not held falls back to fists") {
        const SaveParse p = parse("health=50 armor=0 weapons=fists,pistol active_weapon=rocket");
        REQUIRE(p.data.has_value());
        CHECK(p.data->activeWeapon == "fists");
        CHECK(anyWarning(p, "not held"));
        const SaveParse unknown = parse("health=50 armor=0 active_weapon=laser");
        REQUIRE(unknown.data.has_value());
        CHECK(unknown.data->activeWeapon == "fists");
    }
    SECTION("ammo is clamped, unknown weapons' ammo dropped, repeats keep the last") {
        const SaveParse p = parse("health=50 armor=0 ammo=pistol:-3,laser:9,smg:999999999,shotgun:2,shotgun:5");
        REQUIRE(p.data.has_value());
        REQUIRE(p.data->ammo.size() == 3);
        CHECK(p.data->ammo[0] == WeaponAmmo{"pistol", 0});
        CHECK(p.data->ammo[1] == WeaponAmmo{"smg", 100000});
        CHECK(p.data->ammo[2] == WeaponAmmo{"shotgun", 5});
        CHECK(anyWarning(p, "unknown weapon 'laser'"));
    }
    SECTION("the off hand cannot be fists or an unknown weapon") {
        const SaveParse fists = parse("health=50 armor=0 offhand=fists offhand_ammo=3");
        REQUIRE(fists.data.has_value());
        CHECK(fists.data->offhand.empty());
        CHECK(fists.data->offhandAmmo == -1);
        const SaveParse unknown = parse("health=50 armor=0 offhand=laser");
        REQUIRE(unknown.data.has_value());
        CHECK(unknown.data->offhand.empty());
        CHECK(anyWarning(unknown, "off-hand weapon 'laser'"));
    }
    SECTION("completed levels must be ids and are not repeated") {
        const SaveParse p = parse("health=50 armor=0 completed_levels=a,B,a,../x,level01_escape");
        REQUIRE(p.data.has_value());
        CHECK(p.data->completedLevels == std::vector<std::string>{"a", "level01_escape"});
        CHECK(anyWarning(p, "'B'"));
        CHECK(anyWarning(p, "'../x'"));
    }
    SECTION("unknown keys from a newer build are ignored") {
        const SaveParse p = parse("health=50 armor=0 difficulty=hard");
        REQUIRE(p.data.has_value());
        CHECK(p.warnings.empty());
    }
}

TEST_CASE("Mangling a valid save every way never crashes or yields an unusable game", "[save][malformed][fuzz]") {
    const std::string good = serializeSave(sample());
    const auto check = [](const std::string& text) {
        const SaveParse p = parseSave(text);
        if (!p.data) { CHECK_FALSE(p.error.empty()); return; }
        const SaveData& d = *p.data;
        REQUIRE(d.version == kSaveVersion);
        REQUIRE(d.health >= 1.0f);
        REQUIRE(d.health <= PlayerHealth::kMaxHealth);
        REQUIRE(d.armor >= 0.0f);
        REQUIRE(d.armor <= PlayerHealth::kMaxArmor);
        REQUIRE(std::isfinite(d.spawnPos.x));
        REQUIRE(std::isfinite(d.spawnPos.y));
        REQUIRE(std::isfinite(d.spawnPos.z));
        REQUIRE(std::isfinite(d.spawnYawDeg));
        REQUIRE(rec::validId(d.levelId));
        REQUIRE(weaponIndex(d.activeWeapon) >= 0);
        REQUIRE(std::find(d.weapons.begin(), d.weapons.end(), "fists") != d.weapons.end());
        for (const std::string& w : d.weapons) REQUIRE(weaponIndex(w) >= 0);
        for (const WeaponAmmo& a : d.ammo) { REQUIRE(weaponIndex(a.id) >= 0); REQUIRE(a.rounds >= 0); }
        REQUIRE((d.offhand.empty() || weaponIndex(d.offhand) > 0));
        for (const std::string& c : d.completedLevels) REQUIRE(rec::validId(c));
        // Whatever survived can be written and read back unchanged.
        const SaveParse again = parseSave(serializeSave(d));
        REQUIRE(again.data.has_value());
        REQUIRE(*again.data == d);
    };

    // Every truncation.
    for (std::size_t n = 0; n <= good.size(); ++n) check(good.substr(0, n));
    // Every single byte replaced by each of a few nasty values.
    for (std::size_t i = 0; i < good.size(); ++i)
        for (const char c : {'\0', '\n', '=', ',', ':', ' ', '"', '#', '9', '-', 'x', '\xff'}) {
            std::string m = good;
            m[i] = c;
            check(m);
        }
    // Every byte deleted, and a 3-byte slice deleted at every position.
    for (std::size_t i = 0; i < good.size(); ++i) {
        std::string m = good;
        m.erase(i, 1);
        check(m);
        if (i + 3 <= good.size()) { m = good; m.erase(i, 3); check(m); }
    }
    // Pseudo-random bytes, deterministically.
    std::uint32_t s = 0x1234abcd;
    for (int round = 0; round < 200; ++round) {
        std::string junk;
        for (int k = 0; k < 120; ++k) { s = s * 1664525u + 1013904223u; junk += static_cast<char>(s >> 24); }
        check(junk);
    }
}

// ---------------------------------------------------------------------------
// Loadout: the running game <-> a save
// ---------------------------------------------------------------------------

TEST_CASE("Weapon::setAmmo clamps to the magazine", "[save][weapon]") {
    auto w = makeWeapon(1);
    w->setAmmo(3);
    CHECK(w->ammo() == 3);
    w->setAmmo(-5);
    CHECK(w->ammo() == 0);
    w->setAmmo(100000);
    CHECK(w->ammo() == w->magSize());
    w->setAmmo(2);
    CHECK(w->ammo() == 2);
    CHECK_FALSE(w->reloading());
}

TEST_CASE("Capturing the game and applying it to a fresh one restores the loadout", "[save][loadout]") {
    WeaponSet weapons = freshWeapons();
    weapons[1]->setAmmo(3);
    weapons[2]->setAmmo(17);
    weapons[3]->setAmmo(1);
    weapons[4]->setAmmo(0);
    std::size_t current = 2;
    auto offhand = makeWeapon(1);
    offhand->setAmmo(5);
    PlayerHealth health;
    health.health = 41.0f;
    health.armor  = 8.0f;
    Progress progress;
    progress.complete("level01_escape");

    const SpawnPoint checkpoint{{10.0f, 0.5f, -20.0f}, glm::radians(90.0f)};
    const SaveData d = captureSave("kodiak_island", checkpoint, health, weapons, current, offhand.get(), progress);

    CHECK(d.levelId == "kodiak_island");
    CHECK(d.spawnPos == checkpoint.pos);
    CHECK(d.spawnYawDeg == Approx(90.0f));
    CHECK(d.health == 41.0f);
    CHECK(d.armor == 8.0f);
    CHECK(d.activeWeapon == "smg");
    CHECK(d.offhand == "pistol");
    CHECK(d.offhandAmmo == 5);
    CHECK(d.completedLevels == std::vector<std::string>{"level01_escape"});

    // A fresh game: full magazines, fists out, full health, empty off hand.
    WeaponSet fresh = freshWeapons();
    std::size_t freshCurrent = 0;
    std::unique_ptr<Weapon> freshOffhand;
    PlayerHealth freshHealth;
    applyLoadout(d, freshHealth, fresh, freshCurrent, freshOffhand);

    CHECK(freshHealth.health == 41.0f);
    CHECK(freshHealth.armor == 8.0f);
    CHECK_FALSE(freshHealth.dead);
    CHECK(freshCurrent == 2);
    CHECK(fresh[1]->ammo() == 3);
    CHECK(fresh[2]->ammo() == 17);
    CHECK(fresh[3]->ammo() == 1);
    CHECK(fresh[4]->ammo() == 0);
    REQUIRE(freshOffhand != nullptr);
    CHECK(std::string(freshOffhand->stats().name) == std::string(weapons[1]->stats().name));
    CHECK(freshOffhand->ammo() == 5);

    const SpawnPoint back = saveSpawn(d);
    CHECK(back.pos == checkpoint.pos);
    CHECK(back.yaw == Approx(checkpoint.yaw));
}

TEST_CASE("Loading a save revives a dead player and clears the off hand it does not have", "[save][loadout]") {
    PlayerHealth health;
    health.damage(1000.0f, true);
    REQUIRE(health.dead);
    WeaponSet weapons = freshWeapons();
    std::size_t current = 4;
    std::unique_ptr<Weapon> offhand = makeWeapon(2);   // the running game has a left-hand SMG

    SaveData d = sample();
    d.offhand.clear();
    d.offhandAmmo = -1;
    applyLoadout(d, health, weapons, current, offhand);
    CHECK_FALSE(health.dead);
    CHECK(health.health == 73.5f);
    CHECK(health.iFrames == 0.0f);
    CHECK(offhand == nullptr);
    CHECK(current == 2);   // sample()'s active weapon is the smg
}

TEST_CASE("Applying a save with absent ammo leaves full magazines, and clamps to the magazine size", "[save][loadout]") {
    SaveData d = sample();
    d.ammo = {{"pistol", 100000}};   // more than a magazine holds
    WeaponSet weapons = freshWeapons();
    std::size_t current = 0;
    std::unique_ptr<Weapon> offhand;
    PlayerHealth health;
    applyLoadout(d, health, weapons, current, offhand);
    CHECK(weapons[1]->ammo() == weapons[1]->magSize());
    CHECK(weapons[2]->ammo() == weapons[2]->magSize());   // not mentioned: untouched
}

TEST_CASE("Capture gives a loadable save even for a nearly dead player", "[save][loadout]") {
    WeaponSet weapons = freshWeapons();
    PlayerHealth health;
    health.health = 0.0f;   // died on the same tick as the checkpoint
    health.dead   = true;
    const SaveData d = captureSave("level01_escape", {{0, 0, 10}, 0.0f}, health, weapons, 0, nullptr, Progress{});
    CHECK(d.health >= 1.0f);
    CHECK(parseSave(serializeSave(d)).data.has_value());
}

// ---------------------------------------------------------------------------
// Mission progress
// ---------------------------------------------------------------------------

TEST_CASE("Completed missions are tracked once, in order", "[save][progress]") {
    Progress p;
    CHECK_FALSE(p.hubUnlocked());
    CHECK(p.complete("level01_escape"));
    CHECK_FALSE(p.complete("level01_escape"));   // already done
    CHECK(p.complete("kodiak_range"));
    CHECK(p.isComplete("level01_escape"));
    CHECK_FALSE(p.isComplete("kodiak_island"));
    CHECK(p.hubUnlocked());
    CHECK(p.ids().size() == 2);
    CHECK(p.ids()[0] == "level01_escape");
    CHECK(p.ids()[1] == "kodiak_range");
}

TEST_CASE("Only valid ids can be marked complete", "[save][progress][malformed]") {
    Progress p;
    CHECK_FALSE(p.complete(""));
    CHECK_FALSE(p.complete("../escape"));
    CHECK_FALSE(p.complete("Level 1"));
    CHECK(p.ids().empty());
}

TEST_CASE("The hub unlocks only when the first mission is done", "[save][progress]") {
    Progress p;
    p.complete("kodiak_range");
    CHECK_FALSE(p.hubUnlocked());
    p.complete(kFirstMissionId);
    CHECK(p.hubUnlocked());
}

TEST_CASE("Progress survives a save and a load", "[save][progress]") {
    Progress p;
    p.complete("level01_escape");
    p.complete("docks");
    WeaponSet weapons = freshWeapons();
    const SaveData d = captureSave("kodiak_hub", {{0, 0, 0}, 0.0f}, PlayerHealth{}, weapons, 0, nullptr, p);

    const SaveParse parsed = parseSave(serializeSave(d));
    REQUIRE(parsed.data.has_value());
    Progress restored;
    restored.set(parsed.data->completedLevels);
    CHECK(restored.isComplete("level01_escape"));
    CHECK(restored.isComplete("docks"));
    CHECK(restored.ids().size() == 2);
    CHECK(restored.hubUnlocked());
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

TEST_CASE("Save slots are 1 to 9 under saves/", "[save][files]") {
    CHECK(slotPath(1) == "saves/slot1.sav");
    CHECK(slotPath(9) == "saves/slot9.sav");
    CHECK(slotPath(3, "/tmp/x") == "/tmp/x/slot3.sav");
    for (const int bad : {0, -1, 10, 100, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
        INFO(bad);
        CHECK(slotPath(bad).empty());
        CHECK_FALSE(validSlot(bad));
    }
    CHECK(validSlot(1));
    CHECK(validSlot(kSaveSlots));
}

TEST_CASE("A save is written, creating the folder, and read back", "[save][files]") {
    TempDir dir("roundtrip");
    const std::string path = dir.file("nested/saves/slot1.sav");   // neither folder exists yet
    REQUIRE_FALSE(saveExists(path));

    REQUIRE(writeSave(path, sample()));
    CHECK(saveExists(path));
    CHECK_FALSE(saveExists(path + ".tmp"));    // the temporary file is gone

    const auto back = readSave(path);
    REQUIRE(back.has_value());
    CHECK(*back == sample());
}

TEST_CASE("Writing again replaces the save", "[save][files]") {
    TempDir dir("overwrite");
    const std::string path = dir.file("slot1.sav");
    SaveData a = sample();
    SaveData b = sample();
    b.levelId = "kodiak_island";
    b.health  = 5.0f;
    b.completedLevels.push_back("docks");
    REQUIRE(writeSave(path, a));
    REQUIRE(writeSave(path, b));
    const auto back = readSave(path);
    REQUIRE(back.has_value());
    CHECK(*back == b);
    CHECK_FALSE(saveExists(path + ".tmp"));
}

TEST_CASE("A missing, empty or corrupt save file gives nullopt", "[save][files][malformed]") {
    TempDir dir("bad");
    fs::create_directories(dir.path);

    CHECK_FALSE(readSave(dir.file("absent.sav")).has_value());

    {   std::ofstream f(dir.file("empty.sav")); }
    CHECK_FALSE(readSave(dir.file("empty.sav")).has_value());

    {   std::ofstream f(dir.file("garbage.sav"), std::ios::binary);
        for (int i = 0; i < 400; ++i) f.put(static_cast<char>(i * 37)); }
    CHECK_FALSE(readSave(dir.file("garbage.sav")).has_value());

    {   std::ofstream f(dir.file("truncated.sav"));
        const std::string text = serializeSave(sample());
        f << text.substr(0, text.size() / 2); }
    CHECK_FALSE(readSave(dir.file("truncated.sav")).has_value());

    {   std::ofstream f(dir.file("huge.sav"));
        f << std::string(kMaxSaveBytes * 3, 'a'); }
    CHECK_FALSE(readSave(dir.file("huge.sav")).has_value());

    // A directory where the file should be.
    fs::create_directories(dir.path / "isdir.sav");
    CHECK_FALSE(readSave(dir.file("isdir.sav")).has_value());
    CHECK_FALSE(saveExists(dir.file("isdir.sav")));
}

TEST_CASE("A repaired save is still loaded from disk", "[save][files]") {
    TempDir dir("repair");
    fs::create_directories(dir.path);
    {   std::ofstream f(dir.file("slot1.sav"));
        f << "save version=1 level_id=kodiak_hub spawn_pos=0,0,0 health=500 armor=50 weapons=fists,bazooka\n"; }
    const auto back = readSave(dir.file("slot1.sav"));
    REQUIRE(back.has_value());
    CHECK(back->health == PlayerHealth::kMaxHealth);
    CHECK(back->weapons == std::vector<std::string>{"fists"});
}

TEST_CASE("Writing to somewhere impossible fails cleanly", "[save][files][malformed]") {
    TempDir dir("impossible");
    fs::create_directories(dir.path);
    {   std::ofstream f(dir.file("blocker")); f << "x"; }       // a file where a folder is needed
    CHECK_FALSE(writeSave(dir.file("blocker/slot1.sav"), sample()));
}

TEST_CASE("describeSave names the level for the Continue button", "[save]") {
    SaveData d = sample();
    CHECK(describeSave(d) == "KODIAK_HUB");
    d.levelId = "level01_escape";
    CHECK(describeSave(d) == "LEVEL01_ESCAPE");
}

TEST_CASE("The weapon ids match the weapons the game builds", "[save][weapon]") {
    CHECK(kWeaponIds.size() == kWeaponCount);
    for (std::size_t i = 0; i < kWeaponCount; ++i) {
        CHECK(weaponIndex(kWeaponIds[i]) == static_cast<int>(i));
        const auto w = makeWeapon(i);
        REQUIRE(w != nullptr);
    }
    CHECK(weaponIndex("") == -1);
    CHECK(weaponIndex("Pistol") == -1);   // ids are lower-case
    CHECK(weaponIndex("rocket launcher") == -1);
    // The first slot is the one that never runs out.
    CHECK(makeWeapon(0)->stats().infiniteAmmo);
    for (std::size_t i = 1; i < kWeaponCount; ++i) CHECK_FALSE(makeWeapon(i)->stats().infiniteAmmo);
}
