#pragma once
#include "game/Prompts.hpp"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace carnage {

// Lightweight objective triggers. Pure data + logic (no world, audio or
// rendering), so enter / sector-cleared behaviour is unit-testable.
enum class TriggerCondition { PlayerEnters, SectorCleared };
enum class TriggerAction    { OpenDoor, Checkpoint, Complete, Prompt };

struct TriggerVolume {
    glm::vec3        min{0.0f};
    glm::vec3        max{0.0f};
    TriggerCondition condition = TriggerCondition::PlayerEnters;
    TriggerAction    action    = TriggerAction::OpenDoor;
    int              sector    = -1;  // SectorCleared: which enemy sector
    int              param     = 0;   // OpenDoor: door id
    glm::vec3        point{0.0f};     // Checkpoint: respawn position
    std::string      text;            // Prompt: text to show ({key} placeholders allowed)
    float            time      = 5.0f;  // Prompt: seconds fully visible
    PromptClear      clearOn   = PromptClear::Timer;  // Prompt: action that dismisses it early
    bool             fired     = false;

    bool contains(glm::vec3 p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
};

struct TriggerEvent {
    TriggerAction action = TriggerAction::OpenDoor;
    int           param  = 0;
    glm::vec3     point{0.0f};
    std::string   text;
    float         time    = 5.0f;
    PromptClear   clearOn = PromptClear::Timer;
};

class TriggerSystem {
public:
    void add(const TriggerVolume& t) { volumes_.push_back(t); }
    void clear() { volumes_.clear(); }
    void reset() { for (TriggerVolume& t : volumes_) t.fired = false; }

    // `aliveInSector(sector)` must return the number of living enemies still
    // gating that sector. Every trigger fires at most once per reset; the
    // returned events stay valid until the next call.
    template <class AliveFn>
    const std::vector<TriggerEvent>& update(glm::vec3 playerFeet, AliveFn&& aliveInSector) {
        events_.clear();
        for (TriggerVolume& t : volumes_) {
            if (t.fired) continue;
            const bool met = t.condition == TriggerCondition::PlayerEnters
                                 ? t.contains(playerFeet)
                                 : aliveInSector(t.sector) <= 0;
            if (!met) continue;
            t.fired = true;
            events_.push_back({t.action, t.param, t.point, t.text, t.time, t.clearOn});
        }
        return events_;
    }

    // First unfinished sector objective, or -1 when none remain.
    int activeSector() const {
        for (const TriggerVolume& t : volumes_)
            if (!t.fired && t.condition == TriggerCondition::SectorCleared) return t.sector;
        return -1;
    }

    const std::vector<TriggerVolume>& volumes() const { return volumes_; }

private:
    std::vector<TriggerVolume> volumes_;
    std::vector<TriggerEvent>  events_;
};

} // namespace carnage
