#pragma once
#include "core/Environment.hpp"
#include "core/Record.hpp"
#include "game/Placement.hpp"
#include "game/Enemy.hpp"
#include "game/Interact.hpp"
#include "game/Triggers.hpp"
#include "game/World.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace carnage {

// A level is a plain-text file of one record per line:
//
//     record key=value key=value ...        # comment
//
// Vectors are comma-separated (pos=1,0,-4). Blank lines and '#' comments are
// ignored. See assets/levels/level01_escape.lvl for every record type:
//
//   level   name=<id> [next_level=<id> [next_spawn=<name>]] [transition_type=menu|level] [briefing=<id>]
//   spawn   pos=x,y,z [yaw=<degrees>] [name=<id>]
//   environment [visibility=<m>] [far=<m>] [shadow=<m>]
//   include path=levels/pois/<file>.lvl [offset=x,y,z] [yaw=0|90|180|270] [prefix=<id>]
//           [door_offset=<n>] [sector_offset=<n>]
//   block   min=x,z max=x,z (top=h | h0=h h1=h axis=x|z)
//           [base=h] [color=r,g,b] [material=<name>] [visible=0|1] [door=<id>]
//   light   pos=x,y,z [color=r,g,b] [radius=m]
//   target  pos=x,y,z [patrol=m]
//   enemy   pos=x,y,z type=melee|ranged [sector=<id>]
//   pickup  kind=pipe|bat|barrel pos=x,y,z [yaw=rad]
//   trigger when=enter min=x,y,z max=x,y,z | when=sector_cleared sector=<id>
//           do=open_door door=<id> | do=checkpoint point=x,y,z | do=complete
//           | do=prompt text="..." [time=sec] [clear_on=jump|slide|vault|wall_run|fire|grab|throw]
//           | do=portal level=<id> [spawn=<name>]
//
// `environment` sets how far you can see: fog visibility (metres at which fog is ~95% opaque),
// the camera far plane, and how wide the sun shadow box is. Defaults match the old fixed
// values, so levels without one look as before. For an open map set `far` at least as big as
// `visibility`, so the edge of the world is hidden in fog instead of popping into view.
//
// `include` pastes another level file (a point of interest) into this one: its blocks, lights,
// enemies, pickups, targets, triggers and named spawns, turned by `yaw` (multiples of 90 only,
// since blocks are axis-aligned) and moved by `offset`. Door and sector ids are global, so give
// each copy its own `door_offset` / `sector_offset`, and a `prefix` so its named spawns
// (prefix_name) do not collide. Included files may include others (nesting is limited), but a
// cycle is an error. `level`, `environment` and the unnamed `spawn` are only read from the
// top-level file.
//
// A `spawn` without a name is the level's default start; named spawns are the places
// other levels send you to (`next_spawn=`, a portal's `spawn=`). Yaw is in degrees with
// the engine's convention: 0 faces -Z, 90 faces -X, -90 faces +X, 180 faces +Z.
// A portal loads another level when the player walks into its volume.
//
// Values containing spaces are written in double quotes: text="JUMP: {jump}".
// Prompt text may use {action} placeholders (forward back left right sprint
// crouch jump grab reload_right reload_left respawn left_mod weapon1..weapon5
// lmb rmb); they show the player's current key.
//
// When the level is completed, `transition_type=level` loads `next_level`
// (a level id, i.e. assets/levels/<id>.lvl); the default, menu, returns to the
// main menu. If `briefing=<id>` is set, that briefing (assets/briefings/<id>.brf)
// plays first and then the game continues to the destination above.
//
// Malformed lines are skipped and reported as issues; they never abort a load.

enum class TransitionType { Menu, Level };

struct TargetSpawn {
    glm::vec3 pos{0.0f};
    float     patrol = 0.0f;
    glm::vec3 axis{1.0f, 0.0f, 0.0f};  // the direction a patrolling target paces along
};

struct EnemySpawn {
    glm::vec3 pos{0.0f};
    EnemyType type   = EnemyType::Melee;
    int       sector = -1;
};

struct PickupSpawn {
    PickupKind kind = PickupKind::Pipe;
    glm::vec3  pos{0.0f};
    float      yaw = 0.0f;
};

