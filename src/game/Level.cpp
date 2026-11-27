#include "game/Level.hpp"

#include "core/Record.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <sstream>
#include <utility>

namespace carnage {

namespace {

constexpr std::array<std::pair<std::string_view, Material>, 11> kMaterials{{
    {"concrete", Material::Concrete},      {"tile", Material::Tile},
    {"painted_metal", Material::PaintedMetal}, {"steel", Material::Steel},
    {"brass", Material::Brass},            {"flesh", Material::Flesh},
    {"plastic", Material::Plastic},        {"emissive", Material::Emissive},
    {"skin", Material::Skin},              {"cloth", Material::Cloth},
    {"leather", Material::Leather},
}};

using rec::Record;

std::optional<Material> materialFor(std::string_view n) {
    for (const auto& [name, m] : kMaterials)
        if (name == n) return m;
    return std::nullopt;
}

// Each parse* reads one record into `level`. They return false if the line
// must be dropped; the reason is in `r.error()`.

bool parseBlock(Record& r, LevelData& level) {
    Block b;
    float mn[2] = {}, mx[2] = {};
    r.vec("min", mn, 2, true);
    r.vec("max", mx, 2, true);

    const bool flat = r.has("top");
    if (flat) {
        if (r.has("h0") || r.has("h1") || r.has("axis")) r.fail("use either 'top' or 'h0/h1/axis', not both");
        b.h0 = b.h1 = r.number("top", 0.0f);
    } else {
        b.h0 = r.requiredNumber("h0");
        b.h1 = r.requiredNumber("h1");
        if (const auto a = r.raw("axis")) {
            if (*a == "x")      b.axis = 0;
            else if (*a == "z") b.axis = 1;
            else                r.fail("'axis' must be x or z: '" + std::string(*a) + "'");
        } else {
            r.fail("a sloped block needs 'axis'");
        }
    }
    b.base    = r.number("base", b.base);
    b.color   = r.vec3("color", b.color, false);
    b.visible = r.flag("visible", true);
    b.door    = r.integer("door", -1);
    if (const auto m = r.raw("material")) {
        if (const auto mat = materialFor(*m)) b.material = *mat;
        else r.fail("unknown material '" + std::string(*m) + "'");
    }
    r.finish();
    if (r.failed()) return false;

    if (!(mx[0] > mn[0]) || !(mx[1] > mn[1])) return r.fail("'max' must be greater than 'min' on both axes");
    b.min = {mn[0], mn[1]};
    b.max = {mx[0], mx[1]};
    level.world.add(b);
    return true;
}

bool parseLight(Record& r, LevelData& level) {
    WorldLight l;
    l.pos    = r.vec3("pos", l.pos, true);
    l.color  = r.vec3("color", l.color, false);
    l.radius = r.number("radius", l.radius);
    r.finish();
    if (r.failed()) return false;
    if (l.radius <= 0.0f) return r.fail("'radius' must be positive");
    level.world.addLight(l);
    return true;
}

bool parseTarget(Record& r, LevelData& level) {
    TargetSpawn t;
    t.pos    = r.vec3("pos", t.pos, true);
    t.patrol = r.number("patrol", 0.0f);
    r.finish();
    if (r.failed()) return false;
    if (t.patrol < 0.0f) return r.fail("'patrol' must not be negative");
    level.targets.push_back(t);
    return true;
}

bool parseEnemy(Record& r, LevelData& level) {
    EnemySpawn e;
    e.pos = r.vec3("pos", e.pos, true);
    if (const auto t = r.raw("type")) {
        if (*t == "melee")       e.type = EnemyType::Melee;
        else if (*t == "ranged") e.type = EnemyType::Ranged;
        else                     r.fail("unknown enemy type '" + std::string(*t) + "' (melee or ranged)");
    } else {
        r.fail("missing 'type'");
    }
    e.sector = r.integer("sector", -1);
    r.finish();
    if (r.failed()) return false;
    if (e.sector < -1) return r.fail("'sector' must be -1 (free roaming) or a sector id >= 0");
    level.enemies.push_back(e);
    return true;
}

bool parsePickup(Record& r, LevelData& level) {
    PickupSpawn p;
    if (const auto k = r.raw("kind")) {
        if (*k == "pipe")        p.kind = PickupKind::Pipe;
        else if (*k == "bat")    p.kind = PickupKind::Bat;
        else if (*k == "barrel") p.kind = PickupKind::Barrel;
        else                     r.fail("unknown pickup kind '" + std::string(*k) + "' (pipe, bat or barrel)");
    } else {
        r.fail("missing 'kind'");
    }
    p.pos = r.vec3("pos", p.pos, true);
    p.yaw = r.number("yaw", 0.0f);
    r.finish();
    if (r.failed()) return false;
    level.pickups.push_back(p);
    return true;
}

bool parseTrigger(Record& r, LevelData& level) {
    TriggerVolume t;
    const auto when = r.raw("when");
    const auto doIt = r.raw("do");
    if (!when) r.fail("missing 'when'");
    if (!doIt) r.fail("missing 'do'");

    if (when) {
        if (*when == "enter") {
            t.condition = TriggerCondition::PlayerEnters;
            t.min = r.vec3("min", t.min, true);
            t.max = r.vec3("max", t.max, true);
            if (!r.failed() && (t.max.x < t.min.x || t.max.y < t.min.y || t.max.z < t.min.z))
                r.fail("trigger 'max' must not be below 'min'");
        } else if (*when == "sector_cleared") {
            t.condition = TriggerCondition::SectorCleared;
            t.sector    = r.requiredInteger("sector");
            if (!r.failed() && t.sector < 0) r.fail("'sector' must be >= 0");
        } else {
            r.fail("unknown 'when' value '" + std::string(*when) + "' (enter or sector_cleared)");
        }
    }
    if (doIt) {
        if (*doIt == "open_door") {
            t.action = TriggerAction::OpenDoor;
            t.param  = r.requiredInteger("door");
            if (!r.failed() && t.param < 0) r.fail("'door' must be >= 0");
        } else if (*doIt == "checkpoint") {
            t.action = TriggerAction::Checkpoint;
            t.point  = r.vec3("point", t.point, true);
        } else if (*doIt == "complete") {
            t.action = TriggerAction::Complete;
        } else if (*doIt == "prompt") {
            t.action = TriggerAction::Prompt;
            if (const auto txt = r.raw("text")) {
                t.text = std::string(*txt);
                if (const std::string why = checkPromptText(t.text); !why.empty()) r.fail(why);
            } else {
                r.fail("missing 'text'");
            }
            t.time = r.number("time", t.time);
            if (!r.failed() && (t.time < 1.0f || t.time > 30.0f))
                r.fail("'time' must be between 1 and 30 seconds");
            if (const auto c = r.raw("clear_on")) {
                static constexpr std::pair<std::string_view, PromptClear> kClear[] = {
                    {"none", PromptClear::Timer},   {"jump", PromptClear::Jump},
                    {"slide", PromptClear::Slide},  {"vault", PromptClear::Vault},
                    {"wall_run", PromptClear::WallRun}, {"fire", PromptClear::Fire},
                    {"grab", PromptClear::Grab},    {"throw", PromptClear::Throw},
                };
                const auto it = std::find_if(std::begin(kClear), std::end(kClear),
                                             [&](const auto& e) { return e.first == *c; });
                if (it == std::end(kClear)) r.fail("unknown 'clear_on' value '" + std::string(*c) + "'");
                else t.clearOn = it->second;
            }
        } else {
            r.fail("unknown 'do' value '" + std::string(*doIt) + "' (open_door, checkpoint, complete or prompt)");
        }
    }
    r.finish();
    if (r.failed()) return false;
    level.triggers.push_back(t);
    return true;
}

bool parseLevelRecord(Record& r, LevelData& level) {
    std::string name, next, briefing;
    if (const auto n = r.raw("name")) name = std::string(*n);
    else r.fail("missing 'name'");

    if (const auto b = r.raw("briefing")) {
        briefing = std::string(*b);
        if (!validLevelId(briefing))
            r.fail("'briefing' must be a briefing id (lower-case letters, digits, _): '" + briefing + "'");
    }

    if (const auto n = r.raw("next_level")) {
        next = std::string(*n);
        if (!validLevelId(next))
            r.fail("'next_level' must be a level id (lower-case letters, digits, _): '" + next + "'");
    }
    TransitionType type = next.empty() ? TransitionType::Menu : TransitionType::Level;
    if (const auto t = r.raw("transition_type")) {
        if (*t == "menu") {
            type = TransitionType::Menu;
            if (!next.empty()) r.fail("'next_level' is set but transition_type=menu would ignore it");
        } else if (*t == "level") {
            type = TransitionType::Level;
            if (next.empty()) r.fail("transition_type=level needs 'next_level'");
        } else {
            r.fail("unknown transition_type '" + std::string(*t) + "' (menu or level)");
        }
    }
    r.finish();
    if (r.failed()) return false;
    level.name       = std::move(name);
    level.nextLevel  = std::move(next);
    level.briefing   = std::move(briefing);
    level.transition = type;
    return true;
}

} // namespace

bool validLevelId(std::string_view id) { return rec::validId(id); }

std::string levelPath(std::string_view assetDir, std::string_view id) {
    return std::string(assetDir) + "/levels/" + std::string(id) + ".lvl";
}

LevelTransition completionTransition(const LevelData& level) {
    LevelTransition t;
    if (level.transition == TransitionType::Level && validLevelId(level.nextLevel)) {
        t.type = TransitionType::Level;
        t.next = level.nextLevel;
    }
    if (validLevelId(level.briefing)) t.briefing = level.briefing;
    return t;
}

LevelLoad parseLevel(std::string_view text) {
    LevelLoad out;
    LevelData& level = out.level;
    const auto issue = [&](int line, std::string msg) { out.issues.push_back({line, std::move(msg)}); };

    bool haveLevel = false, haveSpawn = false;
    std::vector<int> triggerLines;  // source line of each accepted trigger

    rec::forEachRecordLine(text, [&](int lineNo, std::string_view kind, std::string_view rest) {
        Record r;
        if (!r.parse(rest)) { issue(lineNo, r.error()); return; }

        bool ok = false;
        if (kind == "level") {
            if (haveLevel) { issue(lineNo, "duplicate 'level' record ignored"); return; }
            ok = parseLevelRecord(r, level);
            haveLevel = haveLevel || ok;
        } else if (kind == "spawn") {
            if (haveSpawn) { issue(lineNo, "duplicate 'spawn' record ignored"); return; }
            level.spawn = r.vec3("pos", level.spawn, true);
            r.finish();
            ok = !r.failed();
            haveSpawn = haveSpawn || ok;
        } else if (kind == "block")   ok = parseBlock(r, level);
        else if (kind == "light")     ok = parseLight(r, level);
        else if (kind == "target")    ok = parseTarget(r, level);
        else if (kind == "enemy")     ok = parseEnemy(r, level);
        else if (kind == "pickup")    ok = parsePickup(r, level);
        else if (kind == "trigger") {
            ok = parseTrigger(r, level);
            if (ok) triggerLines.push_back(lineNo);
        } else {
            issue(lineNo, "unknown record type '" + std::string(kind) + "'");
            return;
        }
        if (!ok) issue(lineNo, std::string(kind) + ": " + r.error());
    });

    // Cross-checks. A trigger that can never work, or that fires the instant the
    // level starts, is dropped rather than left to misbehave in game.
    std::vector<TriggerVolume> kept;
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        const TriggerVolume& t = level.triggers[i];
        const int line = triggerLines[i];
        bool drop = false;
        if (t.action == TriggerAction::OpenDoor) {
            const auto blocks = level.world.blocks();
            if (std::none_of(blocks.begin(), blocks.end(), [&](const Block& b) { return b.door == t.param; })) {
                issue(line, "trigger: no block has door=" + std::to_string(t.param));
                drop = true;
            }
        }
        if (!drop && t.condition == TriggerCondition::SectorCleared &&
            std::none_of(level.enemies.begin(), level.enemies.end(),
                         [&](const EnemySpawn& e) { return e.sector == t.sector; })) {
            issue(line, "trigger: no enemy belongs to sector " + std::to_string(t.sector) +
                        " (it would fire immediately)");
            drop = true;
        }
        if (!drop) kept.push_back(t);
    }
    level.triggers = std::move(kept);

    if (!haveSpawn) issue(0, "no 'spawn' record; using the default spawn point");
    if (level.world.blocks().empty()) issue(0, "level has no 'block' records");
    return out;
}

LevelLoad loadLevelFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LevelLoad out;
        out.opened = false;
        out.issues.push_back({0, "cannot open level file '" + path + "'"});
        return out;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseLevel(ss.str());
}

void spawnTargets(const LevelData& level, Combat& combat) {
    for (const TargetSpawn& t : level.targets) combat.addTarget(Target::dummy(t.pos, t.patrol));
}

void spawnEnemies(const LevelData& level, Enemies& enemies, Combat& combat) {
    for (const EnemySpawn& e : level.enemies) enemies.add(combat, e.pos, e.type, e.sector);
}

void spawnPickups(const LevelData& level, Interact& interact) {
    for (const PickupSpawn& p : level.pickups) interact.add(p.kind, p.pos, p.yaw);
}

void installTriggers(const LevelData& level, TriggerSystem& triggers) {
    triggers.clear();
    for (const TriggerVolume& t : level.triggers) triggers.add(t);
}

} // namespace carnage
