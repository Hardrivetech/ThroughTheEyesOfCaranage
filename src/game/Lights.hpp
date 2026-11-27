#pragma once
#include "game/World.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace carnage {

// Which room lights to submit this frame. The renderer can only shade a handful at once
// (kMaxLights, with headroom needed for muzzle flashes and explosions), and a big level
// has far more lights than that. A light farther than `cullDistance` from the eye is
// skipped before anything else, and of the rest the `maxLights` nearest are kept,
// nearest first (ties go to the lower index). One pass, no sorting of the full list.
struct LightSelection {
    std::size_t considered = 0;  // lights looked at
    std::size_t culled     = 0;  // skipped for being beyond cullDistance
    std::size_t picked     = 0;  // lights written to `out`
};

inline constexpr std::size_t kMaxPickedLights = 16;

inline LightSelection selectLights(std::span<const WorldLight> lights, const glm::vec3& eye,
                                   std::size_t maxLights, float cullDistance,
                                   std::vector<std::size_t>& out) {
    out.clear();
    LightSelection r;
    r.considered = lights.size();
    maxLights = std::min(maxLights, kMaxPickedLights);
    if (maxLights == 0) { r.culled = lights.size(); return r; }

    std::array<float, kMaxPickedLights> dist{};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const float d = glm::distance(lights[i].pos, eye);
        if (!(d <= cullDistance)) { ++r.culled; continue; }  // beyond the cull distance (a NaN position counts as beyond)
        if (out.size() == maxLights && !(d < dist[maxLights - 1])) continue;  // not near enough to make the cut

        std::size_t at = out.size() < maxLights ? out.size() : maxLights - 1;  // slot to fill or replace
        if (out.size() < maxLights) out.push_back(i); else out[at] = i;
        dist[at] = d;
        while (at > 0 && d < dist[at - 1]) {  // strict: equal distances keep index order
            std::swap(dist[at], dist[at - 1]);
            std::swap(out[at], out[at - 1]);
            --at;
        }
    }
    r.picked = out.size();
    return r;
}

} // namespace carnage
