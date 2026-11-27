#include "game/LevelGraph.hpp"

#include "game/Briefing.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace carnage {

std::vector<LevelLink> levelLinks(const LevelData& level) {
    std::vector<LevelLink> out;
    const LevelTransition t = completionTransition(level);
    if (t.type == TransitionType::Level) out.push_back({LevelLink::Kind::Next, t.next, t.spawn});
    for (const TriggerVolume& v : level.triggers)
        if (v.action == TriggerAction::Portal) out.push_back({LevelLink::Kind::Portal, v.target, v.targetSpawn});
    return out;
}

namespace {

struct Node {
    bool                   loaded = false;
    std::set<std::string>  spawnNames;
    std::vector<LevelLink> links;
};

constexpr std::size_t kMaxLevels = 256;  // a runaway chain of generated ids must not loop forever

} // namespace

LevelGraphReport checkLevelGraph(std::string_view assetDir, std::string_view start, bool requireReturn) {
    LevelGraphReport rep;
    const auto problem = [&](std::string msg) { rep.problems.push_back(std::move(msg)); };

    if (!validLevelId(start)) {
        problem("start level '" + std::string(start) + "' is not a valid level id");
        return rep;
    }

    std::map<std::string, Node> nodes;
    std::vector<std::string>    queue{std::string(start)};
    std::map<std::string, std::string> referencedFrom;  // for "missing file" messages

    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::string id = queue[head];
        Node& node = nodes[id];
        rep.levels.push_back(id);

        const std::string path = levelPath(assetDir, id);
        LevelLoad load = loadLevelFile(path);
        if (!load.opened) {
            const auto from = referencedFrom.find(id);
            problem("level '" + id + "' cannot be opened" +
                    (from != referencedFrom.end() ? " (linked from '" + from->second + "')" : std::string{}));
            continue;
        }
        node.loaded = true;
        for (const LevelIssue& i : load.issues)
            problem("level '" + id + "', line " + std::to_string(i.line) + ": " + i.message);
        if (load.level.name != id)
            problem("level file '" + id + ".lvl' declares name '" + load.level.name + "' (they must match)");

        for (const NamedSpawn& s : load.level.spawns) node.spawnNames.insert(s.name);
        node.links = levelLinks(load.level);

        if (!load.level.briefing.empty()) {
            const std::string bpath = briefingPath(assetDir, load.level.briefing);
            const BriefingLoad b = loadBriefingFile(bpath);
            if (!b.opened) problem("level '" + id + "': briefing '" + load.level.briefing + "' cannot be opened");
            else if (b.data.beats.empty()) problem("level '" + id + "': briefing '" + load.level.briefing + "' has no beats");
            for (const rec::Issue& i : checkBriefingImages(b.data, assetDir))
                problem("briefing '" + load.level.briefing + "', line " + std::to_string(i.line) + ": " + i.message);
        }

        for (const LevelLink& link : node.links) {
            if (!validLevelId(link.to)) { problem("level '" + id + "' links to invalid level id '" + link.to + "'"); continue; }
            if (std::find(queue.begin(), queue.end(), link.to) == queue.end()) {
                if (queue.size() >= kMaxLevels) { problem("more than " + std::to_string(kMaxLevels) + " levels reachable; stopping"); continue; }
                referencedFrom.emplace(link.to, id);
                queue.push_back(link.to);
            }
        }
    }

    // Spawn names can only be checked once every target is loaded.
    for (const std::string& id : rep.levels) {
        for (const LevelLink& link : nodes[id].links) {
            const auto target = nodes.find(link.to);
            if (target == nodes.end() || !target->second.loaded || link.spawn.empty()) continue;
            if (!target->second.spawnNames.count(link.spawn))
                problem("level '" + id + "' sends the player to spawn '" + link.spawn + "' in '" + link.to +
                        "', which has no such spawn");
        }
    }

    if (requireReturn) {
        for (const std::string& id : rep.levels) {
            if (!nodes[id].loaded) continue;
            std::set<std::string> seen{id};
            std::vector<std::string> walk{id};
            for (std::size_t i = 0; i < walk.size(); ++i)
                for (const LevelLink& l : nodes[walk[i]].links)
                    if (nodes.count(l.to) && seen.insert(l.to).second) walk.push_back(l.to);
            if (!seen.count(std::string(start)))
                problem("level '" + id + "' has no way back to '" + std::string(start) + "'");
        }
    }
    return rep;
}

} // namespace carnage
