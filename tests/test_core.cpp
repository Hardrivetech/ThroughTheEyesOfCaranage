#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/catch_approx.hpp>

#include "core/InputMap.hpp"
#include "game/Enemy.hpp"
#include "game/PlayerHealth.hpp"
#include "game/Triggers.hpp"
#include "core/Spatial.hpp"
#include "gfx/ParticleSystem.hpp"
#include "core/Random.hpp"
#include "ui/Menu.hpp"
#include "dev/Console.hpp"

#include <cstdio>
#include <fstream>
#include <string>

using namespace carnage;
using Catch::Matchers::WithinAbs;

namespace {
struct TempFile {
    std::string path = "carnage_test_settings.ini";
    ~TempFile() { std::remove(path.c_str()); }
};
} // namespace

TEST_CASE("Settings round-trip through the INI file", "[settings]") {
    TempFile tmp;
    ui::Settings a;
    a.sensitivity = 1.7f; a.fov = 100.0f; a.shake = 0.5f; a.vsync = true; a.volume = 0.25f;
    a.invertY = true; a.flash = 0.4f; a.fovKick = 0.3f; a.music = 0.6f; a.sfx = 0.2f;
    ui::saveSettings(a, tmp.path.c_str());

    ui::Settings b;
    REQUIRE(ui::loadSettings(b, tmp.path.c_str()));
    CHECK_THAT(b.sensitivity, WithinAbs(1.7, 1e-4));
    CHECK_THAT(b.fov, WithinAbs(100.0, 1e-4));
    CHECK_THAT(b.shake, WithinAbs(0.5, 1e-4));
    CHECK(b.vsync);
    CHECK_THAT(b.volume, WithinAbs(0.25, 1e-4));
    CHECK(b.invertY);
    CHECK_THAT(b.flash, WithinAbs(0.4, 1e-4));
    CHECK_THAT(b.fovKick, WithinAbs(0.3, 1e-4));
    CHECK_THAT(b.music, WithinAbs(0.6, 1e-4));
    CHECK_THAT(b.sfx, WithinAbs(0.2, 1e-4));
}

TEST_CASE("Settings loader clamps out-of-range values", "[settings]") {
    TempFile tmp;
    {
        std::ofstream f(tmp.path);
        f << "sensitivity 99\nfov 5\nshake -3\nvolume 7\nmusic 9\nsfx -1\nflash 50\nfov_kick 50\n";
    }
    ui::Settings s;
    REQUIRE(ui::loadSettings(s, tmp.path.c_str()));
    CHECK(s.sensitivity <= 3.0f);
    CHECK(s.fov >= 70.0f);
    CHECK(s.shake >= 0.0f);
    CHECK(s.volume <= 1.0f);
    CHECK(s.music <= 1.0f);
    CHECK(s.sfx >= 0.0f);
    CHECK(s.flash <= 1.5f);
    CHECK(s.fovKick <= 1.5f);
}

TEST_CASE("Missing settings file leaves defaults and reports failure", "[settings]") {
    ui::Settings s;
    CHECK_FALSE(ui::loadSettings(s, "does_not_exist.ini"));
    CHECK_THAT(s.fov, WithinAbs(90.0, 1e-4));
}

TEST_CASE("Console tokenizer and parsing", "[dev]") {
    using namespace carnage::dev;
    const auto t = tokenize("  tp 1  -2.5 \"a b\" ");
    REQUIRE(t.size() == 4);
    CHECK(t[0] == "tp");
    CHECK(t[3] == "a b");
    CHECK(tokenize("   ").empty());
    CHECK(parseFloat("-2.5").value() == Catch::Approx(-2.5f));
    CHECK_FALSE(parseFloat("2x").has_value());
    CHECK(parseInt("12").value() == 12);
    CHECK_FALSE(parseInt("").has_value());
}

