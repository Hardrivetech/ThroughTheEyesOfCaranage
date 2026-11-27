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

using rec::Record;

bool parseSpawn(Record& r, LevelData& level, bool& haveDefault, bool allowDefault) {
    SpawnPoint p;
    p.pos = r.vec3("pos", p.pos, true);
    p.yaw = glm::radians(r.number("yaw", 0.0f));
    std::string name;
    if (const auto n = r.raw("name")) {
        name = std::string(*n);
        if (!rec::validId(name)) r.fail("'name' must be an id (lower-case letters, digits, _): '" + name + "'");
        else if (name == "default") r.fail("'default' is reserved for the unnamed spawn");
    }
    r.finish();
    if (r.failed()) return false;
    if (name.empty()) {
        if (!allowDefault) return r.fail("an included file cannot set the default spawn; give it a name=");
        if (haveDefault) return r.fail("duplicate default spawn ignored");
        haveDefault   = true;
        level.spawn    = p.pos;
        level.spawnYaw = p.yaw;
        return true;
    }
    if (level.hasSpawn(name)) return r.fail("duplicate spawn name '" + name + "' ignored");
    level.spawns.push_back({std::move(name), p});
    return true;
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
    // `mat=` is the short form of `material=`; give one of them. An unknown name keeps the block
    // (a missing wall would break the level far worse than a wrong texture) with the default
    // material, and the loader reports it.
    std::optional<std::string_view> matName = r.raw("mat");
    if (const auto longForm = r.raw("material")) {
        if (matName) r.fail("use either 'mat' or 'material', not both");
        else         matName = longForm;
    }
    if (matName) {
        if (const auto mat = materialFromName(*matName)) b.material = *mat;
        else r.warn("unknown material '" + std::string(*matName) + "'; using " + std::string(materialName(b.material)));
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
        } else if (*doIt == "portal") {
            t.action = TriggerAction::Portal;
            if (const auto l = r.raw("level")) {
                t.target = std::string(*l);
                if (!validLevelId(t.target)) r.fail("'level' must be a level id (lower-case letters, digits, _): '" + t.target + "'");
            } else {
                r.fail("missing 'level'");
            }
            if (const auto sp = r.raw("spawn")) {
                t.targetSpawn = std::string(*sp);
                if (!rec::validId(t.targetSpawn)) r.fail("'spawn' must be a spawn name (lower-case letters, digits, _): '" + t.targetSpawn + "'");
            }
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
            r.fail("unknown 'do' value '" + std::string(*doIt) + "' (open_door, checkpoint, complete, prompt or portal)");
        }
    }
    r.finish();
    if (r.failed()) return false;
    level.triggers.push_back(t);
    return true;
}

