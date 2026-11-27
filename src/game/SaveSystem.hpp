#pragma once
#include "game/Level.hpp"
#include "game/PlayerHealth.hpp"
#include "game/Weapon.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace carnage {

// Checkpoint and mission-progress persistence.
//
// A save is the player's loadout (health, armor, weapons, ammo), the last checkpoint they
// reached, and which missions they have completed. It is NOT a snapshot of the world: enemy
// positions, sleeping/awake state, projectiles, decals, doors and physics are not stored, and
// loading rebuilds the level fresh and puts the player at the checkpoint. That keeps saves tiny,
// deterministic and impossible to corrupt into an unplayable state.
//
// The file is one `save` record in the same `key=value` format as the level files
// (src/core/Record.hpp), so it is human-readable and hand-editable:
//
//   save version=1 level_id=kodiak_hub spawn_pos=0,0.2,40 spawn_yaw=0 health=80 armor=25
//        weapons=fists,pistol,smg active_weapon=pistol ammo=pistol:7,smg:30
//        offhand=smg offhand_ammo=12 completed_levels=level01_escape
//   (on one line)
inline constexpr int              kSaveVersion = 1;
inline constexpr std::size_t      kMaxSaveBytes = 64 * 1024;  // a save is a few hundred bytes; anything bigger is not one
inline constexpr int              kSaveSlots = 9;             // slot numbers 1..9
inline constexpr std::string_view kSaveDir = "saves";

inline constexpr std::string_view kFirstMissionId = "level01_escape";
inline constexpr std::string_view kHubLevelId     = "kodiak_hub";

// Weapon names in slot order (the same order as makeWeapon(index)).
inline constexpr std::size_t kWeaponCount = 5;
inline constexpr std::array<std::string_view, kWeaponCount> kWeaponIds{"fists", "pistol", "smg", "shotgun", "rocket"};
// Index of a weapon id (case-sensitive), or -1.
int weaponIndex(std::string_view id);

using WeaponSet = std::array<std::unique_ptr<Weapon>, kWeaponCount>;

struct WeaponAmmo {
    std::string id;
    int         rounds = 0;
    bool operator==(const WeaponAmmo&) const = default;
};

struct SaveData {
    int         version = kSaveVersion;
    std::string levelId;                  // the level to load
    glm::vec3   spawnPos{0.0f};           // the last checkpoint (or the level arrival point)
    float       spawnYawDeg = 0.0f;       // which way it faces, degrees (0 = north, -Z; 90 = -X)
    float       health = PlayerHealth::kMaxHealth;
    float       armor  = PlayerHealth::kStartArmor;
    std::vector<std::string> weapons{kWeaponIds.begin(), kWeaponIds.end()};  // held / unlocked
    std::string activeWeapon = "fists";
    std::vector<WeaponAmmo>  ammo;        // rounds in the magazine, per weapon (absent = full)
    std::string offhand;                  // dual-wield left hand ("" = empty)
    int         offhandAmmo = -1;         // -1 = full
    std::vector<std::string> completedLevels;  // missions finished, in the order they were

    bool operator==(const SaveData&) const = default;
};

// The set of finished missions. Survives New Game, so the hub stays unlocked.
class Progress {
public:
    // Marks `id` complete. Returns true if it was not already.
    bool complete(std::string_view id);
    bool isComplete(std::string_view id) const;
    std::span<const std::string> ids() const { return done_; }
    void set(std::vector<std::string> ids) { done_ = std::move(ids); }
    // The hub (and level select) unlock when the first mission is done.
    bool hubUnlocked() const { return isComplete(kFirstMissionId); }

private:
    std::vector<std::string> done_;
};

// ---- text format ---------------------------------------------------------------------------

// The save as file text. Deterministic: the same data always gives the same bytes, and
// parseSave(serializeSave(d)) == d for any valid d.
std::string serializeSave(const SaveData& d);

struct SaveParse {
    std::optional<SaveData>  data;       // empty when the text is not a usable save
    std::string              error;      // why, when `data` is empty
    std::vector<std::string> warnings;   // problems that were repaired (unknown weapon dropped, value clamped)
};

// Parses and validates save text without touching the disk. Never throws or crashes: empty,
// truncated, oversized, binary or hand-mangled input gives an empty `data` and an `error`.
// Numbers are clamped to sane ranges and unknown weapons are dropped (both reported as warnings);
// a missing or malformed required field, a newer `version`, or a bad level id rejects the save.
SaveParse parseSave(std::string_view text);

// ---- files -----------------------------------------------------------------------------------

// "saves/slot<N>.sav", or an empty string for a slot outside 1..9.
std::string slotPath(int slot, std::string_view dir = kSaveDir);
bool        validSlot(int slot);

bool saveExists(const std::string& path);

// Writes `d` to `path`, creating the directory if needed. The file is written to a temporary
// name and renamed into place, so a crash mid-write never leaves a half-written save. Logs a
// warning and returns false on failure.
bool writeSave(const std::string& path, const SaveData& d);

// Reads and parses `path`. A missing, empty or corrupt file logs a [WRN] and gives nullopt.
std::optional<SaveData> readSave(const std::string& path);

// ---- the game's state <-> a save ---------------------------------------------------------------

// Everything a save needs from the running game, taken at `checkpoint` in level `levelId`.
SaveData captureSave(std::string levelId, const SpawnPoint& checkpoint, const PlayerHealth& health,
                     const WeaponSet& weapons, std::size_t current, const Weapon* offhand, const Progress& progress);

// Puts a save's loadout back: vitals (alive, clamped), the active weapon and each magazine, and
// the off-hand weapon. Weapons missing from the save keep whatever they were given.
void applyLoadout(const SaveData& d, PlayerHealth& health, WeaponSet& weapons, std::size_t& current,
                  std::unique_ptr<Weapon>& offhand);

// The checkpoint a save restores.
inline SpawnPoint saveSpawn(const SaveData& d) { return {d.spawnPos, glm::radians(d.spawnYawDeg)}; }

// One line for a menu: "KODIAK_HUB" (level ids are upper-cased).
std::string describeSave(const SaveData& d);

} // namespace carnage
