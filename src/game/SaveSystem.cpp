#include "game/SaveSystem.hpp"

#include "core/Log.hpp"
#include "core/Record.hpp"
#include "game/Weapons.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace carnage {

namespace {

constexpr int   kMaxRounds = 100000;
constexpr float kMaxCoord  = 100000.0f;  // a spawn further out than this is a corrupt number, not a place

std::string joinWeapons(const std::vector<std::string>& v) {
    std::string out;
    for (const std::string& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
}

bool contains(const std::vector<std::string>& v, std::string_view s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

// Parses the one `save` record. Returns false with `out.error` set if the save is unusable.
bool parseRecord(std::string_view rest, int lineNo, SaveParse& out) {
    const auto fail = [&](std::string msg) {
        out.error = "line " + std::to_string(lineNo) + ": " + std::move(msg);
        return false;
    };
    rec::Record r;
    if (!r.parse(rest)) return fail(r.error());

    SaveData d;
    d.version = r.requiredInteger("version");
    if (r.failed()) return fail(r.error());
    if (d.version > kSaveVersion) return fail("saved by a newer version of the game (version " + std::to_string(d.version) + ")");
    if (d.version != kSaveVersion) return fail("unsupported save version " + std::to_string(d.version));

    if (const auto id = r.raw("level_id")) {
        d.levelId = std::string(*id);
        if (!rec::validId(d.levelId)) return fail("'level_id' is not a level id: '" + d.levelId + "'");
    } else {
        return fail("missing 'level_id'");
    }

    d.spawnPos = r.vec3("spawn_pos", d.spawnPos, true);
    d.spawnYawDeg = r.number("spawn_yaw", 0.0f);
    const float hp = r.requiredNumber("health");
    const float ar = r.requiredNumber("armor");
    if (r.failed()) return fail(r.error());
    if (std::abs(d.spawnPos.x) > kMaxCoord || std::abs(d.spawnPos.y) > kMaxCoord || std::abs(d.spawnPos.z) > kMaxCoord)
        return fail("'spawn_pos' is out of range");
    if (std::abs(d.spawnYawDeg) > 3600.0f) return fail("'spawn_yaw' is out of range");

    // A loaded game is always alive, and nobody has more than the maximum.
    d.health = std::clamp(hp, 1.0f, PlayerHealth::kMaxHealth);
    d.armor  = std::clamp(ar, 0.0f, PlayerHealth::kMaxArmor);
    if (d.health != hp) out.warnings.push_back("'health' " + std::to_string(hp) + " was out of range; using " + std::to_string(d.health));
    if (d.armor != ar)  out.warnings.push_back("'armor' " + std::to_string(ar) + " was out of range; using " + std::to_string(d.armor));

    // Weapons: unknown names are dropped; fists are always held.
    if (const auto w = r.raw("weapons")) {
        d.weapons.clear();
        for (const std::string_view tok : rec::split(*w, ',')) {
            const std::string id(rec::trim(tok));
            if (weaponIndex(id) < 0) out.warnings.push_back("unknown weapon '" + id + "' dropped");
            else if (!contains(d.weapons, id)) d.weapons.push_back(id);
        }
        if (!contains(d.weapons, "fists")) d.weapons.insert(d.weapons.begin(), "fists");
    }
    if (const auto a = r.raw("active_weapon")) {
        d.activeWeapon = std::string(*a);
        if (weaponIndex(d.activeWeapon) < 0 || !contains(d.weapons, d.activeWeapon)) {
            out.warnings.push_back("active weapon '" + d.activeWeapon + "' is not held; using fists");
            d.activeWeapon = "fists";
        }
    }
    if (const auto am = r.raw("ammo")) {
        for (const std::string_view tok : rec::split(*am, ',')) {
            const auto parts = rec::split(rec::trim(tok), ':');
            int rounds = 0;
            if (parts.size() != 2 || !rec::parseInt(parts[1], rounds)) return fail("bad ammo entry '" + std::string(tok) + "' (expected weapon:rounds)");
            const std::string id(parts[0]);
            if (weaponIndex(id) < 0) { out.warnings.push_back("ammo for unknown weapon '" + id + "' dropped"); continue; }
            if (rounds < 0 || rounds > kMaxRounds) {
                out.warnings.push_back("ammo for '" + id + "' was out of range");
                rounds = std::clamp(rounds, 0, kMaxRounds);
            }
            const auto it = std::find_if(d.ammo.begin(), d.ammo.end(), [&](const WeaponAmmo& e) { return e.id == id; });
            if (it != d.ammo.end()) it->rounds = rounds;
            else                    d.ammo.push_back({id, rounds});
        }
    }
    if (const auto o = r.raw("offhand")) {
        d.offhand = std::string(*o);
        const int idx = weaponIndex(d.offhand);
        if (idx <= 0) {   // unknown, or fists (which means an empty hand)
            if (idx < 0) out.warnings.push_back("off-hand weapon '" + d.offhand + "' is unknown; dropped");
            d.offhand.clear();
        }
    }
    d.offhandAmmo = r.integer("offhand_ammo", -1);
    if (r.failed()) return fail(r.error());
    d.offhandAmmo = std::clamp(d.offhandAmmo, -1, kMaxRounds);
    if (d.offhand.empty()) d.offhandAmmo = -1;

    if (const auto c = r.raw("completed_levels")) {
        for (const std::string_view tok : rec::split(*c, ',')) {
            const std::string id(rec::trim(tok));
            if (!rec::validId(id)) out.warnings.push_back("completed level '" + id + "' is not an id; dropped");
            else if (!contains(d.completedLevels, id)) d.completedLevels.push_back(id);
        }
    }
    // Unknown keys are ignored on purpose: a newer build may add fields an older one can skip.
    out.data = std::move(d);
    return true;
}

} // namespace

int weaponIndex(std::string_view id) {
    for (std::size_t i = 0; i < kWeaponIds.size(); ++i)
        if (kWeaponIds[i] == id) return static_cast<int>(i);
    return -1;
}

bool Progress::complete(std::string_view id) {
    if (!rec::validId(id) || isComplete(id)) return false;
    done_.emplace_back(id);
    return true;
}

bool Progress::isComplete(std::string_view id) const { return contains(done_, id); }

std::string serializeSave(const SaveData& d) {
    std::string line = std::format("save version={} level_id={} spawn_pos={},{},{} spawn_yaw={} health={} armor={} weapons={} active_weapon={}",
                                   d.version, d.levelId, d.spawnPos.x, d.spawnPos.y, d.spawnPos.z, d.spawnYawDeg, d.health,
                                   d.armor, joinWeapons(d.weapons), d.activeWeapon);
    if (!d.ammo.empty()) {
        std::string a;
        for (const WeaponAmmo& e : d.ammo) a += std::format("{}{}:{}", a.empty() ? "" : ",", e.id, e.rounds);
        line += " ammo=" + a;
    }
    if (!d.offhand.empty()) {
        line += " offhand=" + d.offhand;
        if (d.offhandAmmo >= 0) line += std::format(" offhand_ammo={}", d.offhandAmmo);
    }
    if (!d.completedLevels.empty()) line += " completed_levels=" + joinWeapons(d.completedLevels);
    return "# Through the Eyes of Carnage - save file (checkpoint and mission progress). Safe to edit by hand.\n" + line + "\n";
}

SaveParse parseSave(std::string_view text) {
    SaveParse out;
    if (text.size() > kMaxSaveBytes) { out.error = "file is too large to be a save"; return out; }
    if (text.find('\0') != std::string_view::npos) { out.error = "file contains binary data"; return out; }

    bool seen = false;
    rec::forEachRecordLine(text, [&](int lineNo, std::string_view kind, std::string_view rest) {
        if (!out.error.empty()) return;
        if (kind != "save") { out.error = "line " + std::to_string(lineNo) + ": unexpected record '" + std::string(kind) + "'"; return; }
        if (seen) { out.error = "line " + std::to_string(lineNo) + ": more than one save record"; return; }
        seen = true;
        parseRecord(rest, lineNo, out);
    });
    if (out.error.empty() && !seen) out.error = rec::trim(text).empty() ? "file is empty" : "no save record";
    if (!out.error.empty()) out.data.reset();
    return out;
}

bool validSlot(int slot) { return slot >= 1 && slot <= kSaveSlots; }

std::string slotPath(int slot, std::string_view dir) {
    if (!validSlot(slot)) return {};
    return std::string(dir) + "/slot" + std::to_string(slot) + ".sav";
}

bool saveExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool writeSave(const std::string& path, const SaveData& d) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path target(path);
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        if (ec) { log::warn("cannot create '{}': {}", target.parent_path().string(), ec.message()); return false; }
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const std::string text = serializeSave(d);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.flush();
        if (!f) { log::warn("cannot write save '{}'", tmp); fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, target, ec);
    if (ec) { log::warn("cannot move save into place '{}': {}", path, ec.message()); fs::remove(tmp, ec); return false; }
    return true;
}

std::optional<SaveData> readSave(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { log::warn("save '{}' not found", path); return std::nullopt; }
    std::string text(kMaxSaveBytes + 1, '\0');
    f.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(f.gcount()));