TEST_CASE("Console dispatch, history and completion", "[dev]") {
    using namespace carnage::dev;
    Console c;
    int calls = 0;
    std::string lastArg;
    c.add("Echo", "echo arg", [&](std::span<const std::string> a) -> CmdResult {
        ++calls;
        if (a.empty()) return std::unexpected("need arg");
        lastArg = a[0];
        return a[0];
    });
    c.add("exit", "x", [](std::span<const std::string>) -> CmdResult { return std::string{}; });

    CHECK(c.execute("ECHO hi").value() == "hi");
    CHECK(calls == 1);
    CHECK(lastArg == "hi");
    CHECK_FALSE(c.execute("echo").has_value());
    CHECK_FALSE(c.execute("nope").has_value());
    CHECK(c.log().back().error);

    CHECK(c.execute("exit").has_value());
    c.historyUp();
    CHECK(c.input() == "exit");
    c.historyUp();
    CHECK(c.input() == "nope");
    c.historyDown();
    CHECK(c.input() == "exit");
    c.historyDown();
    CHECK(c.input().empty());

    c.clearInput();
    c.type('e');
    c.tabComplete();
    CHECK(c.input() == "e");  // ambiguous: echo/exit share only "e"
    c.type('c');
    c.tabComplete();
    CHECK(c.input() == "echo ");

    c.clearInput();
    c.type('a'); c.backspace(); c.backspace();
    CHECK(c.input().empty());
    c.type('\n');
    CHECK(c.input().empty());
    for (std::size_t i = 0; i < Console::kMaxLog + 10; ++i) c.print("x");
    CHECK(c.log().size() == Console::kMaxLog);
    CHECK(c.helpText().find("echo - echo arg") != std::string::npos);
}

TEST_CASE("Menu navigation", "[menu]") {
    constexpr float kAspect = 16.0f / 9.0f;
    ui::Menu menu;
    REQUIRE(menu.active());
    REQUIRE(menu.atMainMenu());

    SECTION("clicking PLAY starts the game") {
        ui::MenuInput in;
        in.cursor  = {0.0f, 0.10f};
        in.clicked = true;
        CHECK(menu.update(in, kAspect) == ui::MenuAction::Play);
        CHECK_FALSE(menu.active());
        CHECK(menu.soundClick());
    }

    SECTION("ESC while playing opens the pause menu, ESC again resumes") {
        ui::MenuInput click;
        click.cursor = {0.0f, 0.10f};
        click.clicked = true;
        (void)menu.update(click, kAspect);

        ui::MenuInput esc;
        esc.esc = true;
        CHECK(menu.update(esc, kAspect) == ui::MenuAction::Pause);
        CHECK(menu.active());
        CHECK_FALSE(menu.atMainMenu());
        CHECK(menu.update(esc, kAspect) == ui::MenuAction::Play);
    }

    SECTION("pause MAIN MENU button returns the reset action") {
        ui::MenuInput click;
        click.cursor = {0.0f, 0.10f};
        click.clicked = true;
        (void)menu.update(click, kAspect);
        ui::MenuInput esc;
        esc.esc = true;
        (void)menu.update(esc, kAspect);

        ui::MenuInput mm;
        // QUIT TO MAIN MENU is the last of the six pause rows.
        mm.cursor  = {0.0f, ui::itemCenterY(ui::Screen::Pause, 5)};
        mm.clicked = true;
        CHECK(menu.update(mm, kAspect) == ui::MenuAction::MainMenu);
        CHECK(menu.atMainMenu());
    }

    SECTION("hover raises a one-shot sound flag only on change") {
        ui::MenuInput in;
        in.cursor = {0.0f, 0.10f};
        (void)menu.update(in, kAspect);
        CHECK(menu.soundHover());
        (void)menu.update(in, kAspect);
        CHECK_FALSE(menu.soundHover());
    }
}

