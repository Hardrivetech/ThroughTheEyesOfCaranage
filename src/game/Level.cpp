#include "game/Level.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
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

std::string_view trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const std::size_t a = s.find_first_not_of(ws);
    if (a == std::string_view::npos) return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t i = s.find(sep, start);
        out.push_back(s.substr(start, i == std::string_view::npos ? i : i - start));
        if (i == std::string_view::npos) return out;
        start = i + 1;
    }
}

bool parseFloat(std::string_view s, float& out) {
    if (s.empty()) return false;
    float v = 0.0f;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool parseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
    out = v;
    return true;
}

// The key=value pairs of one record. Getters remember the first problem;
// finish() additionally rejects keys nobody asked for (typos must be visible).
class Record {
public:
    // Returns false (and sets the error) if a token is not key=value or repeats a key.
    // A value may be double-quoted to contain spaces: text="HELLO THERE".
    bool parse(std::string_view rest) {
        constexpr std::string_view ws = " \t";
        std::size_t i = 0;
        while (i < rest.size()) {
            const std::size_t a = rest.find_first_not_of(ws, i);
            if (a == std::string_view::npos) break;
            std::size_t tokEnd = rest.find_first_of(ws, a);
            if (tokEnd == std::string_view::npos) tokEnd = rest.size();
            const std::string_view tok = rest.substr(a, tokEnd - a);

            const std::size_t eq = rest.find('=', a);
            if (eq == std::string_view::npos || eq >= tokEnd || eq == a)
                return fail("expected key=value, got '" + std::string(tok) + "'");
            const std::string_view key = rest.substr(a, eq - a);

            std::string_view value;
            if (eq + 1 < rest.size() && rest[eq + 1] == '"') {
                const std::size_t close = rest.find('"', eq + 2);
                if (close == std::string_view::npos)
                    return fail("unterminated quote in '" + std::string(key) + "'");
                value = rest.substr(eq + 2, close - eq - 2);
                i = close + 1;
                if (i < rest.size() && ws.find(rest[i]) == std::string_view::npos)
                    return fail("unexpected text after the closing quote of '" + std::string(key) + "'");
            } else {
                value = rest.substr(eq + 1, tokEnd - eq - 1);
                i = tokEnd;
                if (value.empty()) return fail("expected key=value, got '" + std::string(tok) + "'");
            }
            if (std::any_of(kv_.begin(), kv_.end(), [&](const Kv& e) { return e.key == key; }))
                return fail("duplicate key '" + std::string(key) + "'");
            kv_.push_back({key, value, false});
        }
        return true;
    }

    bool has(std::string_view key) const {
        return std::any_of(kv_.begin(), kv_.end(), [&](const Kv& e) { return e.key == key; });
    }

    std::optional<std::string_view> raw(std::string_view key) {
        for (Kv& e : kv_)
            if (e.key == key) { e.used = true; return e.value; }
        return std::nullopt;
    }

    bool fail(std::string msg) {
        if (err_.empty()) err_ = std::move(msg);
        return false;
    }
    bool failed() const { return !err_.empty(); }
    const std::string& error() const { return err_; }

    float number(std::string_view key, float def) {
        const auto v = raw(key);
        if (!v) return def;
        float out = def;
        if (!parseFloat(*v, out)) fail("'" + std::string(key) + "' is not a number: '" + std::string(*v) + "'");
        return out;
    }
    float requiredNumber(std::string_view key) {
        if (!has(key)) { fail("missing '" + std::string(key) + "'"); return 0.0f; }
        return number(key, 0.0f);
    }
    int integer(std::string_view key, int def) {
        const auto v = raw(key);
        if (!v) return def;
        int out = def;
        if (!parseInt(*v, out)) fail("'" + std::string(key) + "' is not an integer: '" + std::string(*v) + "'");
        return out;
    }
    int requiredInteger(std::string_view key) {
        if (!has(key)) { fail("missing '" + std::string(key) + "'"); return 0; }
        return integer(key, 0);
    }
    bool flag(std::string_view key, bool def) {
        const auto v = raw(key);
        if (!v) return def;
        if (*v == "1" || *v == "true")  return true;
        if (*v == "0" || *v == "false") return false;
        fail("'" + std::string(key) + "' must be 0 or 1: '" + std::string(*v) + "'");
        return def;
    }

