#pragma once
#include "game/Level.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace carnage {

// Levels are connected by completion transitions (`next_level`) and by portal
// triggers. This module reads those links so the whole network can be checked,
// without loading anything into the game: every target must exist, every arrival
// spawn must exist in its target, every briefing must load, and (optionally)
// every level must lead back to where the player started.

struct LevelLink {
    enum class Kind { Next, Portal };
    Kind        kind = Kind::Next;
    std::string to;     // target level id
    std::string spawn;  // named spawn in the target ("" = its default)
};

// The links leaving `level`: its next_level (if the transition goes to a level)
// followed by each portal trigger, in file order.
std::vector<LevelLink> levelLinks(const LevelData& level);

struct LevelGraphReport {
    std::vector<std::string> levels;    // every level reachable from the start, start first
    std::vector<std::string> problems;  // each is a complete, human-readable sentence

    bool ok() const { return problems.empty(); }
};

// Walks the network from `start` (a level id under <assetDir>/levels/).
// With `requireReturn`, a level that cannot lead back to `start` is a problem
// (the player would be stranded).
LevelGraphReport checkLevelGraph(std::string_view assetDir, std::string_view start,
                                 bool requireReturn = true);

} // namespace carnage
