#include "game/LevelAudit.hpp"

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

// Drawn at all: visible blocks, plus doors (invisible in the static mesh, drawn as boxes).
bool drawn(const Block& b) { return b.visible || b.door >= 0; }

} // namespace

std::vector<CoplanarTops> findCoplanarTops(std::span<const Block> blocks, float eps, float minOverlap) {
    std::vector<CoplanarTops> out;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (!drawn(blocks[i])) continue;
        for (std::size_t j = i + 1; j < blocks.size(); ++j) {
            if (!drawn(blocks[j])) continue;
            const Block &a = blocks[i], &b = blocks[j];
            const glm::vec2 lo = glm::max(a.min, b.min), hi = glm::min(a.max, b.max);
            if (hi.x - lo.x < minOverlap || hi.y - lo.y < minOverlap) continue;

            // The tops are in one plane over the overlap when they agree at its corners and centre
            // (two planes that agree at five points of a rectangle are the same plane).
            const glm::vec2 pts[5] = {lo, {hi.x, lo.y}, {lo.x, hi.y}, hi, (lo + hi) * 0.5f};
            bool same = true;
            for (const glm::vec2& p : pts) same = same && std::abs(a.surface(p) - b.surface(p)) < eps;
            if (!same) continue;

            // Not a problem if the shared top is buried: a third block is solid across it and rises
            // above it (the wall tops under a roof slab, say), so nobody ever sees the two flicker.
            const float h = a.surface(pts[4]);
            bool buried = false;
            for (std::size_t k = 0; k < blocks.size() && !buried; ++k) {
                if (k == i || k == j || !drawn(blocks[k])) continue;
                bool coversAll = true;
                for (const glm::vec2& p : pts)
                    coversAll = coversAll && blocks[k].contains(p) && blocks[k].base <= h + eps && blocks[k].surface(p) > h + eps;
                buried = coversAll;
            }
            if (!buried) out.push_back({i, j, lo, hi, h});
        }
    }
    return out;
}

std::vector<SnowyInterior> findSnowyInteriors(std::span<const Block> blocks, float maxCeiling, float minRoofSpan) {
    std::vector<SnowyInterior> out;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const Block& floor = blocks[i];
        if (!floor.visible || floor.door >= 0 || !accumulatesSnow(floor.material)) continue;

        const auto cells = [](float len) { return std::clamp(static_cast<int>(std::ceil(len)), 1, 24); };
        const int nx = cells(floor.max.x - floor.min.x), nz = cells(floor.max.y - floor.min.y);
        for (int iz = 0; iz < nz; ++iz) {
            for (int ix = 0; ix < nx; ++ix) {
                const glm::vec2 p(floor.min.x + (floor.max.x - floor.min.x) * (static_cast<float>(ix) + 0.5f) / static_cast<float>(nx),
                                  floor.min.y + (floor.max.y - floor.min.y) * (static_cast<float>(iz) + 0.5f) / static_cast<float>(nz));
                const float s = floor.surface(p);

                bool  covered = false;   // something taller is drawn here, so this top is not a floor
                float roof    = 0.0f;
                bool  roofed  = false;
                for (std::size_t j = 0; j < blocks.size(); ++j) {
                    if (j == i || !drawn(blocks[j]) || !blocks[j].contains(p)) continue;
                    if (blocks[j].surface(p) > s + 1e-3f) covered = covered || blocks[j].base <= s + 1e-3f;
                    const float clearance = blocks[j].base - s;
                    const bool  slab = blocks[j].max.x - blocks[j].min.x >= minRoofSpan && blocks[j].max.y - blocks[j].min.y >= minRoofSpan;
                    if (slab && clearance >= 1.0f && clearance <= maxCeiling && (!roofed || blocks[j].base < roof)) {
                        roofed = true;
                        roof   = blocks[j].base;
                    }
                }
                if (!covered && roofed) out.push_back({i, p, roof});
            }
        }
    }
    return out;
}

} // namespace carnage