bool parseLevelRecord(Record& r, LevelData& level) {
    std::string name, next, briefing, nextSpawn;
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
    if (const auto sp = r.raw("next_spawn")) {
        nextSpawn = std::string(*sp);
        if (!rec::validId(nextSpawn))
            r.fail("'next_spawn' must be a spawn name (lower-case letters, digits, _): '" + nextSpawn + "'");
        else if (next.empty())
            r.fail("'next_spawn' needs 'next_level'");
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
    level.nextSpawn  = std::move(nextSpawn);
    level.transition = type;
    return true;
}


// ---- includes and the environment ------------------------------------------------------------

constexpr int kMaxIncludeDepth = 8;     // files nested inside files
constexpr int kMaxIncludes     = 256;   // include records expanded in one load (stops fan-out blow-ups)
constexpr std::size_t kMaxIncludedBytes = 16u << 20;

struct TriggerLoc {
    std::string file;  // "" = the top-level file
    int         line = 0;
};

// One file parsed on its own (with its own includes already merged in, in its coordinates).
struct Piece {
    LevelData               data;
    std::vector<TriggerLoc> locs;  // where each trigger in data.triggers came from
    bool                    haveSpawn = false;
};

struct ParseCtx {
    const IncludeReader&     reader;
    std::vector<rec::Issue>& issues;
    std::vector<std::string> stack;  // normalised paths of the files being parsed, outermost first
    int                      includeCount = 0;
};

bool parseEnvironment(Record& r, LevelData& level) {
    Environment e;
    if (const auto v = r.raw("visibility")) {
        float m = 0.0f;
        if (!rec::parseFloat(*v, m)) r.fail("'visibility' is not a number: '" + std::string(*v) + "'");
        else if (m < Environment::kMinVisibility || m > Environment::kMaxVisibility)
            r.fail("'visibility' must be between " + std::to_string(static_cast<int>(Environment::kMinVisibility)) + " and " +
                   std::to_string(static_cast<int>(Environment::kMaxVisibility)) + " metres");
        else e.haze = Environment::hazeForVisibility(m);
    }
    e.farPlane = r.number("far", e.farPlane);
    if (!r.failed() && (e.farPlane < Environment::kMinFar || e.farPlane > Environment::kMaxFar))
        r.fail("'far' must be between " + std::to_string(static_cast<int>(Environment::kMinFar)) + " and " +
               std::to_string(static_cast<int>(Environment::kMaxFar)) + " metres");
    e.shadowRadius = r.number("shadow", e.shadowRadius);
    if (!r.failed() && (e.shadowRadius < Environment::kMinShadow || e.shadowRadius > Environment::kMaxShadow))
        r.fail("'shadow' must be between " + std::to_string(static_cast<int>(Environment::kMinShadow)) + " and " +
               std::to_string(static_cast<int>(Environment::kMaxShadow)) + " metres");
    e.snow = r.number("snow", e.snow);
    if (!r.failed() && (e.snow < 0.0f || e.snow > 1.0f)) r.fail("'snow' must be between 0 and 1");
    r.finish();
    if (r.failed()) return false;
    level.environment    = e;
    level.hasEnvironment = true;
    return true;
}

// Moves everything in `child` into `parent` through `place`. Ids and names are adjusted so that
// two copies of the same piece (or two different pieces) do not collide silently.
void mergePiece(Piece& parent, const Piece& child, const Placement& place, const std::string& prefix,
                int doorOffset, int sectorOffset, const std::string& file, int includeLine, ParseCtx& ctx) {
    LevelData&       p = parent.data;
    const LevelData& c = child.data;
    const auto problem = [&](std::string msg) { ctx.issues.push_back({includeLine, std::move(msg), ctx.stack.empty() ? std::string{} : ctx.stack.back()}); };

    // Door ids are global: reusing one means opening either door opens both.
    {
        const std::vector<int> existing = p.world.doorIds();
        for (const int id : c.world.doorIds()) {
            const int shifted = id + doorOffset;
            if (std::binary_search(existing.begin(), existing.end(), shifted))
                problem("include '" + file + "': door id " + std::to_string(shifted) +
                        " is already used; opening one would open both (give the include a door_offset)");
        }
    }

    for (const Block& b : c.world.blocks()) {
        Block nb = place.block(b);
        if (nb.door >= 0) nb.door += doorOffset;
        p.world.add(nb);
    }
    for (const WorldLight& l : c.world.lights()) {
        WorldLight nl = l;
        nl.pos = place.point(l.pos);
        p.world.addLight(nl);
    }
    for (const TargetSpawn& t : c.targets) {
        TargetSpawn nt = t;
        nt.pos  = place.point(t.pos);
        nt.axis = place.direction(t.axis);
        p.targets.push_back(nt);
    }
    for (const EnemySpawn& e : c.enemies) {
        EnemySpawn ne = e;
        ne.pos = place.point(e.pos);
        if (ne.sector >= 0) ne.sector += sectorOffset;
        p.enemies.push_back(ne);
    }
    for (const PickupSpawn& k : c.pickups) {
        PickupSpawn nk = k;
        nk.pos = place.point(k.pos);
        nk.yaw = k.yaw + place.yawRadians();
        p.pickups.push_back(nk);
    }
    for (std::size_t i = 0; i < c.triggers.size(); ++i) {
        const TriggerVolume& v = c.triggers[i];
        TriggerVolume t = v;
        if (v.condition == TriggerCondition::PlayerEnters) place.box(v.min, v.max, t.min, t.max);
        else t.sector += sectorOffset;
        if (v.action == TriggerAction::OpenDoor) t.param += doorOffset;
        if (v.action == TriggerAction::Checkpoint) t.point = place.point(v.point);
        p.triggers.push_back(t);
        parent.locs.push_back(child.locs[i]);
    }
    for (const NamedSpawn& s : c.spawns) {
        const std::string name = prefix.empty() ? s.name : prefix + "_" + s.name;
        if (p.hasSpawn(name)) {
            problem("include '" + file + "': spawn name '" + name + "' is already defined (give the include a prefix=)");
            continue;
        }
        SpawnPoint sp = s.point;
        sp.pos = place.point(s.point.pos);
        sp.yaw = s.point.yaw + place.yawRadians();
        p.spawns.push_back({name, sp});
    }
    for (const std::string& f : c.includedFiles)
        if (std::find(p.includedFiles.begin(), p.includedFiles.end(), f) == p.includedFiles.end()) p.includedFiles.push_back(f);
    if (std::find(p.includedFiles.begin(), p.includedFiles.end(), file) == p.includedFiles.end()) p.includedFiles.push_back(file);
}

Piece parsePiece(ParseCtx& ctx, std::string_view text, const std::string& file, bool top, int depth);

bool parseInclude(Record& r, ParseCtx& ctx, Piece& piece, int depth, int lineNo) {
    std::string pathText;
    if (const auto p = r.raw("path")) pathText = std::string(*p);
    else r.fail("missing 'path'");
    const glm::vec3 offset = r.vec3("offset", glm::vec3(0.0f), false);

    int turns = 0;
    if (const auto y = r.raw("yaw")) {
        float deg = 0.0f;
        if (!rec::parseFloat(*y, deg)) r.fail("'yaw' is not a number: '" + std::string(*y) + "'");
        else if (deg != std::floor(deg) || std::fmod(deg, 90.0f) != 0.0f)
            r.fail("'yaw' must be a multiple of 90 degrees (blocks are axis-aligned): " + std::string(*y));
        else turns = static_cast<int>(deg / 90.0f);
    }
    std::string prefix;
    if (const auto pf = r.raw("prefix")) {
        prefix = std::string(*pf);
        if (!rec::validId(prefix)) r.fail("'prefix' must be an id (lower-case letters, digits, _): '" + prefix + "'");
    }
    const int doorOffset   = r.integer("door_offset", 0);
    const int sectorOffset = r.integer("sector_offset", 0);
    if (!r.failed() && (doorOffset < 0 || sectorOffset < 0)) r.fail("'door_offset' and 'sector_offset' must not be negative");
    r.finish();
    if (r.failed()) return false;

    const auto norm = normalizeIncludePath(pathText);
    if (!norm) return r.fail("'path' must be a plain relative path to a .lvl file under levels/: '" + pathText + "'");
    if (depth + 1 > kMaxIncludeDepth) return r.fail("includes are nested more than " + std::to_string(kMaxIncludeDepth) + " levels deep");
    if (++ctx.includeCount > kMaxIncludes) return r.fail("too many includes (the limit is " + std::to_string(kMaxIncludes) + " per level)");

    if (const auto at = std::find(ctx.stack.begin(), ctx.stack.end(), *norm); at != ctx.stack.end()) {
        std::string cycle;
        for (auto it = at; it != ctx.stack.end(); ++it) cycle += *it + " -> ";
        return r.fail("include cycle: " + cycle + *norm);
    }
    if (!ctx.reader) return r.fail("includes are not available here (no way to read '" + *norm + "')");
    const std::optional<std::string> text = ctx.reader(*norm);
    if (!text) return r.fail("cannot open included file '" + *norm + "'");

    ctx.stack.push_back(*norm);
    const Piece child = parsePiece(ctx, *text, *norm, false, depth + 1);
    ctx.stack.pop_back();

    Placement place;
    place.offset = offset;
    place.turns  = turns;
    mergePiece(piece, child, place, prefix, doorOffset, sectorOffset, *norm, lineNo, ctx);
    return true;
}

Piece parsePiece(ParseCtx& ctx, std::string_view text, const std::string& file, bool top, int depth) {
    Piece piece;
    LevelData& level = piece.data;
    const auto issue = [&](int line, std::string msg) { ctx.issues.push_back({line, std::move(msg), file}); };

    bool haveLevel = false;
    rec::forEachRecordLine(text, [&](int lineNo, std::string_view kind, std::string_view rest) {
        Record r;
        if (!r.parse(rest)) { issue(lineNo, r.error()); return; }

        // Records that describe the whole level are only read from the top-level file.
        if (!top && (kind == "level" || kind == "environment")) {
            issue(lineNo, std::string(kind) + ": only the top-level level file sets this; ignored in an included file");
            return;
        }

        bool ok = false;
        if (kind == "level") {
            if (haveLevel) { issue(lineNo, "duplicate 'level' record ignored"); return; }
            ok = parseLevelRecord(r, level);
            haveLevel = haveLevel || ok;
        } else if (kind == "environment") {
            if (level.hasEnvironment) { issue(lineNo, "duplicate 'environment' record ignored"); return; }
            ok = parseEnvironment(r, level);
            if (ok && (r.has("visibility") || r.has("far")) && !level.environment.farPlaneIsHidden())
                issue(lineNo, "environment: fog is only " + std::to_string(static_cast<int>(level.environment.fogAtFarPlane() * 100.0f)) +
                              "% opaque at the far plane (" + std::to_string(static_cast<int>(level.environment.farPlane)) +
                              " m), so the edge of the world will pop into view; make 'visibility' no larger than 'far'");
        } else if (kind == "spawn") {
            ok = parseSpawn(r, level, piece.haveSpawn, top);
        } else if (kind == "include") {
            ok = parseInclude(r, ctx, piece, depth, lineNo);
        } else if (kind == "block")   ok = parseBlock(r, level);
        else if (kind == "light")     ok = parseLight(r, level);
        else if (kind == "target")    ok = parseTarget(r, level);
        else if (kind == "enemy")     ok = parseEnemy(r, level);
        else if (kind == "pickup")    ok = parsePickup(r, level);
        else if (kind == "trigger") {
            ok = parseTrigger(r, level);
            if (ok) piece.locs.push_back({file, lineNo});
        } else {
            issue(lineNo, "unknown record type '" + std::string(kind) + "'");
            return;
        }
        if (!ok) issue(lineNo, std::string(kind) + ": " + r.error());
        for (const std::string& w : r.warnings()) issue(lineNo, std::string(kind) + ": " + w);
    });
    return piece;
}

IncludeReader diskReader(std::string assetDir) {
    return [assetDir = std::move(assetDir)](std::string_view path) -> std::optional<std::string> {
        std::ifstream f(assetDir + "/" + std::string(path), std::ios::binary | std::ios::ate);
        if (!f) return std::nullopt;
        const auto size = f.tellg();
        if (size < 0 || static_cast<std::size_t>(size) > kMaxIncludedBytes) return std::nullopt;
        std::string text(static_cast<std::size_t>(size), '\0');
        f.seekg(0);
        f.read(text.data(), size);
        return text;
    };
}

} // namespace