TEST_CASE("InputMap defaults are unique and rebind swaps on conflict", "[input]") {
    InputMap m;
    for (std::size_t i = 0; i < kActionCount; ++i)
        for (std::size_t j = i + 1; j < kActionCount; ++j) CHECK(m.key(i) != m.key(j));

    CHECK(m.key(Action::Jump) == 32);
    CHECK(m.rebind(Action::Jump, 'E'));  // E belongs to Grab: they swap
    CHECK(m.key(Action::Jump) == 'E');
    CHECK(m.key(Action::Grab) == 32);
    CHECK_FALSE(m.rebind(Action::Jump, key::Escape));
    CHECK(m.key(Action::Jump) == 'E');

    m.reset();
    CHECK(m == InputMap{});
}

TEST_CASE("Key bindings round-trip through the INI and old files still load", "[input][settings]") {
    TempFile tmp;
    ui::Settings a;
    REQUIRE(a.keys.rebind(Action::Sprint, 'F'));
    ui::saveSettings(a, tmp.path.c_str());
    ui::Settings b;
    REQUIRE(ui::loadSettings(b, tmp.path.c_str()));
    CHECK(b.keys == a.keys);
    CHECK(b.keys.key(Action::Sprint) == 'F');

    {
        std::ofstream f(tmp.path);
        f << "fov 100\n";  // pre-rebinding file
    }
    ui::Settings c;
    REQUIRE(ui::loadSettings(c, tmp.path.c_str()));
    CHECK(c.keys == InputMap{});

    {
        std::ofstream f(tmp.path);
        f << "bind_jump 69\nbind_grab 69\n";  // duplicate: rejected
    }
    ui::Settings d;
    REQUIRE(ui::loadSettings(d, tmp.path.c_str()));
    CHECK(d.keys == InputMap{});
}

TEST_CASE("Enemy state machine walks IDLE -> PATROL -> CHASE -> ATTACK", "[ai]") {
    const EnemyConfig cfg;
    Enemy e;
    constexpr float dt = 1.0f / 60.0f;
    const Perception none{50.0f, false};

    for (int i = 0; i < 60 * 2; ++i) (void)stepState(e, cfg, none, dt);
    CHECK(e.state == AiState::Patrol);

    (void)stepState(e, cfg, {10.0f, true}, dt);
    CHECK(e.state == AiState::Chase);

    (void)stepState(e, cfg, {1.0f, true}, dt);
    CHECK(e.state == AiState::Attack);

    int landed = 0;
    for (int i = 0; i < 60; ++i) landed += stepState(e, cfg, {1.0f, true}, dt) ? 1 : 0;
    CHECK(landed >= 1);
}

TEST_CASE("Enemy gives up the chase after losing sight", "[ai]") {
    const EnemyConfig cfg;
    Enemy e;
    enterState(e, AiState::Chase);
    constexpr float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60; ++i) (void)stepState(e, cfg, {20.0f, false}, dt);
    CHECK(e.state == AiState::Chase);  // still searching
    for (int i = 0; i < 60 * 3; ++i) (void)stepState(e, cfg, {20.0f, false}, dt);
    CHECK(e.state == AiState::Patrol);
}

TEST_CASE("Rng is deterministic and uniform() stays in [0,1)", "[rng]") {
    Rng a{1234}, b{1234};
    for (int i = 0; i < 1000; ++i) {
        const float x = a.uniform();
        CHECK(x == b.uniform());
        CHECK(x >= 0.0f);
        CHECK(x < 1.0f);
    }
}

TEST_CASE("PlayerHealth splits damage with armor and honours i-frames", "[health]") {
    PlayerHealth h;
    REQUIRE(h.damage(20.0f));
    CHECK(h.armor == Catch::Approx(40.0f));   // 50% of 20 absorbed
    CHECK(h.health == Catch::Approx(90.0f));
    CHECK_FALSE(h.damage(20.0f));             // inside i-frames
    CHECK(h.damage(20.0f, true));             // explosions bypass them
    h.update(1.0f);
    CHECK(h.damage(5.0f));
}