    SaveParse p = parseSave(text);
    if (!p.data) { log::warn("save '{}' is unusable: {}", path, p.error); return std::nullopt; }
    for (const std::string& w : p.warnings) log::warn("save '{}': {}", path, w);
    return std::move(p.data);
}

SaveData captureSave(std::string levelId, const SpawnPoint& checkpoint, const PlayerHealth& health, const WeaponSet& weapons,
                     std::size_t current, const Weapon* offhand, const Progress& progress) {
    SaveData d;
    d.levelId     = std::move(levelId);
    d.spawnPos    = checkpoint.pos;
    d.spawnYawDeg = glm::degrees(checkpoint.yaw);
    d.health      = std::clamp(health.health, 1.0f, PlayerHealth::kMaxHealth);
    d.armor       = std::clamp(health.armor, 0.0f, PlayerHealth::kMaxArmor);
    d.weapons.assign(kWeaponIds.begin(), kWeaponIds.end());
    d.activeWeapon = std::string(kWeaponIds[std::min(current, kWeaponCount - 1)]);
    for (std::size_t i = 0; i < kWeaponCount; ++i)
        if (weapons[i]) d.ammo.push_back({std::string(kWeaponIds[i]), weapons[i]->ammo()});
    if (offhand) {
        for (std::size_t i = 1; i < kWeaponCount; ++i)
            if (weapons[i] && std::string_view(weapons[i]->stats().name) == offhand->stats().name) {
                d.offhand     = std::string(kWeaponIds[i]);
                d.offhandAmmo = offhand->ammo();
            }
    }
    d.completedLevels.assign(progress.ids().begin(), progress.ids().end());
    return d;
}

void applyLoadout(const SaveData& d, PlayerHealth& health, WeaponSet& weapons, std::size_t& current,
                  std::unique_ptr<Weapon>& offhand) {
    health.reset();
    health.health = std::clamp(d.health, 1.0f, PlayerHealth::kMaxHealth);
    health.armor  = std::clamp(d.armor, 0.0f, PlayerHealth::kMaxArmor);

    for (const WeaponAmmo& a : d.ammo) {
        const int i = weaponIndex(a.id);
        if (i >= 0 && weapons[static_cast<std::size_t>(i)]) weapons[static_cast<std::size_t>(i)]->setAmmo(a.rounds);
    }
    const int active = weaponIndex(d.activeWeapon);
    current = active >= 0 ? static_cast<std::size_t>(active) : 0;
    weapons[current]->equip();

    const int left = weaponIndex(d.offhand);
    if (left > 0) {
        offhand = makeWeapon(static_cast<std::size_t>(left));
        offhand->equip();
        if (d.offhandAmmo >= 0) offhand->setAmmo(d.offhandAmmo);
    } else {
        offhand.reset();
    }
}

std::string describeSave(const SaveData& d) {
    std::string name = d.levelId;
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return name;
}

} // namespace carnage