bool LevelData::hasSpawn(std::string_view name) const {
    return std::any_of(spawns.begin(), spawns.end(), [&](const NamedSpawn& s) { return s.name == name; });
}

SpawnPoint LevelData::spawnPoint(std::string_view name) const {
    for (const NamedSpawn& s : spawns)
        if (!name.empty() && s.name == name) return s.point;
    return {spawn, spawnYaw};
}

bool validLevelId(std::string_view id) { return rec::validId(id); }

std::string levelPath(std::string_view assetDir, std::string_view id) {
    return std::string(assetDir) + "/levels/" + std::string(id) + ".lvl";
}

LevelTransition completionTransition(const LevelData& level) {
    LevelTransition t;
    if (level.transition == TransitionType::Level && validLevelId(level.nextLevel)) {
        t.type  = TransitionType::Level;
        t.next  = level.nextLevel;
        t.spawn = level.nextSpawn;
    }
    if (validLevelId(level.briefing)) t.briefing = level.briefing;
    return t;
}

std::optional<std::string> normalizeIncludePath(std::string_view path) {
    constexpr std::string_view kAssets = "assets/";
    if (path.starts_with(kAssets)) path.remove_prefix(kAssets.size());
    if (path.empty() || path.size() > 128 || !path.starts_with("levels/") || !path.ends_with(".lvl")) return std::nullopt;
    if (path.size() <= std::string_view("levels/.lvl").size()) return std::nullopt;  // no file name
    if (!std::all_of(path.begin(), path.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/';
        }))
        return std::nullopt;
    // No empty, "." or ".." segments (so no way out of levels/).
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view seg = path.substr(start, end == std::string_view::npos ? end : end - start);
        if (seg.empty() || seg == "." || seg == "..") return std::nullopt;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return std::string(path);
}