TEST_CASE("PlayerHealth death, fade and reset", "[health]") {
    PlayerHealth h;
    h.armor = 0.0f;
    REQUIRE(h.damage(500.0f));
    CHECK(h.dead);
    CHECK(h.health == 0.0f);
    CHECK_FALSE(h.damage(10.0f, true));
    CHECK(h.deathFade() == 0.0f);
    h.update(PlayerHealth::kFadeTime);
    CHECK(h.deathFade() == Catch::Approx(1.0f));
    CHECK_FALSE(h.readyToRespawn());
    h.update(PlayerHealth::kRespawnDelay);
    CHECK(h.readyToRespawn());
    h.reset();
    CHECK_FALSE(h.dead);
    CHECK(h.health == PlayerHealth::kMaxHealth);
}

TEST_CASE("Triggers fire once on enter and on sector clear", "[triggers]") {
    TriggerSystem ts;
    TriggerVolume enter;
    enter.min = {0, 0, 0}; enter.max = {1, 1, 1};
    enter.action = TriggerAction::Checkpoint;
    ts.add(enter);
    TriggerVolume clear;
    clear.condition = TriggerCondition::SectorCleared;
    clear.sector = 3; clear.param = 7;
    ts.add(clear);

    int alive = 2;
    auto fn = [&](int s) { return s == 3 ? alive : 0; };
    CHECK(ts.update({5, 5, 5}, fn).empty());
    CHECK(ts.activeSector() == 3);
    CHECK(ts.update({0.5f, 0.5f, 0.5f}, fn).size() == 1);
    CHECK(ts.update({0.5f, 0.5f, 0.5f}, fn).empty());  // only once
    alive = 0;
    const auto& ev = ts.update({5, 5, 5}, fn);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].param == 7);
    CHECK(ts.activeSector() == -1);
    ts.reset();
    CHECK(ts.activeSector() == 3);
}

TEST_CASE("Ranged attack aborts on lost LOS and respects its cooldown", "[ai]") {
    const EnemyConfig cfg;
    Enemy e;
    e.type = EnemyType::Ranged;
    constexpr float dt = 1.0f / 60.0f;
    enterState(e, AiState::Attack);
    for (int i = 0; i < 5; ++i) (void)stepState(e, cfg, {8.0f, true}, dt);
    (void)stepState(e, cfg, {8.0f, false}, dt);   // LOS lost mid-windup
    CHECK(e.state == AiState::Chase);

    enterState(e, AiState::Attack);
    int shots = 0;
    for (int i = 0; i < 60 * 3; ++i) shots += stepState(e, cfg, {8.0f, true}, dt) ? 1 : 0;
    // One shot per (windup + fireInterval) = 1.45 s -> 2 shots in 3 s.
    CHECK(shots == 2);
}

TEST_CASE("Spatial audio attenuates with distance and pans by angle", "[audio]") {
    const audio::Listener l;  // at origin, facing -Z, right = +X
    const auto near  = audio::spatialize(l, {0.0f, 0.0f, -1.0f});
    const auto far   = audio::spatialize(l, {0.0f, 0.0f, -20.0f});
    const auto gone  = audio::spatialize(l, {0.0f, 0.0f, -100.0f});
    CHECK(near.gain > far.gain);
    CHECK(far.gain > 0.0f);
    CHECK(gone.gain == 0.0f);

    const auto right = audio::spatialize(l, {10.0f, 0.0f, 0.0f});
    const auto left  = audio::spatialize(l, {-10.0f, 0.0f, 0.0f});
    CHECK(right.pan > 0.9f);
    CHECK(left.pan < -0.9f);
    CHECK(std::abs(audio::spatialize(l, {0.0f, 0.0f, -10.0f}).pan) < 0.01f);

    const auto behind = audio::spatialize(l, {0.0f, 0.0f, 10.0f});
    const auto ahead  = audio::spatialize(l, {0.0f, 0.0f, -10.0f});
    CHECK(behind.gain < ahead.gain);
}


