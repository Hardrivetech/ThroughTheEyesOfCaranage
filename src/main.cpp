#include "core/GameLoop.hpp"
#include "core/Audio.hpp"
#include "core/Spatial.hpp"
#include "core/Input.hpp"
#include "core/InputMap.hpp"
#include "core/Log.hpp"
#include "core/Memory.hpp"
#include "core/Random.hpp"
#include "core/Renderer.hpp"
#include "core/Subsystems.hpp"
#include "game/Briefing.hpp"
#include "game/Camera.hpp"
#include "game/CameraFeel.hpp"
#include "game/Combat.hpp"
#include "game/Blind.hpp"
#include "game/Effects.hpp"
#include "game/Enemy.hpp"
#include "game/Hud.hpp"
#include "game/Interact.hpp"
#include "game/Level.hpp"
#include "game/LevelGraph.hpp"
#include "game/Movement.hpp"
#include "game/PlayerHealth.hpp"
#include "game/Triggers.hpp"
#include "game/Trauma.hpp"
#include "game/Viewmodel.hpp"
#include "game/Weapons.hpp"
#include "ui/Menu.hpp"
#include "game/World.hpp"
#include "gfx/Humanoid.hpp"
#include "dev/Console.hpp"
#include "dev/DevState.hpp"
#include "dev/DevUi.hpp"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

static_assert(carnage::key::Space == GLFW_KEY_SPACE && carnage::key::Escape == GLFW_KEY_ESCAPE &&
              carnage::key::LeftShift == GLFW_KEY_LEFT_SHIFT &&
              carnage::key::LeftControl == GLFW_KEY_LEFT_CONTROL &&
              carnage::key::LeftAlt == GLFW_KEY_LEFT_ALT && carnage::key::F1 == GLFW_KEY_F1 &&
              carnage::key::F12 == GLFW_KEY_F12 && carnage::key::Last == GLFW_KEY_MENU,
              "InputMap key constants must match GLFW");

using namespace carnage;

// Init order top -> bottom, shutdown bottom -> top.
// Memory first (everything allocates), log second (everything reports),
// renderer before input because input hooks the renderer's window.
constexpr std::array<Subsystem, 5> kSubsystems{{
    {"memory",   mem::init,      mem::shutdown},
    {"log",      log::init,      log::shutdown},
    {"renderer", renderer::init, renderer::shutdown},
    {"input",    input::init,    input::shutdown},
    {"audio",    audio::init,    audio::shutdown},
}};

namespace {

constexpr const char* kFirstLevel = "level01_escape";

audio::Sfx fireSound(const char* name) {
    const std::string_view n{name};
    if (n == "Fists")   return audio::Sfx::Punch;
    if (n == "SMG")     return audio::Sfx::Smg;
    if (n == "Shotgun") return audio::Sfx::Shotgun;
    if (n == "Rocket Launcher") return audio::Sfx::Rocket;
    return audio::Sfx::Pistol;
}

} // namespace