    // Reads `n` comma-separated floats into `out`. Returns false when absent
    // (an error if `required`) or malformed.
    bool vec(std::string_view key, float* out, int n, bool required) {
        const auto v = raw(key);
        if (!v) {
            if (required) fail("missing '" + std::string(key) + "'");
            return false;
        }
        const auto parts = split(*v, ',');
        if (static_cast<int>(parts.size()) != n) {
            fail("'" + std::string(key) + "' needs " + std::to_string(n) + " comma-separated numbers");
            return false;
        }
        for (int i = 0; i < n; ++i) {
            if (!parseFloat(parts[static_cast<std::size_t>(i)], out[i])) {
                fail("'" + std::string(key) + "' has a bad number: '" +
                     std::string(parts[static_cast<std::size_t>(i)]) + "'");
                return false;
            }
        }
        return true;
    }
    glm::vec3 vec3(std::string_view key, glm::vec3 def, bool required) {
        float v[3] = {def.x, def.y, def.z};
        return vec(key, v, 3, required) ? glm::vec3(v[0], v[1], v[2]) : def;
    }

    // Call once all expected keys were read.
    void finish() {
        for (const Kv& e : kv_)
            if (!e.used) { fail("unknown key '" + std::string(e.key) + "'"); return; }
    }

private:
    struct Kv {
        std::string_view key, value;
        bool             used;
    };
    std::vector<Kv> kv_;
    std::string     err_;
};

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
    std::string name, next;
    if (const auto n = r.raw("name")) name = std::string(*n);
    else r.fail("missing 'name'");

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
    level.transition = type;
    return true;
}

} // namespace

bool validLevelId(std::string_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}

std::string levelPath(std::string_view assetDir, std::string_view id) {
    return std::string(assetDir) + "/levels/" + std::string(id) + ".lvl";
}

LevelTransition completionTransition(const LevelData& level) {
    if (level.transition == TransitionType::Level && validLevelId(level.nextLevel))
        return {TransitionType::Level, level.nextLevel};
    return {};
}

LevelLoad parseLevel(std::string_view text) {
    LevelLoad out;
    LevelData& level = out.level;
    const auto issue = [&](int line, std::string msg) { out.issues.push_back({line, std::move(msg)}); };

    bool haveLevel = false, haveSpawn = false;
    std::vector<int> triggerLines;  // source line of each accepted trigger

    int lineNo = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;

        // A '#' starts a comment unless it is inside a quoted value.
        {
            bool inQuote = false;
            for (std::size_t c = 0; c < line.size(); ++c) {
                if (line[c] == '"') inQuote = !inQuote;
                else if (line[c] == '#' && !inQuote) { line = line.substr(0, c); break; }
            }
        }
        line = trim(line);
        if (line.empty()) continue;

        const std::size_t sp = line.find_first_of(" \t");
        const std::string_view kind = line.substr(0, sp);
        const std::string_view rest = sp == std::string_view::npos ? std::string_view{} : line.substr(sp);

        Record r;
        if (!r.parse(rest)) { issue(lineNo, r.error()); continue; }

        bool ok = false;
        if (kind == "level") {
            if (haveLevel) { issue(lineNo, "duplicate 'level' record ignored"); continue; }
            ok = parseLevelRecord(r, level);
            haveLevel = haveLevel || ok;
        } else if (kind == "spawn") {
            if (haveSpawn) { issue(lineNo, "duplicate 'spawn' record ignored"); continue; }
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
            continue;
        }
        if (!ok) issue(lineNo, std::string(kind) + ": " + r.error());
    }

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