namespace {
World floorWorld() {
    World w;
    w.add(Block{{-50.0f, -50.0f}, {50.0f, 50.0f}, 0.0f, 0.0f, 0, glm::vec3(0.5f), false,
                Material::Concrete, true, -0.2f, true, -1});
    return w;
}
} // namespace

TEST_CASE("Staggered enemies can be grabbed and live ones cannot", "[enemy][seize]") {
    World w = floorWorld();
    Combat combat(w);
    Enemies enemies;
    enemies.add(combat, {0.0f, 0.0f, -2.0f}, EnemyType::Melee);
    const glm::vec3 eye{0.0f, 1.6f, 0.0f}, fwd{0.0f, 0.0f, -1.0f};

    CHECK(enemies.findStaggered(eye, fwd) < 0);  // alive and unstaggered
    REQUIRE(enemies.stagger(0));
    CHECK_FALSE(enemies.stagger(0));             // already seized
    CHECK(enemies.findStaggered(eye, fwd) == 0);
    CHECK(combat.targets()[0].alive());

    enemies.grab(0);
    CHECK(enemies.heldIndex() == 0);
    CHECK(combat.targets()[0].alive());

    // A held body is ignored by player attacks.
    enemies.setCarryPoint({0.0f, 0.4f, -1.0f});
    enemies.tick(1.0f / 60.0f, combat, w, {0.0f, 0.0f, 0.0f}, 1.7f);
    CHECK(combat.targets()[0].held);
    combat.hitscan(eye, fwd, eye, 20.0f, 50.0f);
    CHECK(combat.targets()[0].hp == combat.targets()[0].maxHp);
}

TEST_CASE("Stagger expires back into the chase", "[enemy][seize]") {
    World w = floorWorld();
    Combat combat(w);
    Enemies enemies;
    enemies.add(combat, {0.0f, 0.0f, -6.0f}, EnemyType::Melee);
    REQUIRE(enemies.stagger(0));
    for (int i = 0; i < 200; ++i) enemies.tick(1.0f / 60.0f, combat, w, {0.0f, 0.0f, 0.0f}, 1.7f);
    CHECK(enemies.enemies()[0].state != AiState::Seized);
}

TEST_CASE("Human shield absorbs hitscan and takes the damage", "[enemy][seize]") {
    World w = floorWorld();
    Combat combat(w);
    EnemyConfig cfg;
    cfg.spread = 0.0f;
    Enemies enemies(cfg);
    enemies.add(combat, {0.0f, 0.0f, -8.0f}, EnemyType::Ranged);  // shooter (index 0)
    enemies.add(combat, {0.0f, 0.0f, -1.0f}, EnemyType::Melee);   // shield (index 1)
    REQUIRE(enemies.stagger(1));
    enemies.grab(1);
    REQUIRE(enemies.heldIndex() == 1);

    int shots = 0, shielded = 0;
    float playerDamage = 0.0f;
    for (int i = 0; i < 240; ++i) {
        enemies.setCarryPoint({0.0f, 0.0f, -1.0f});
        enemies.tick(1.0f / 60.0f, combat, w, {0.0f, 0.0f, 0.0f}, 1.7f);
        for (const EnemyAttack& a : enemies.attacks()) {
            if (!a.ranged) continue;
            ++shots;
            shielded += a.shielded ? 1 : 0;
            playerDamage += a.damage;
        }
        combat.clearEvents();
    }
    CHECK(shots > 0);
    CHECK(shielded == shots);
    CHECK(playerDamage == 0.0f);
    CHECK(combat.targets()[1].hp < combat.targets()[1].maxHp);
}