LevelLoad parseLevel(std::string_view text, const IncludeReader& includes, std::string_view selfPath) {
    LevelLoad out;
    ParseCtx ctx{includes, out.issues, {}, 0};
    if (!selfPath.empty()) ctx.stack.emplace_back(selfPath);

    Piece piece = parsePiece(ctx, text, std::string{}, true, 0);
    out.level = std::move(piece.data);
    LevelData& level = out.level;
    const auto issue = [&](const TriggerLoc& at, std::string msg) { out.issues.push_back({at.line, std::move(msg), at.file}); };

    // Cross-checks, on the merged level. A trigger that can never work, or that fires the
    // instant the level starts, is dropped rather than left to misbehave in game.
    std::vector<TriggerVolume> kept;
    for (std::size_t i = 0; i < level.triggers.size(); ++i) {
        const TriggerVolume& t = level.triggers[i];
        const TriggerLoc& at = piece.locs[i];
        bool drop = false;
        if (t.action == TriggerAction::OpenDoor && !level.world.hasDoor(t.param)) {
            issue(at, "trigger: no block has door=" + std::to_string(t.param));
            drop = true;
        }
        if (!drop && t.condition == TriggerCondition::SectorCleared &&
            std::none_of(level.enemies.begin(), level.enemies.end(),
                         [&](const EnemySpawn& e) { return e.sector == t.sector; })) {
            issue(at, "trigger: no enemy belongs to sector " + std::to_string(t.sector) + " (it would fire immediately)");
            drop = true;
        }
        if (!drop) kept.push_back(t);
    }
    level.triggers = std::move(kept);

    if (!piece.haveSpawn) out.issues.push_back({0, "no 'spawn' record; using the default spawn point", {}});
    if (level.world.blocks().empty()) out.issues.push_back({0, "level has no 'block' records", {}});
    return out;
}

LevelLoad loadLevelFile(const std::string& path, std::string_view assetDir) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        LevelLoad out;
        out.opened = false;
        out.issues.push_back({0, "cannot open level file '" + path + "'", {}});
        return out;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    std::string self;  // this file's own path relative to the assets directory, for cycle checks
    if (const std::size_t at = path.rfind("/levels/"); at != std::string::npos)
        if (const auto norm = normalizeIncludePath(std::string_view(path).substr(at + 1))) self = *norm;
    return parseLevel(ss.str(), diskReader(std::string(assetDir)), self);
}

LevelLoad loadLevelFile(const std::string& path) {
    std::string assets = ".";
    if (const std::size_t at = path.rfind("/levels/"); at != std::string::npos) assets = path.substr(0, at);
    return loadLevelFile(path, assets);
}

void spawnTargets(const LevelData& level, Combat& combat) {
    for (const TargetSpawn& t : level.targets) {
        const std::size_t i = combat.addTarget(Target::dummy(t.pos, t.patrol));
        if (Target* target = combat.targetAt(i)) target->patrolAxis = t.axis;
    }
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
