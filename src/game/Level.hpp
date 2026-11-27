#pragma once
#include "game/Enemy.hpp"
#include "game/Interact.hpp"
#include "game/Triggers.hpp"
#include "game/World.hpp"

#include <glm/glm.hpp>

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
//   level   name=<id>
//   spawn   pos=x,y,z
//   block   min=x,z max=x,z (top=h | h0=h h1=h axis=x|z)
//           [base=h] [color=r,g,b] [material=<name>] [visible=0|1] [door=<id>]
//   light   pos=x,y,z [color=r,g,b] [radius=m]
//   target  pos=x,y,z [patrol=m]
//   enemy   pos=x,y,z type=melee|ranged [sector=<id>]
//   pickup  kind=pipe|bat|barrel pos=x,y,z [yaw=rad]
//   trigger when=enter min=x,y,z max=x,y,z | when=sector_cleared sector=<id>
//           do=open_door door=<id> | do=checkpoint point=x,y,z | do=complete
//
// Malformed lines are skipped and reported as issues; they never abort a load.

struct TargetSpawn {
    glm::vec3 pos{0.0f};
    float     patrol = 0.0f;
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

struct LevelData {
    std::string name = "unnamed";
    glm::vec3   spawn{0.0f, 0.0f, 10.0f};
    World       world;
    std::vector<TargetSpawn>   targets;
    std::vector<EnemySpawn>    enemies;
    std::vector<PickupSpawn>   pickups;
    std::vector<TriggerVolume> triggers;
};

struct LevelIssue {
    int         line = 0;  // 1-based; 0 = not tied to a line
    std::string message;
};

struct LevelLoad {
    LevelData               level;
    std::vector<LevelIssue> issues;
    bool                    opened = true;  // false: the file could not be read

    // Loaded and without a single skipped or suspicious line.
    bool clean() const { return opened && issues.empty(); }
};

LevelLoad parseLevel(std::string_view text);
LevelLoad loadLevelFile(const std::string& path);

// Fill the live game systems from a level. Call after clearing them.
void spawnTargets(const LevelData& level, Combat& combat);
void spawnEnemies(const LevelData& level, Enemies& enemies, Combat& combat);
void spawnPickups(const LevelData& level, Interact& interact);
void installTriggers(const LevelData& level, TriggerSystem& triggers);

} // namespace carnage