int main() {
    SubsystemManager subsystems{kSubsystems};
    if (!subsystems.initAll()) return EXIT_FAILURE;

    const std::string firstPath = levelPath(CARNAGE_ASSET_DIR, kFirstLevel);
    LevelLoad loaded = loadLevelFile(firstPath);
    for (const LevelIssue& i : loaded.issues) log::warn("{}:{}: {}", firstPath, i.line, i.message);
    if (!loaded.opened) return EXIT_FAILURE;
    // Check the whole level network once (portals, next_level, briefings, spawn names) so a
    // broken link is reported at startup rather than when a player walks into a gate.
    for (const std::string& p : checkLevelGraph(CARNAGE_ASSET_DIR, kFirstLevel).problems)
        log::warn("level network: {}", p);
    // `loaded` is reassigned in place on a level switch, so these references stay valid.
    const LevelData& level = loaded.level;
    World& world = loaded.level.world;  // mutable: doors open at runtime
    if (!renderer::uploadWorld(world)) return EXIT_FAILURE;

    // Props: render-only meshes sitting on the collision-only pillar blocks.
    const auto buildProps = [&] {
        renderer::clearProps();
        const int pillar = renderer::loadMesh("meshes/pillar.obj");
        if (pillar < 0) return;
        for (const Block& b : world.blocks()) {
            if (b.visible || b.door >= 0) continue;
            const glm::vec3 c{(b.min.x + b.max.x) * 0.5f, 0.0f, (b.min.y + b.max.y) * 0.5f};
            renderer::addProp(pillar, glm::translate(glm::mat4(1.0f), c),
                              glm::vec3(0.5f), Material::Concrete);
        }
    };
    buildProps();

    using Clock = std::chrono::steady_clock;
    Clock::time_point lastReport = Clock::now();
    std::uint64_t     lastTicks = 0, lastFrames = 0, ticks = 0, frames = 0;

    ui::Menu menu;
    ui::loadSettings(menu.settings(), "carnage_settings.ini");
    input::setCursorCaptured(false);  // start on the main menu
    renderer::setVsync(menu.settings().vsync);
    audio::setMasterVolume(menu.settings().volume);
    bool appliedVsync = menu.settings().vsync;
    constexpr float kFovKick = 18.0f;
    float fovPrev = menu.settings().fov, fovCur = menu.settings().fov;

    Camera             camera;
    TraumaManager      trauma;
    CameraFeel         feel;
    MovementController player{world};
    MoveState          lastState = player.state();

    Combat  combat{world};
    Effects effects;
    Hud     hud;
    Rng     gameplayRng;
    spawnTargets(level, combat);
    Enemies enemies;
    spawnEnemies(level, enemies, combat);

    std::array<std::unique_ptr<Weapon>, 5> weapons{
        makeFists(), makePistol(), makeSmg(), makeShotgun(), makeRocketLauncher()};
    std::size_t current = 0;  // start with Fists!
    weapons[current]->equip();
    std::unique_ptr<Weapon> offhand;  // optional left-hand weapon (dual wield)
    Blind blind;
    Interact interact;
    spawnPickups(level, interact);

    PlayerHealth  health;
    TriggerSystem triggers;
    installTriggers(level, triggers);
    PromptSystem  prompts;  // tutorial hints shown by `prompt` triggers
    BriefingPlayer  briefing;             // lore screen between levels; the sim is frozen while active
    LevelTransition afterBriefing;        // where to go once it ends
    bool            briefingResume = false;  // a console preview: just resume play afterwards
    std::string currentLevelId = kFirstLevel;
    SpawnPoint  arrival    = level.spawnPoint();  // where the running level was entered
    CheckpointTracker checkpoint;
    checkpoint.arrive(arrival);
    // Start at the level's spawn point, facing the way it says (not the controller's default).
    player.respawn(arrival.pos);
    camera.face(arrival.yaw);
    bool        deathHandled = false;
    bool        levelComplete = false;
    float       dmgFlash = 0.0f;
    std::string objectiveMsg;
    float       objectiveMsgTime = 0.0f;
    const auto  say = [&](const char* m) { objectiveMsg = m; objectiveMsgTime = 3.5f; };
    float       completeTimer = 0.0f;  // seconds since the level was completed
    constexpr float kCompleteFadeStart = 1.5f, kCompleteFadeEnd = 3.5f;

    // Dev tooling (console, dev menu, perf stats). Not part of gameplay state.
    dev::DevState     dev;
    dev::Console      console;
    dev::FrameProfile prof;
    dev::StatsInfo    statsInfo;
    Clock::time_point lastFrameStart = Clock::now();

    // Unloads the running session and rebuilds a pristine level. Shares the
    // spawn* helpers with startup so the two paths cannot diverge.
    const auto resetGame = [&] {
        world.resetDoors();
        installTriggers(level, triggers);  // also re-arms every trigger
        prompts.clear();
        health.reset();
        checkpoint.arrive(arrival);
        deathHandled = levelComplete = false;
        completeTimer = 0.0f;
        dmgFlash = 0.0f;
        objectiveMsg.clear();
        objectiveMsgTime = 0.0f;
        combat.clear();
        spawnTargets(level, combat);
        enemies.clear();
        spawnEnemies(level, enemies, combat);
        interact = Interact{};
        spawnPickups(level, interact);
        effects = Effects{};
        trauma  = TraumaManager{};
        feel    = CameraFeel{};
        blind   = Blind{};
        camera  = Camera{};
        camera.face(arrival.yaw);
        player.respawn(arrival.pos);
        lastState = player.state();
        weapons = {makeFists(), makePistol(), makeSmg(), makeShotgun(), makeRocketLauncher()};
        current = 0;
        weapons[current]->equip();
        offhand.reset();
        fovPrev = fovCur = menu.settings().fov;
    };

    // Replaces the running level with `id` (assets/levels/<id>.lvl) and starts it
    // fresh at its named spawn `spawnName` (default spawn if empty or unknown). Returns false, leaving the current level untouched, if it cannot be read.
    const auto switchLevel = [&](const std::string& id, const std::string& spawnName = {}) -> bool {
        const std::string path = levelPath(CARNAGE_ASSET_DIR, id);
        LevelLoad next = loadLevelFile(path);
        for (const LevelIssue& i : next.issues) log::warn("{}:{}: {}", path, i.line, i.message);
        if (!next.opened) return false;
        loaded = std::move(next);
        currentLevelId = id;
        if (!spawnName.empty() && !level.hasSpawn(spawnName))
            log::warn("level '{}' has no spawn '{}'; using its default spawn", id, spawnName);
        arrival = level.spawnPoint(spawnName);
        if (!renderer::uploadWorld(world)) {
            log::error("could not upload level '{}'", id);
            renderer::requestClose();
        }
        buildProps();
        resetGame();
        return true;
    };

    // Back to a fresh game: the first level at its default spawn. (Leaving to the main
    // menu from the hub must not make "Play" start in the hub.)
    const auto resetToFirstLevel = [&] {
        if (currentLevelId != kFirstLevel && switchLevel(kFirstLevel)) return;
        arrival = level.spawnPoint();
        resetGame();
    };

    // Starts the briefing `id` (assets/briefings/<id>.brf). Returns false, changing nothing,
    // if it cannot be read or has no beats; the caller then skips straight to the destination.
    const auto startBriefing = [&](const std::string& id) -> bool {
        const std::string path = briefingPath(CARNAGE_ASSET_DIR, id);
        BriefingLoad b = loadBriefingFile(path);
        for (const rec::Issue& i : b.issues) log::warn("{}:{}: {}", path, i.line, i.message);
        if (!b.opened || b.data.beats.empty()) return false;
        for (const rec::Issue& i : checkBriefingImages(b.data, CARNAGE_ASSET_DIR))
            log::warn("{}:{}: {}", path, i.line, i.message);
        renderer::clearBriefingImage();
        briefing.start(std::move(b.data));
        return true;
    };
    // Called once when a briefing finishes or is skipped: frees its image and moves on.
    const auto endBriefing = [&] {
        renderer::clearBriefingImage();
        briefing.reset();
        if (briefingResume) { briefingResume = false; return; }
        if (afterBriefing.type == TransitionType::Level) {
            if (switchLevel(afterBriefing.next, afterBriefing.spawn)) return;
            log::error("cannot load next level '{}'; returning to the menu", afterBriefing.next);
        }
        menu.showMain();
        input::setCursorCaptured(false);
        resetToFirstLevel();
    };

    // World sound with occlusion: geometry between the ear and the source
    // (a closed door, a wall) muffles it. One raycast per sound, game thread.
    glm::vec3 listenerPos{0.0f};
    const auto playAt3 = [&](audio::Sfx sfx, const glm::vec3& pos, float gain = 1.0f,
                             float pitch = 1.0f) {
        const glm::vec3 d = pos - listenerPos;
        const float     len = glm::length(d);
        float occ = 0.0f;
        if (len > 1.0f) {
            // 5 probe rays (centre + 4 offsets around the source) give a soft,
            // partial occlusion at doorways and corners instead of a hard on/off.
            const glm::vec3 dir = d / len;
            const glm::vec3 side = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)) + glm::vec3(1e-4f, 0, 0));
            const glm::vec3 up = glm::cross(side, dir);
            const glm::vec3 offs[5] = {{0, 0, 0}, side * 0.6f, -side * 0.6f, up * 0.6f, -up * 0.6f};
            int blocked = 0;
            for (const glm::vec3& o : offs) {
                const glm::vec3 t = pos + o - listenerPos;
                const float tl = glm::length(t);
                if (tl > 0.4f && world.raycast(listenerPos, t / tl, tl - 0.3f)) ++blocked;
            }
            occ = audio::occlusionFraction(blocked, 5);
        }
        audio::playAt(sfx, pos, gain, pitch, occ);
    };

    // ---- dev console commands ------------------------------------------
    {
        using dev::CmdResult;
        using Args = std::span<const std::string>;
        const auto flag = [](bool& b, Args a) {
            if (!a.empty()) { if (const auto v = dev::parseInt(a[0])) b = *v != 0; else b = !b; }
            else b = !b;
            return std::string(b ? "on" : "off");
        };
        static constexpr std::array<std::string_view, 5> kWeaponNames{
            "fists", "pistol", "smg", "shotgun", "rocket"};
        const auto weaponIndex = [](std::string_view n) -> int {
            if (const auto v = dev::parseInt(n)) return (*v >= 1 && *v <= 5) ? *v - 1 : -1;
            const std::string l = dev::toLower(n);
            for (std::size_t i = 0; i < kWeaponNames.size(); ++i)
                if (kWeaponNames[i].rfind(l, 0) == 0) return static_cast<int>(i);
            return -1;
        };

        console.add("help", "list commands", [&](Args) -> CmdResult { return console.helpText(); });
        console.add("clear", "clear the console", [&](Args) -> CmdResult { console.clearLog(); return std::string{}; });
        console.add("god", "toggle invulnerability [0|1]", [&](Args a) -> CmdResult { return "god " + flag(dev.god, a); });
        console.add("infammo", "toggle infinite ammo [0|1]", [&](Args a) -> CmdResult { return "infammo " + flag(dev.infAmmo, a); });
        console.add("noclip", "toggle free-fly [0|1]", [&](Args a) -> CmdResult { return "noclip " + flag(dev.noclip, a); });
        console.add("freeze", "toggle enemy AI freeze [0|1]", [&](Args a) -> CmdResult { return "freeze " + flag(dev.freezeAI, a); });
        console.add("stats", "toggle perf overlay [0|1]", [&](Args a) -> CmdResult { return "stats " + flag(dev.showStats, a); });
        console.add("heal", "restore health and armor", [&](Args) -> CmdResult {
            health.heal(PlayerHealth::kMaxHealth);
            health.addArmor(PlayerHealth::kMaxArmor);
            return std::string("healed");
        });
        console.add("ammo", "refill all magazines", [&](Args) -> CmdResult {
            for (auto& w : weapons) w->refill();
            if (offhand) offhand->refill();
            return std::string("ammo refilled");
        });
        console.add("give", "give <fists|pistol|smg|shotgun|rocket|1-5> [left]", [&](Args a) -> CmdResult {
            if (a.empty()) return std::unexpected("usage: give <weapon> [left]");
            const int i = weaponIndex(a[0]);
            if (i < 0) return std::unexpected("unknown weapon: " + a[0]);
            const auto idx = static_cast<std::size_t>(i);
            if (a.size() > 1 && dev::toLower(a[1]) == "left") {
                if (i == 0) offhand.reset();
                else { offhand = makeWeapon(idx); offhand->equip(); }
                return std::string("left hand: ") + std::string(kWeaponNames[idx]);
            }
            current = idx;
            weapons[current]->equip();
            return std::string("equipped ") + std::string(kWeaponNames[idx]);
        });
        console.add("stagger_all", "stagger every live enemy (seizable)", [&](Args) -> CmdResult {
            int n = 0;
            for (std::size_t i = 0; i < combat.targets().size(); ++i)
                if (const Target* t = combat.targetAt(i); t && t->alive() && enemies.stagger(i)) ++n;
            return std::format("staggered {}", n);
        });
        console.add("kill_all", "kill every live enemy", [&](Args) -> CmdResult {
            int n = 0;
            for (std::size_t i = 0; i < combat.targets().size(); ++i) {
                Target* t = combat.targetAt(i);
                if (t && t->alive()) { t->hp = 0.0f; t->respawnTimer = 3.0f; ++n; }
            }
            return std::format("killed {}", n);
        });
        console.add("spawn", "spawn <melee|ranged> [count] in front of you", [&](Args a) -> CmdResult {
            if (a.empty()) return std::unexpected("usage: spawn <melee|ranged> [count]");
            const std::string k = dev::toLower(a[0]);
            EnemyType type;
            if (k == "melee")       type = EnemyType::Melee;
            else if (k == "ranged") type = EnemyType::Ranged;
            else return std::unexpected("type must be melee or ranged");
            const int n = a.size() > 1 ? std::clamp(dev::parseInt(a[1]).value_or(1), 1, 16) : 1;
            const float yaw = camera.yaw();
            const glm::vec3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
            const glm::vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
            for (int i = 0; i < n; ++i) {
                const float off = (static_cast<float>(i) - 0.5f * static_cast<float>(n - 1)) * 1.4f;
                enemies.add(combat, player.position() + fwd * 7.0f + right * off, type, -1);
            }
            return std::format("spawned {} {}", n, k);
        });
        console.add("tp", "tp <x> <y> <z> | tp <spawn name>", [&](Args a) -> CmdResult {
            const auto spawnList = [&] {
                std::string names = "default";
                for (const NamedSpawn& sp : level.spawns) names += ", " + sp.name;
                return names;
            };
            if (a.size() == 1 && !dev::parseFloat(a[0])) {  // a spawn name
                if (a[0] != "default" && !level.hasSpawn(a[0]))
                    return std::unexpected("no spawn '" + a[0] + "' in this level (spawns: " + spawnList() + ")");
                const SpawnPoint sp = level.spawnPoint(a[0] == "default" ? std::string_view{} : std::string_view(a[0]));
                player.respawn(sp.pos);
                camera.face(sp.yaw);
                return std::format("teleported to spawn {} ({:.1f} {:.1f} {:.1f})", a[0], sp.pos.x, sp.pos.y, sp.pos.z);
            }
            if (a.size() < 3) return std::unexpected("usage: tp <x> <y> <z> | tp <spawn name> (spawns: " + spawnList() + ")");
            const auto x = dev::parseFloat(a[0]), y = dev::parseFloat(a[1]), z = dev::parseFloat(a[2]);
            if (!x || !y || !z) return std::unexpected("tp: coordinates must be numbers");
            player.respawn({*x, *y, *z});
            return std::format("teleported to {:.1f} {:.1f} {:.1f}", *x, *y, *z);
        });
        console.add("pos", "print player position and state", [&](Args) -> CmdResult {
            const glm::vec3 p = player.position();
            return std::format("{:.2f} {:.2f} {:.2f} | {} | {:.1f} m/s", p.x, p.y, p.z,
                               name(player.state()), player.horizontalSpeed());
        });
        console.add("respawn", "return to the last checkpoint", [&](Args) -> CmdResult {
            player.respawn(checkpoint.current().pos);
            camera.face(checkpoint.current().yaw);
            return std::string("respawned");
        });
        const auto doorError = [&](int id) {
            std::string ids;
            for (const int d : world.doorIds()) ids += (ids.empty() ? "" : ", ") + std::to_string(d);
            return std::format("no door {} in '{}' (doors: {})", id, currentLevelId, ids.empty() ? "none" : ids);
        };
        console.add("open_door", "open_door <id>", [&](Args a) -> CmdResult {
            const auto id = a.empty() ? std::nullopt : dev::parseInt(a[0]);
            if (!id) return std::unexpected("usage: open_door <id>");
            if (!world.hasDoor(*id)) return std::unexpected(doorError(*id));
            world.setDoorOpen(*id, true);
            return std::format("door {} opened", *id);
        });
        console.add("close_door", "close_door <id>", [&](Args a) -> CmdResult {
            const auto id = a.empty() ? std::nullopt : dev::parseInt(a[0]);
            if (!id) return std::unexpected("usage: close_door <id>");
            if (!world.hasDoor(*id)) return std::unexpected(doorError(*id));
            world.setDoorOpen(*id, false);
            return std::format("door {} closed", *id);
        });
        console.add("level", "level <id> [spawn] - load a level (assets/levels/<id>.lvl)", [&](Args a) -> CmdResult {
            if (a.empty() || !validLevelId(a[0])) return std::unexpected("usage: level <id> [spawn name]");
            const std::string spawnName = a.size() > 1 ? a[1] : std::string{};
            if (!switchLevel(a[0], spawnName)) return std::unexpected("cannot load level '" + a[0] + "' (see the log)");
            return std::format("loaded {} at spawn {}", a[0], spawnName.empty() ? "default" : spawnName);
        });
        console.add("win", "trigger the level-complete transition", [&](Args) -> CmdResult {
            if (!levelComplete) { levelComplete = true; completeTimer = 0.0f; }
            return std::string("level complete");
        });
        console.add("set_fov", "set_fov <50-130>", [&](Args a) -> CmdResult {
            const auto v = a.empty() ? std::nullopt : dev::parseFloat(a[0]);
            if (!v) return std::unexpected("usage: set_fov <degrees>");
            menu.settings().fov = std::clamp(*v, 50.0f, 130.0f);
            fovPrev = fovCur = menu.settings().fov;
            return std::format("fov {:.0f}", menu.settings().fov);
        });
        console.add("timescale", "timescale <0.05-2>", [&](Args a) -> CmdResult {
            const auto v = a.empty() ? std::nullopt : dev::parseFloat(a[0]);
            if (!v) return std::unexpected("usage: timescale <0.05-2>");
            dev.setTimeScale(*v);
            return std::format("timescale {:.2f}", dev.timeScale);
        });
        console.add("clear_decals", "remove all decals", [&](Args) -> CmdResult {
            effects.clearDecals();
            return std::string("decals cleared");
        });
        console.add("briefing", "briefing <id> - preview a briefing (close the console to watch)", [&](Args a) -> CmdResult {
            if (a.empty() || !validLevelId(a[0])) return std::unexpected("usage: briefing <id>");
            afterBriefing  = {};
            briefingResume = true;
            if (!startBriefing(a[0])) {
                briefingResume = false;
                return std::unexpected("cannot load briefing '" + a[0] + "' (see the log)");
            }
            return std::format("playing briefing {}", a[0]);
        });
        console.add("quit", "exit the game", [&](Args) -> CmdResult {
            renderer::requestClose();
            return std::string("bye");
        });
        console.print("DEV CONSOLE - TYPE HELP");
    }

    // Reused every frame so rendering doesn't allocate in steady state.
    std::vector<DrawBox>     boxes, viewBoxes;
    std::vector<OverlayRect> overlay;
    std::vector<PointLight>  lights;
    std::vector<std::pair<float, std::size_t>> lightPick;
    const auto               startTime = Clock::now();

    const LoopConfig cfg;
    const LoopStats  stats = runLoop(
        cfg,
        // Once per frame: events, then mouse look at render rate.
        [&] {
            renderer::pollEvents();
            if (!menu.active() && !briefing.active() && !health.dead && !dev.consoleOpen && !dev.menuOpen) {
                glm::vec2 d = input::consumeMouseDelta() * menu.settings().sensitivity;
                if (menu.settings().invertY) d.y = -d.y;
                camera.look(d);
            }
            else                (void)input::consumeMouseDelta();
        },
        // Fixed 60 Hz tick.
        [&](double dt) {
            const float fdt = static_cast<float>(dt) * dev.timeScale;

            // Dev console / dev menu own the tick while open (sim frozen).
            if (!menu.active() || dev.consoleOpen || dev.menuOpen) {
                const auto setDev = [&](bool con, bool men) {
                    dev.consoleOpen = con;
                    dev.menuOpen    = men;
                    input::setTextMode(con);
                    input::setCursorCaptured(!(con || men));
                };
                bool consumed = false;
                if (input::pressed(GLFW_KEY_GRAVE_ACCENT)) { setDev(!dev.consoleOpen, false); consumed = true; }
                else if (input::pressed(GLFW_KEY_F3))      { setDev(false, !dev.menuOpen);    consumed = true; }
                else if (input::pressed(GLFW_KEY_F4))      { dev.showStats = !dev.showStats; }
                else if (dev.consoleOpen) {
                    consumed = true;
                    for (const char c : input::drainTyped())
                        if (c != '`' && c != '~') console.type(c);
                    if (input::pressedRepeat(GLFW_KEY_BACKSPACE)) console.backspace();
                    if (input::pressedRepeat(GLFW_KEY_UP))        console.historyUp();
                    if (input::pressedRepeat(GLFW_KEY_DOWN))      console.historyDown();
                    if (input::pressed(GLFW_KEY_TAB))             console.tabComplete();
                    if (input::pressed(GLFW_KEY_ENTER))           (void)console.submit();
                    if (input::pressed(GLFW_KEY_ESCAPE))          setDev(false, false);
                } else if (dev.menuOpen) {
                    consumed = true;
                    if (input::pressed(GLFW_KEY_ESCAPE)) setDev(false, false);
                    else if (input::mousePressed(GLFW_MOUSE_BUTTON_LEFT))
                        if (const char* cmd = dev::menuHit(dev, input::cursorNdc()))
                            (void)console.execute(cmd);
                }
                if (consumed || dev.consoleOpen || dev.menuOpen) {
                    input::endTick();
                    ++ticks;
                    return;
                }
            }

            // A briefing owns the tick while it plays (ahead of the menu, so ESC skips it
            // instead of opening the pause menu). SPACE / ENTER advance, ESC skips.
            if (briefing.active()) {
                BriefingInput bi;
                bi.skip    = input::pressed(GLFW_KEY_ESCAPE);
                bi.advance = input::pressed(GLFW_KEY_SPACE) || input::pressed(GLFW_KEY_ENTER) ||
                             input::pressed(GLFW_KEY_KP_ENTER);
                const BriefingEvents be = briefing.update(static_cast<float>(dt), bi);
                if (be.beatStarted) {
                    if (const BriefingBeat* b = briefing.beat()) renderer::setBriefingImage(b->image.c_str());
                    switch (be.sfx) {
                        case BriefingSfx::Click:     audio::play(audio::Sfx::UiClick, 0.7f); break;
                        case BriefingSfx::Impact:    audio::play(audio::Sfx::Impact, 0.8f); break;
                        case BriefingSfx::Explosion: audio::play(audio::Sfx::Explosion, 0.8f, 0.0f, 0.8f); break;
                        case BriefingSfx::None:      break;
                    }
                }
                audio::setMenuMusic(briefing.active() && briefing.data().music);
                if (be.ended) endBriefing();
                input::endTick();
                ++ticks;
                return;
            }

            // Menus own the tick while active: the simulation is frozen.
            {
                ui::MenuInput mi2;
                mi2.cursor  = input::cursorNdc();
                mi2.clicked = input::mousePressed(GLFW_MOUSE_BUTTON_LEFT);
                mi2.held    = input::mouseDown(GLFW_MOUSE_BUTTON_LEFT);
                mi2.esc     = input::pressed(GLFW_KEY_ESCAPE) && !dev.consoleOpen && !dev.menuOpen;
                mi2.key     = input::lastKeyPressed();
                const bool wasActive = menu.active();
                switch (menu.update(mi2, renderer::aspect())) {
                    case ui::MenuAction::Play:  input::setCursorCaptured(true);  break;
                    case ui::MenuAction::Pause: input::setCursorCaptured(false); break;
                    case ui::MenuAction::Quit:  renderer::requestClose();        break;
                    case ui::MenuAction::MainMenu:
                        input::setCursorCaptured(false);
                        resetToFirstLevel();
                        break;
                    case ui::MenuAction::None:  break;
                }
                if (menu.settings().vsync != appliedVsync) {
                    appliedVsync = menu.settings().vsync;
                    renderer::setVsync(appliedVsync);
                }
                audio::setMasterVolume(menu.settings().volume);
                audio::setMusicVolume(menu.settings().music);
                audio::setSfxVolume(menu.settings().sfx);
                audio::setMenuMusic(menu.atMainMenu());
                if (menu.soundClick()) audio::play(audio::Sfx::UiClick, 0.6f);
                else if (menu.soundHover()) audio::play(audio::Sfx::UiHover, 0.4f);
                if (wasActive || menu.active()) {
                    input::endTick();
                    ++ticks;
                    return;
                }
            }
            const InputMap& km = menu.settings().keys;
            const auto kDown    = [&](Action a) { return input::down(km.key(a)); };
            const auto kPressed = [&](Action a) { return input::pressed(km.key(a)); };
            if (input::pressed(GLFW_KEY_F5))     renderer::reloadShaders();

            health.update(fdt);
            dmgFlash = std::max(0.0f, dmgFlash - fdt * 1.6f);
            objectiveMsgTime = std::max(0.0f, objectiveMsgTime - fdt);
            const bool alive = !health.dead;
            PromptEvents promptEvents;  // what the player did this tick, for prompts waiting on it

            if (health.dead && !deathHandled && health.readyToRespawn()) {
                // Fast checkpoint reload: keeps rooms, doors and cleared sectors,
                // restores vitals and puts the player at the last checkpoint.
                deathHandled = true;
                health.reset();
                deathHandled = false;
                player.respawn(checkpoint.current().pos);
                camera = Camera{};
                camera.face(checkpoint.current().yaw);
                trauma = TraumaManager{};
                feel   = CameraFeel{};
                blind  = Blind{};
                dmgFlash = 0.0f;
                say("CHECKPOINT RELOADED");
            }
            if (alive && kPressed(Action::Respawn)) {
                player.respawn(checkpoint.current().pos);
                camera.face(checkpoint.current().yaw);
            }

            // 1-5 equips the right hand; Alt+2..5 equips the left hand; Alt+1
            // empties it.
            const bool alt = kDown(Action::LeftHandMod);
            for (std::size_t i = 0; i < weapons.size(); ++i) {
                if (!input::pressed(km.weaponKey(i))) continue;
                if (alt) {
                    if (i == 0) offhand.reset();
                    else { offhand = makeWeapon(i); offhand->equip(); }
                } else if (i != current) {
                    current = i;
                    weapons[current]->equip();
                }
            }
            Weapon& weapon = *weapons[current];
            Weapon* left   = (offhand && !interact.holding()) ? offhand.get() : nullptr;
            if (dev.infAmmo) { weapon.refill(); if (offhand) offhand->refill(); }

            // Listener follows the camera; world sounds are spatialised from it.
            {
                const glm::quat lq = camera.orientation({0.0f, 0.0f});
                listenerPos = player.eyePosition(1.0f);
                audio::setListener(listenerPos, lq * glm::vec3(0.0f, 0.0f, -1.0f),
                                   lq * glm::vec3(1.0f, 0.0f, 0.0f));
            }

            MoveInput mi;
            mi.move = {
                static_cast<float>(kDown(Action::Right)) - static_cast<float>(kDown(Action::Left)),
                static_cast<float>(kDown(Action::Forward)) - static_cast<float>(kDown(Action::Back))};
            mi.sprint        = kDown(Action::Sprint);
            mi.crouchHeld    = kDown(Action::Crouch) || kDown(Action::CrouchAlt);
            mi.crouchPressed = kPressed(Action::Crouch) || kPressed(Action::CrouchAlt);
            mi.jumpPressed   = kPressed(Action::Jump);
            mi.jumpHeld      = kDown(Action::Jump);
            if (!alive) mi = MoveInput{};  // death locks movement input
            player.setNoclip(dev.noclip && alive);
            player.tick(fdt, camera.yaw(), mi);

            // Aim includes the current recoil offset, so the crosshair and
            // bullets agree with what the camera shows.
            const glm::quat q = camera.orientation(
                weapon.recoil().current() + (left ? left->recoil().current() : glm::vec2(0.0f)));
            Aim aim;
            aim.origin  = player.eyePosition(1.0f);
            aim.forward = q * glm::vec3(0.0f, 0.0f, -1.0f);
            aim.right   = q * glm::vec3(1.0f, 0.0f, 0.0f);
            aim.up      = q * glm::vec3(0.0f, 1.0f, 0.0f);
            aim.muzzle  = aim.origin + q * muzzleLocal(weapon.stats());

            TriggerInput trig;
            trig.held    = input::mouseDown(GLFW_MOUSE_BUTTON_LEFT);
            trig.pressed = input::mousePressed(GLFW_MOUSE_BUTTON_LEFT);
            // R reloads the right hand, or the left one when a left weapon is
            // equipped and Q is used; with only one weapon R is enough.
            trig.reload  = kPressed(Action::ReloadRight);
            if (!alive) trig = TriggerInput{};

            // While an object is held, left click swings it (like punching with
            // fists), so the right-hand weapon must not fire at the same time.
            const bool holdingItem = interact.holding();
            if (holdingItem) { trig.held = false; trig.pressed = false; }

            auto fireHand = [&](Weapon& w, const TriggerInput& t, Hand hand) {
                const float side = hand == Hand::Left ? -1.0f : 1.0f;
                Aim a = aim;
                a.muzzle = aim.origin + q * muzzleLocal(w.stats(), hand);
                const WeaponEvents e = w.tick(fdt, t, a, combat, gameplayRng);
                const float pitchJitter = 0.94f + 0.12f * gameplayRng.uniform();
                if (e.fired)  { audio::play(fireSound(w.stats().name), 1.0f, side * 0.35f, pitchJitter); promptEvents.fire = true; }
                if (e.dryFired)       audio::play(audio::Sfx::DryFire, 0.7f, side * 0.3f);
                if (e.reloadStarted)  audio::play(audio::Sfx::Reload, 0.8f, side * 0.3f);
                if (e.reloadFinished) audio::play(audio::Sfx::ReloadDone, 0.8f, side * 0.3f);
                if (e.fired) {
                    const float t01 = w.stats().trauma;
                    trauma.add(t01);
                    feel.onFire(std::clamp(t01 * 2.0f, 0.2f, 1.5f), side);
                    if (w.stats().flashSize > 0.0f) blind.add(w.stats().flashSize * 2.2f * menu.settings().flash);
                    effects.onShot(w.stats(), aim.origin + q * ejectionPortLocal(w.stats(), hand),
                                   aim.right, aim.up, aim.forward, player.velocity());
                }
            };
            fireHand(weapon, trig, Hand::Right);
            if (left) {
                TriggerInput lt;
                lt.held    = input::mouseDown(GLFW_MOUSE_BUTTON_RIGHT);
                lt.pressed = input::mousePressed(GLFW_MOUSE_BUTTON_RIGHT);
                lt.reload  = kPressed(Action::ReloadLeft);
                if (!alive) lt = TriggerInput{};
                fireHand(*left, lt, Hand::Left);
            }
            blind.update(fdt);

            // Grab / hurl (E) and bludgeon (left click while holding something).
            {
                // Living enemies: E throws a held one, or grabs a staggered one in
                // reach. Either consumes the press so Interact doesn't also react.
                bool grabPress = alive && kPressed(Action::Grab);
                if (grabPress && enemies.heldIndex() >= 0) {
                    enemies.throwHeld(aim.forward * 17.0f + glm::vec3(0.0f, 5.0f, 0.0f) +
                                      player.velocity() * 0.5f);
                    audio::play(audio::Sfx::Throw, 1.0f, 0.0f, 0.7f);
                    promptEvents.thrown = true;
                    trauma.add(0.35f);
                    feel.onFire(1.2f, -1.0f);
                    grabPress = false;
                } else if (grabPress && !interact.holding()) {
                    const int idx = enemies.findStaggered(aim.origin, aim.forward);
                    if (idx >= 0) {
                        enemies.grab(idx);
                        promptEvents.grab = true;
                        audio::play(audio::Sfx::Pickup, 0.9f, 0.0f, 0.7f);
                        trauma.add(0.15f);
                        grabPress = false;
                    }
                }
                if (enemies.heldIndex() >= 0) {
                    // Carry point: just ahead of the player, below eye level, not inside walls.
                    float dist = 1.1f;
                    if (const auto h = world.raycast(aim.origin, aim.forward, 1.4f))
                        dist = std::clamp(h->t - 0.5f, 0.3f, 1.1f);
                    enemies.setCarryPoint(aim.origin + aim.forward * dist - glm::vec3(0.0f, 1.2f, 0.0f));
                }
                InteractInput ii;
                ii.grabPressed  = grabPress;
                ii.swingPressed = alive && holdingItem && input::mousePressed(GLFW_MOUSE_BUTTON_LEFT);
                InteractContext ic;
                ic.eye       = aim.origin;
                ic.forward   = aim.forward;
                ic.right     = aim.right;
                ic.playerVel = player.velocity();
                const InteractEvents ie = interact.tick(fdt, ii, ic, combat, world);
                if (ie.grabbed) { audio::play(audio::Sfx::Pickup, 0.8f); promptEvents.grab = true; }
                if (ie.thrown)  { audio::play(audio::Sfx::Throw, 0.9f);  promptEvents.thrown = true; }
                if (ie.swung)   audio::play(audio::Sfx::Swing, 0.9f, 0.2f);
                if (ie.hit)     audio::play(audio::Sfx::Impact, 0.5f + 0.5f * ie.impact);
                if (const float ti = enemies.takeImpact(); ti > 0.0f) {
                    audio::play(audio::Sfx::Explosion, 0.5f + 0.4f * ti, 0.0f, 1.3f);
                    trauma.add(0.2f + 0.4f * ti);
                }
                if (ie.swung || ie.thrown || ie.hit || ie.exploded) {
                    trauma.add(0.25f * ie.impact + (ie.exploded ? 0.5f : 0.0f));
                    feel.onFire(1.2f * ie.impact, -1.0f);
                }
                if (ie.hit) hud.hitMarker(false);
            }
            combat.update(fdt);
            // Applies damage to the player and fans it out to the feel systems.
            const auto hurt = [&](float amount, glm::vec3 from, bool blast) {
                    if (dev.god || !health.damage(amount, blast)) return;
                    const glm::vec3 rel   = from - player.position();
                    const glm::vec3 right = camera.orientation({0.0f, 0.0f}) * glm::vec3(1.0f, 0.0f, 0.0f);
                    const float     side  = glm::dot(rel, right) >= 0.0f ? 1.0f : -1.0f;
                    const float     k     = std::clamp(amount / 25.0f, 0.2f, 1.0f);
                    trauma.add(0.25f + 0.5f * k);
                    feel.onDamage(k, side);
                    blind.add(0.08f + 0.2f * k);
                    dmgFlash = std::max(dmgFlash, 0.5f + 0.5f * k);
                    audio::play(audio::Sfx::HitBody, 0.9f);
                    if (health.dead) {
                        trauma.add(1.0f);
                        audio::play(audio::Sfx::Kill, 1.0f, 0.0f, 0.6f);
                    }
                };

                const bool aiRan = !dev.freezeAI;
                if (aiRan) enemies.tick(fdt, combat, world, player.position(), 1.7f);
                if (aiRan) for (const glm::vec3& fp : enemies.footsteps())
                    playAt3(audio::Sfx::Footstep, fp + glm::vec3(0.0f, 0.1f, 0.0f), 0.8f,
                                  0.75f + 0.15f * gameplayRng.uniform());
                if (aiRan) for (const EnemyAttack& a : enemies.attacks()) {
                    if (a.ranged) {
                        HitEvent h;
                        h.kind   = a.blocked ? HitKind::World : HitKind::Miss;
                        h.pos    = a.to;
                        h.normal = a.normal;
                        h.from   = a.from;
                        h.tracer = true;
                        effects.onHit(h, world);
                        playAt3(audio::Sfx::Pistol, a.from, 0.9f, 0.7f);
                    }
                    if (a.damage > 0.0f) hurt(a.damage, a.from, false);
                    else if (!a.ranged)  playAt3(audio::Sfx::Swing, a.from, 0.8f);
                }

            // Combat events drive feedback and knockback; combat itself stays
            // unaware of any of it.
            for (const HitEvent& e : combat.events()) {
                effects.onHit(e, world);
                // A heavy melee hit at close range staggers a still-living enemy so it
                // can be seized, instead of simply dying.
                if (e.melee && !e.killed && e.target >= 0 &&
                    (e.kind == HitKind::Body || e.kind == HitKind::Head) &&
                    glm::length(e.pos - player.position()) < 2.6f) {
                    const Target* tt = combat.targetAt(static_cast<std::size_t>(e.target));
                    if (tt && tt->alive() && (tt->hp <= 0.6f * tt->maxHp || e.damage >= 30.0f))
                        enemies.stagger(static_cast<std::size_t>(e.target));
                }
                if (e.kind == HitKind::Body)  playAt3(audio::Sfx::HitBody, e.pos, 0.9f);
                if (e.kind == HitKind::Head)  playAt3(audio::Sfx::HitHead, e.pos, 1.0f);
                if (e.kind == HitKind::World) playAt3(audio::Sfx::Impact, e.pos, 0.5f);
                if (e.kind == HitKind::Explosion) playAt3(audio::Sfx::Explosion, e.pos, 1.4f);
                if (e.killed) playAt3(audio::Sfx::Kill, e.pos, 0.9f);
                if (e.kind == HitKind::Body || e.kind == HitKind::Head) hud.hitMarker(e.killed);
                if (e.kind == HitKind::Explosion) {
                    const glm::vec3 body = player.position() + glm::vec3(0.0f, 0.9f, 0.0f);
                    player.impulse(blastImpulse(e, body));
                    const float d = glm::length(body - e.pos);
                    if (d < e.radius * 2.0f) {
                        const float k = 1.0f - d / (e.radius * 2.0f);
                        hurt(60.0f * k, e.pos, true);
                    }
                }
            }
            combat.clearEvents();

            // Objective triggers: sector clears open doors, volumes set checkpoints.
            std::optional<std::pair<std::string, std::string>> portal;  // level id, spawn name
            for (const TriggerEvent& te : triggers.update(player.position(), [&](int s) {
                     return enemies.aliveInSector(combat, s);
                 })) {
                switch (te.action) {
                    case TriggerAction::OpenDoor:
                        world.setDoorOpen(te.param, true);
                        audio::play(audio::Sfx::Slide, 0.9f, 0.0f, 0.5f);
                        trauma.add(0.3f);
                        if (te.condition == TriggerCondition::SectorCleared) say("SECTOR CLEARED - DOOR OPEN");
                        break;
                    case TriggerAction::Checkpoint:
                        checkpoint.reached(te.point, camera.yaw());
                        say("CHECKPOINT");
                        break;
                    case TriggerAction::Complete:
                        if (!levelComplete) completeTimer = 0.0f;
                        levelComplete = true;
                        say("LEVEL COMPLETE");
                        break;
                    case TriggerAction::Prompt:
                        prompts.show(expandPromptText(te.text, km), te.time, te.clearOn);
                        break;
                    case TriggerAction::Portal:
                        portal = {te.target, te.targetSpawn};  // acted on after the loop: it replaces the level
                        break;
                }
            }
            if (portal) {
                // A mission gate: leave this level for another, arriving at the gate's spawn.
                const std::string targetId = portal->first;
                if (switchLevel(targetId, portal->second)) {
                    audio::play(audio::Sfx::Pickup, 0.8f, 0.0f, 0.6f);
                    std::string title = currentLevelId;
                    std::transform(title.begin(), title.end(), title.begin(),
                                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
                    say(("ENTERING " + title).c_str());
                    input::endTick();
                    ++ticks;
                    return;
                }
                log::error("portal to '{}' failed; the level could not be loaded", targetId);
                say("PORTAL OFFLINE");
            }
            if (levelComplete) {
                completeTimer += fdt;
                // Fade out, then go where the level says: straight into the next
                // level, or (the default, or if that fails) back to the main menu.
                if (completeTimer >= kCompleteFadeEnd) {
                    const LevelTransition next = completionTransition(level);
                    if (!next.briefing.empty()) {
                        afterBriefing  = next;
                        briefingResume = false;
                        if (startBriefing(next.briefing)) {
                            input::endTick();
                            ++ticks;
                            return;
                        }
                        log::warn("briefing '{}' could not be played; continuing without it", next.briefing);
                    }
                    if (next.type == TransitionType::Level) {
                        if (switchLevel(next.next, next.spawn)) {
                            input::endTick();
                            ++ticks;
                            return;
                        }
                        log::error("cannot load next level '{}'; returning to the menu", next.next);
                    }
                    menu.showMain();
                    input::setCursorCaptured(false);
                    resetToFirstLevel();
                    input::endTick();
                    ++ticks;
                    return;
                }
            }
            effects.update(fdt, world);
            hud.update(fdt);

            // Movement feeds the camera feedback system.
            const MoveEvents& ev = player.events();
            promptEvents.jump    = ev.jumped || ev.wallJumped;
            promptEvents.slide   = ev.slideStarted;
            promptEvents.vault   = ev.vaultStarted;
            promptEvents.wallRun = ev.wallRunStarted;
            prompts.update(fdt, promptEvents);
            if (ev.landImpact > 3.0f)
                audio::play(audio::Sfx::Land, std::clamp(ev.landImpact / 14.0f, 0.3f, 1.2f));
            if (ev.jumped || ev.wallJumped) audio::play(audio::Sfx::Jump, 0.6f);
            if (ev.slideStarted)   audio::play(audio::Sfx::Slide, 0.7f);
            if (ev.wallRunStarted) audio::play(audio::Sfx::WallRun, 0.6f);
            if (ev.vaultStarted)   audio::play(audio::Sfx::Vault, 0.8f);
            if (ev.landImpact > 7.0f) trauma.add(std::clamp((ev.landImpact - 7.0f) / 20.0f, 0.0f, 0.6f));
            if (ev.slideStarted)      trauma.add(0.10f);
            if (ev.wallRunStarted)    trauma.add(0.12f);
            if (ev.wallJumped)        trauma.add(0.20f);
            if (ev.jumped)            trauma.add(0.04f);
            feel.tick(fdt, {player.velocity(), camera.yaw(), player.state(),
                            player.grounded(), ev});
            if (feel.footstep() && player.grounded())
                audio::play(audio::Sfx::Footstep, 0.45f, 0.0f, 0.9f + 0.2f * gameplayRng.uniform());

            // Speed-driven FOV kick, smoothed in the tick and interpolated on render.
            const float speedT    = std::clamp((player.horizontalSpeed() - 6.0f) / 10.0f, 0.0f, 1.0f);
            const float fovTarget = menu.settings().fov + kFovKick * menu.settings().fovKick * speedT;
            fovPrev = fovCur;
            fovCur += (fovTarget - fovCur) * (1.0f - std::exp(-6.0f * fdt));

            if (player.state() != lastState) {
                log::debug("{} -> {}", name(lastState), name(player.state()));
                lastState = player.state();
            }

            trauma.update(fdt);
            input::endTick();
            ++ticks;
        },
        [&](double alpha) {
            const auto tFrame = Clock::now();
            dev::FrameProfile::ema(prof.frameMs,
                std::chrono::duration<float, std::milli>(tFrame - lastFrameStart).count());
            lastFrameStart = tFrame;
            if (briefing.active()) {
                // Lore screen: backdrop image (or plain dark), then the text overlay.
                overlay.clear();
                renderer::drawBriefingBackdrop(std::chrono::duration<float>(Clock::now() - startTime).count());
                buildBriefingOverlay(briefing, renderer::aspect(), overlay);
                if (dev.consoleOpen)
                    dev::drawConsole(console, renderer::aspect(), std::fmod(std::chrono::duration<float>(Clock::now() - startTime).count(), 1.0f) < 0.5f, overlay);
                renderer::drawOverlay(overlay);
                renderer::endFrame();
                ++frames;
                return;
            }
            const float   a      = static_cast<float>(alpha);
            const Weapon& weapon = *weapons[current];
            const Weapon* left   = (offhand && !interact.holding()) ? offhand.get() : nullptr;
            camera.fovDegrees = fovPrev + (fovCur - fovPrev) * a;
            ShakeOffset shakeOff = trauma.sample(a);
            const ShakeOffset fo = feel.sample(a);
            shakeOff.translation += fo.translation;
            shakeOff.rotation    += fo.rotation;
            shakeOff.translation *= menu.settings().shake;
            shakeOff.rotation    *= menu.settings().shake;
            const glm::vec2 recoilSum = weapon.recoil().offset(a) +
                                        (left ? left->recoil().offset(a) : glm::vec2(0.0f));
            const glm::mat4 view = camera.view(player.eyePosition(a), shakeOff,
                                               player.cameraRoll(a), recoilSum);
            const glm::mat4 proj = camera.projection(renderer::aspect());

            renderer::SceneParams sp;
            sp.view                = view;
            sp.projection          = proj;
            sp.viewmodelProjection = glm::perspective(glm::radians(65.0f), renderer::aspect(),
                                                      0.01f, 10.0f);
            sp.time  = std::chrono::duration<float>(Clock::now() - startTime).count();
            sp.shake = trauma.shake() * menu.settings().shake;
            sp.flash = blind.sample(a);

            boxes.clear();
            // 3D Player Character body (visible in shadows & when looking down)
            gfx::HumanoidPose playerPose;
            playerPose.position = player.position();
            playerPose.yaw      = camera.yaw();
            playerPose.time     = sp.time;
            playerPose.speed    = player.horizontalSpeed();
            playerPose.state    = player.state();
            playerPose.hideHead = true;  // prevent camera clipping inside head
            gfx::buildPlayerBodyBoxes(playerPose, boxes);

            combat.collect(a, boxes);
            effects.collect(a, boxes);
            effects.collectDecals(player.eyePosition(a), boxes);
            interact.collect(a, boxes);

            // Doors are dynamic boxes: drawn only while closed (solid).
            for (const Block& b : world.blocks()) {
                if (b.door < 0 || !b.enabled) continue;
                const float top = b.topHeight();
                boxes.push_back({{(b.min.x + b.max.x) * 0.5f, (top + b.base) * 0.5f, (b.min.y + b.max.y) * 0.5f},
                                 {(b.max.x - b.min.x) * 0.5f, (top - b.base) * 0.5f, (b.max.y - b.min.y) * 0.5f},
                                 glm::mat3(1.0f), b.color, false, b.material});
            }

            lights.clear();
            // Room lights: the nearest few of the level's fixtures (kMaxLights is
            // 8 and muzzle/explosion lights need headroom).
            {
                const glm::vec3 eyeP = player.eyePosition(a);
                lightPick.clear();
                const auto wl = world.lights();
                for (std::size_t i = 0; i < wl.size(); ++i)
                    lightPick.emplace_back(glm::distance(wl[i].pos, eyeP), i);
                const std::size_t n = std::min<std::size_t>(4, lightPick.size());
                std::partial_sort(lightPick.begin(), lightPick.begin() + static_cast<std::ptrdiff_t>(n),
                                  lightPick.end());
                for (std::size_t i = 0; i < n; ++i) {
                    if (lightPick[i].first > 40.0f) break;
                    const WorldLight& l = wl[lightPick[i].second];
                    lights.push_back({l.pos, l.color, l.radius});
                }
            }
            auto flashLight = [&](const Weapon& w, Hand h) {
                if (!w.flashing()) return;
                const glm::vec3 muzzle =
                    glm::vec3(glm::inverse(view) * glm::vec4(muzzleLocal(w.stats(), h), 1.0f));
                lights.push_back({muzzle, glm::vec3(5.0f, 3.2f, 1.4f) * w.flashT(), 9.0f});
            };
            flashLight(weapon, Hand::Right);
            if (left) flashLight(*left, Hand::Left);
            combat.collectLights(a, lights);
            effects.collectLights(a, lights);
            sp.lights = lights;

            viewBoxes.clear();
            buildViewmodel(weapon, a, viewBoxes, Hand::Right, left != nullptr);
            if (left) buildViewmodel(*left, a, viewBoxes, Hand::Left, true);
            interact.buildHeld(a, viewBoxes);
            const auto tCollected = Clock::now();
            if (menu.atMainMenu()) renderer::drawMenuBackground(sp.time);  // skip the 3D pass entirely
            else                   renderer::renderScene(sp, boxes, viewBoxes);
            const auto tScene = Clock::now();

            overlay.clear();
            if (!menu.active()) {
                hud.build(weapon, left, effects, proj * view, renderer::aspect(), overlay);
                if (enemies.heldIndex() >= 0)
                    Hud::prompt(keyName(menu.settings().keys.key(Action::Grab)), "THROW", renderer::aspect(), overlay);
                else if (!interact.holding() &&
                         (interact.canGrab() ||
                          enemies.findStaggered(player.eyePosition(1.0f),
                                                camera.orientation({0.0f, 0.0f}) * glm::vec3(0.0f, 0.0f, -1.0f)) >= 0))
                    Hud::prompt(keyName(menu.settings().keys.key(Action::Grab)), "GRAB", renderer::aspect(), overlay);
                if (!health.dead && !levelComplete && prompts.active())
                    Hud::tutorial(prompts.text(), prompts.alpha(), renderer::aspect(), overlay);
                if (!health.dead) {
                    if (levelComplete) {
                        Hud::objective("LEVEL COMPLETE", renderer::aspect(), overlay);
                    } else if (objectiveMsgTime > 0.0f) {
                        Hud::objective(objectiveMsg, renderer::aspect(), overlay);
                    } else if (const int s = triggers.activeSector(); s >= 0) {
                        const int total = enemies.totalInSector(s);
                        const int left  = enemies.aliveInSector(combat, s);
                        Hud::objective("CLEAR THE SECTOR: " + std::to_string(total - left) + "/" + std::to_string(total),
                                       renderer::aspect(), overlay);
                    }
                }
            }
            if (!menu.active()) {
                Hud::vitals(health.health01(), health.armor01(), dmgFlash, health.deathFade(),
                            health.dead, renderer::aspect(), overlay);
            }
            menu.draw(overlay);
            if (levelComplete && !menu.active()) {
                const float f = std::clamp((completeTimer - kCompleteFadeStart) /
                                           (kCompleteFadeEnd - kCompleteFadeStart), 0.0f, 1.0f);
                if (f > 0.0f) overlay.push_back({{-1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, f}});
            }
            if (dev.menuOpen)
                dev::drawMenu(dev, input::cursorNdc(), renderer::aspect(), overlay);
            if (!menu.active() || dev.consoleOpen || dev.menuOpen)
                dev::drawStats(dev, prof, statsInfo, renderer::aspect(), overlay);
            if (dev.consoleOpen)
                dev::drawConsole(console, renderer::aspect(), std::fmod(sp.time, 1.0f) < 0.5f, overlay);
            renderer::drawOverlay(overlay);
            {
                const auto tEnd = Clock::now();
                const auto ms = [](auto d) { return std::chrono::duration<float, std::milli>(d).count(); };
                dev::FrameProfile::ema(prof.collectMs, ms(tCollected - tFrame));
                dev::FrameProfile::ema(prof.sceneMs,   ms(tScene - tCollected));
                dev::FrameProfile::ema(prof.overlayMs, ms(tEnd - tScene));
                dev::FrameProfile::ema(prof.cpuMs,     ms(tEnd - tFrame));
                prof.boxes        = static_cast<int>(boxes.size() + viewBoxes.size());
                prof.overlayRects = static_cast<int>(overlay.size());
            }

            renderer::endFrame();
            ++frames;

            const auto now = Clock::now();
            if (now - lastReport >= std::chrono::seconds(1)) {
                log::info("{} ticks/s | {} fps | {} | {:.1f} m/s | {} {}/{} | {} particles",
                          ticks - lastTicks, frames - lastFrames, name(player.state()),
                          player.horizontalSpeed(), weapon.stats().name, weapon.ammo(),
                          weapon.magSize(), effects.particleCount());
                statsInfo.ticksPerSec  = static_cast<int>(ticks - lastTicks);
                statsInfo.fps          = static_cast<int>(frames - lastFrames);
                statsInfo.particles    = static_cast<int>(effects.particleCount());
                statsInfo.decals       = static_cast<int>(effects.decalCount());
                statsInfo.enemiesAlive = 0;
                for (const Target& t : combat.targets()) statsInfo.enemiesAlive += t.alive() ? 1 : 0;
                lastTicks = ticks; lastFrames = frames; lastReport = now;
            }
        },
        [] { return renderer::shouldClose(); });

    log::info("Exiting after {} ticks, {} frames", stats.ticks, stats.frames);
    return EXIT_SUCCESS;
}