TEST_CASE("Thrown enemy hurts others and lands on the floor", "[enemy][seize]") {
    World w = floorWorld();
    Combat combat(w);
    Enemies enemies;
    enemies.add(combat, {0.0f, 0.0f, -1.0f}, EnemyType::Melee);  // thrown (0)
    enemies.add(combat, {0.0f, 0.0f, -7.0f}, EnemyType::Melee);  // victim (1)
    enemies.stagger(0);
    enemies.grab(0);
    enemies.throwHeld({0.0f, 5.0f, -17.0f});
    CHECK(enemies.heldIndex() < 0);
    for (int i = 0; i < 120; ++i) {
        enemies.tick(1.0f / 60.0f, combat, w, {20.0f, 0.0f, 20.0f}, 1.7f);
        combat.clearEvents();
    }
    CHECK(combat.targets()[1].hp < combat.targets()[1].maxHp);
    CHECK(combat.targets()[0].hp < combat.targets()[0].maxHp);   // impact damage to the thrown body
    CHECK(enemies.enemies()[0].pos.y >= -0.01f);
}

TEST_CASE("Occlusion lowers cutoff and gain smoothly", "[audio]") {
    CHECK(audio::occlusionFraction(0, 5) == 0.0f);
    CHECK(audio::occlusionFraction(2, 5) == Catch::Approx(0.4f));
    CHECK(audio::occlusionFraction(1, 0) == 0.0f);

    const float clear = audio::lowpassCutoff(1.0f, 0.0f);
    const float half  = audio::lowpassCutoff(1.0f, 0.5f);
    const float full  = audio::lowpassCutoff(1.0f, 1.0f);
    CHECK(clear > 15000.0f);
    CHECK(half < clear);
    CHECK(full < half);
    CHECK(full == Catch::Approx(600.0f).margin(1.0f));
    CHECK(audio::lowpassCutoff(50.0f, 0.0f) < clear);  // air absorption

    const float aLow  = audio::lowpassCoeff(600.0f, 44100.0f);
    const float aHigh = audio::lowpassCoeff(20000.0f, 44100.0f);
    CHECK(aLow > 0.0f);
    CHECK(aLow < aHigh);
    CHECK(aHigh <= 1.0f);
    CHECK(audio::occlusionGain(1.0f) < audio::occlusionGain(0.0f));
}

TEST_CASE("DecalSystem is a bounded ring", "[gfx]") {
    DecalSystem d;
    for (std::size_t i = 0; i < DecalSystem::kMax * 2; ++i) d.add({});
    CHECK(d.size() == DecalSystem::kMax);
    std::vector<DrawBox> out;
    d.collect(out, {0.0f, 0.0f, 0.0f});
    CHECK(!out.empty());
    out.clear();
    d.collect(out, {1000.0f, 0.0f, 0.0f});  // everything culled
    CHECK(out.empty());
    d.clear();
    CHECK(d.size() == 0);
}

TEST_CASE("ParticleSystem caps its pool and expires particles", "[gfx]") {
    World w;
    w.add(Block{{-50.0f, -50.0f}, {50.0f, 50.0f}, 0.0f, 0.0f, 0, glm::vec3(0.5f), false,
                Material::Concrete, true, -0.2f, true, -1});
    ParticleSystem ps;
    ps.burst(ParticleSystem::kMax + 500, {0, 1, 0}, {0, 1, 0}, 1, 2, 0.5f, 0.02f, 0.03f,
             {1, 0, 0}, 1.0f, 1.2f, 14.0f, 0.1f, 0.2f, false, true);
    CHECK(ps.size() == ParticleSystem::kMax);
    DecalSystem decals;
    for (int i = 0; i < 120; ++i) ps.update(1.0f / 60.0f, w, &decals);
    CHECK(ps.size() == 0);
    CHECK(decals.size() > 0);  // landed blood drops left splats
}

TEST_CASE("Only dead, unclaimed targets are grabbable", "[interact]") {
    Target t = Target::dummy({0, 0, 0});
    CHECK_FALSE(t.isCorpse());   // alive
    t.hp = 0.0f;
    CHECK(t.isCorpse());
    t.claimed = true;
    CHECK_FALSE(t.isCorpse());
}