// Where the player appears. `yaw` is in radians (engine convention, see spawn above).
struct SpawnPoint {
    glm::vec3 pos{0.0f};
    float     yaw = 0.0f;
};

// Where the player returns to after dying. A level starts it at its arrival spawn;
// every checkpoint the player reaches replaces it. Arriving in another level (a portal,
// a completion transition) resets it, so a checkpoint never leaks between levels.
class CheckpointTracker {
public:
    void arrive(const SpawnPoint& p) { point_ = p; }
    void reached(const glm::vec3& pos, float yaw) { point_ = {pos, yaw}; }
    const SpawnPoint& current() const { return point_; }

private:
    SpawnPoint point_;
};

struct NamedSpawn {
    std::string name;
    SpawnPoint  point;
};

struct LevelData {
    std::string name = "unnamed";
    std::string nextLevel;                          // level id loaded on completion
    std::string briefing;                           // briefing id played before moving on
    std::string nextSpawn;                          // named spawn to arrive at in nextLevel ("" = default)
    TransitionType transition = TransitionType::Menu;
    Environment environment;                        // fog, far plane, shadow reach
    bool        hasEnvironment = false;             // set by an `environment` record
    glm::vec3   spawn{0.0f, 0.0f, 10.0f};           // the default spawn...
    float       spawnYaw = 0.0f;                    // ...and which way it faces (radians)
    std::vector<NamedSpawn> spawns;                 // named spawns
    World       world;
    std::vector<TargetSpawn>   targets;
    std::vector<EnemySpawn>    enemies;
    std::vector<PickupSpawn>   pickups;
    std::vector<TriggerVolume> triggers;
    std::vector<std::string>   includedFiles;       // every file pulled in by `include`, normalised, in order

    bool hasSpawn(std::string_view name) const;
    // The named spawn, or the default one if `name` is empty or unknown.
    SpawnPoint spawnPoint(std::string_view name = {}) const;
};

using LevelIssue = rec::Issue;  // line is 1-based; 0 = not tied to a line

struct LevelLoad {
    LevelData               level;
    std::vector<LevelIssue> issues;
    bool                    opened = true;  // false: the file could not be read

    // Loaded and without a single skipped or suspicious line.
    bool clean() const { return opened && issues.empty(); }
};

// Supplies the text of an included file, given its normalised path ("levels/pois/docks.lvl"),
// or nothing if it cannot be read. Lets parsing work without a disk (tests, tools).
using IncludeReader = std::function<std::optional<std::string>(std::string_view path)>;

// `selfPath` (optional) is the normalised path of the text being parsed, so a file that
// ends up including itself is caught on the first lap.
LevelLoad parseLevel(std::string_view text, const IncludeReader& includes = {}, std::string_view selfPath = {});

// Loads a level file. Includes are read relative to the assets directory, which is found by
// looking for "/levels/" in `path` (or given explicitly).
LevelLoad loadLevelFile(const std::string& path);
LevelLoad loadLevelFile(const std::string& path, std::string_view assetDir);

// Include paths: relative to the assets directory, under levels/, plain characters only,
// ending in .lvl. A leading "assets/" is accepted and dropped. Returns the normalised path
// ("levels/pois/docks.lvl"), or nothing if it is not allowed.
std::optional<std::string> normalizeIncludePath(std::string_view path);

// Level ids are lower-case letters, digits and '_' only (no paths).
bool        validLevelId(std::string_view id);
std::string levelPath(std::string_view assetDir, std::string_view id);

// What the game should do when this level is completed.
struct LevelTransition {
    TransitionType type = TransitionType::Menu;
    std::string    next;      // level id, when type == Level
    std::string    briefing;  // briefing id to play first; empty = none
    std::string    spawn;     // named spawn to arrive at in `next` ("" = its default)
};
LevelTransition completionTransition(const LevelData& level);

// Fill the live game systems from a level. Call after clearing them.
void spawnTargets(const LevelData& level, Combat& combat);
void spawnEnemies(const LevelData& level, Enemies& enemies, Combat& combat);
void spawnPickups(const LevelData& level, Interact& interact);
void installTriggers(const LevelData& level, TriggerSystem& triggers);

} // namespace carnage
