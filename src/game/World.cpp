#include "game/World.hpp"

#include <cmath>

namespace carnage {

namespace {
constexpr float kBodyHeight = 1.9f;  // blocks whose underside is above this are overhead
}

float Block::surface(glm::vec2 p) const {
    const float len = max[axis] - min[axis];
    const float t   = len > 0.0f ? std::clamp((p[axis] - min[axis]) / len, 0.0f, 1.0f) : 0.0f;
    return h0 + (h1 - h0) * t;
}

bool Block::contains(glm::vec2 p) const {
    return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y;
}

glm::vec3 Block::normal() const {
    const float len   = max[axis] - min[axis];
    const float slope = len > 0.0f ? (h1 - h0) / len : 0.0f;
    glm::vec3 n{0.0f, 1.0f, 0.0f};
    n[axis == 0 ? 0 : 2] = -slope;
    return glm::normalize(n);
}

bool Block::raycast(const glm::vec3& o, const glm::vec3& d, float maxT, float& tOut,
                    glm::vec3& nOut) const {
    const float len   = max[axis] - min[axis];
    const float slope = len > 0.0f ? (h1 - h0) / len : 0.0f;
    glm::vec3 topN{0.0f, 1.0f, 0.0f};
    topN[axis == 0 ? 0 : 2] = -slope;

    // Half-spaces n.p <= d. Their intersection is the block.
    struct Plane { glm::vec3 n; float d; };
    const Plane planes[6] = {
        {{-1.0f, 0.0f, 0.0f}, -min.x},
        {{ 1.0f, 0.0f, 0.0f},  max.x},
        {{0.0f, 0.0f, -1.0f}, -min.y},
        {{0.0f, 0.0f,  1.0f},  max.y},
        {{0.0f, -1.0f, 0.0f}, -base},
        {topN, h0 - slope * min[axis]},
    };

    float     tEnter = 0.0f;
    float     tExit  = maxT;
    glm::vec3 nEnter{0.0f};
    for (const Plane& p : planes) {
        const float denom = glm::dot(p.n, d);
        const float dist  = p.d - glm::dot(p.n, o);
        if (std::abs(denom) < 1e-8f) {
            if (dist < 0.0f) return false;  // parallel and outside
            continue;
        }
        const float t = dist / denom;
        if (denom < 0.0f) {
            if (t > tEnter) { tEnter = t; nEnter = p.n; }
        } else {
            tExit = std::min(tExit, t);
        }
        if (tEnter > tExit) return false;
    }
    if (glm::dot(nEnter, nEnter) == 0.0f) return false;  // started inside
    tOut = tEnter;
    nOut = glm::normalize(nEnter);
    return true;
}

// ---- broadphase ----------------------------------------------------------------------------

namespace {
constexpr float kCellSize   = 8.0f;     // metres; doubled until the grid fits kMaxCells
constexpr int   kMaxCells   = 1 << 18;
constexpr int   kLargeCells = 64;       // a block covering more cells than this is always examined
constexpr float kMargin     = 1e-3f;    // query boxes are padded by this, far above float error
} // namespace

int World::Grid::cx(float x) const {
    const float f = std::floor((x - lo.x) * inv);
    if (!(f >= 0.0f)) return 0;  // also catches NaN
    return f >= static_cast<float>(nx - 1) ? nx - 1 : static_cast<int>(f);
}

int World::Grid::cz(float z) const {
    const float f = std::floor((z - lo.y) * inv);
    if (!(f >= 0.0f)) return 0;
    return f >= static_cast<float>(nz - 1) ? nz - 1 : static_cast<int>(f);
}

void World::buildGrid() const {
    Grid g;
    if (!blocks_.empty()) {
        g.lo = blocks_[0].min;
        g.hi = blocks_[0].max;
        for (const Block& b : blocks_) {
            g.lo = glm::min(g.lo, b.min);
            g.hi = glm::max(g.hi, b.max);
        }
        g.cell = kCellSize;
        for (;;) {
            const double cells = (std::floor(static_cast<double>(g.hi.x - g.lo.x) / g.cell) + 1.0) *
                                 (std::floor(static_cast<double>(g.hi.y - g.lo.y) / g.cell) + 1.0);
            if (cells <= kMaxCells) break;
            g.cell *= 2.0f;
        }
        g.inv = 1.0f / g.cell;
        g.nx  = static_cast<int>(std::floor((g.hi.x - g.lo.x) * g.inv)) + 1;
        g.nz  = static_cast<int>(std::floor((g.hi.y - g.lo.y) * g.inv)) + 1;

        // Two passes (count, then fill) give a compact array with each cell's list ascending.
        const std::size_t cells = static_cast<std::size_t>(g.nx) * static_cast<std::size_t>(g.nz);
        g.offsets.assign(cells + 1, 0);
        const auto span = [&](const Block& b, int& x0, int& x1, int& z0, int& z1) {
            x0 = g.cx(b.min.x); x1 = g.cx(b.max.x);
            z0 = g.cz(b.min.y); z1 = g.cz(b.max.y);
        };
        std::vector<char> isLarge(blocks_.size(), 0);
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            int x0, x1, z0, z1;
            span(blocks_[i], x0, x1, z0, z1);
            if ((x1 - x0 + 1) * (z1 - z0 + 1) > kLargeCells) {
                isLarge[i] = 1;
                g.large.push_back(static_cast<std::uint32_t>(i));
                continue;
            }
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x) ++g.offsets[static_cast<std::size_t>(z * g.nx + x) + 1];
        }
        for (std::size_t c = 0; c < cells; ++c) g.offsets[c + 1] += g.offsets[c];
        g.items.resize(g.offsets[cells]);
        std::vector<std::uint32_t> fill(g.offsets.begin(), g.offsets.end() - 1);
        for (std::size_t i = 0; i < blocks_.size(); ++i) {
            if (isLarge[i]) continue;
            int x0, x1, z0, z1;
            span(blocks_[i], x0, x1, z0, z1);
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                    g.items[fill[static_cast<std::size_t>(z * g.nx + x)]++] = static_cast<std::uint32_t>(i);
        }
    }
    grid_ = std::move(g);
    seen_.assign(blocks_.size(), 0);
    epoch_     = 0;
    gridDirty_ = false;
}

void World::gather(glm::vec2 lo, glm::vec2 hi) const {
    ensureGrid();
    cand_.assign(grid_.large.begin(), grid_.large.end());
    if (grid_.nx > 0) {
        const int x0 = grid_.cx(lo.x - kMargin), x1 = grid_.cx(hi.x + kMargin);
        const int z0 = grid_.cz(lo.y - kMargin), z1 = grid_.cz(hi.y + kMargin);
        for (int z = z0; z <= z1; ++z) {
            const std::size_t row = static_cast<std::size_t>(z) * static_cast<std::size_t>(grid_.nx);
            const std::uint32_t begin = grid_.offsets[row + static_cast<std::size_t>(x0)];
            const std::uint32_t end   = grid_.offsets[row + static_cast<std::size_t>(x1) + 1];
            cand_.insert(cand_.end(), grid_.items.begin() + begin, grid_.items.begin() + end);
        }
    }
    std::sort(cand_.begin(), cand_.end());
    cand_.erase(std::unique(cand_.begin(), cand_.end()), cand_.end());
}

// ---- queries --------------------------------------------------------------------------------
// Each loop body is the original per-block logic, run over the broadphase candidates in
// ascending index order (the order, and therefore the tie-breaking, a full scan has).

std::optional<WorldHit> World::raycast(const glm::vec3& o, const glm::vec3& d, float maxT) const {
    ensureGrid();
    ++stats_.queries;

    std::optional<WorldHit> best;
    std::uint32_t           bestIdx = 0;
    // The nearest hit wins; on an exact tie the later block does (what a front-to-back
    // scan that keeps shrinking its limit produces).
    const auto consider = [&](std::uint32_t idx) {
        const Block& b = blocks_[idx];
        if (!b.enabled) return;
        ++stats_.blocksTested;
        float     t = 0.0f;
        glm::vec3 n{0.0f};
        if (!b.raycast(o, d, maxT, t, n)) return;
        if (!best || t < best->t || (t == best->t && idx > bestIdx)) {
            best    = WorldHit{t, n, &b};
            bestIdx = idx;
        }
    };

    for (const std::uint32_t idx : grid_.large) consider(idx);
    if (grid_.nx == 0) return best;

    // Clip the ray's XZ line to the grid's bounds: nothing outside them can be hit, and a
    // huge maxT must not mean a huge walk.
    float t0 = 0.0f, t1 = maxT;
    const float oxz[2] = {o.x, o.z}, dxz[2] = {d.x, d.z};
    const float lo[2] = {grid_.lo.x - kMargin, grid_.lo.y - kMargin};
    const float hi[2] = {grid_.hi.x + kMargin, grid_.hi.y + kMargin};
    for (int a = 0; a < 2; ++a) {
        if (std::abs(dxz[a]) < 1e-9f) {
            if (oxz[a] < lo[a] || oxz[a] > hi[a]) return best;
            continue;
        }
        float ta = (lo[a] - oxz[a]) / dxz[a], tb = (hi[a] - oxz[a]) / dxz[a];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    if (t0 > t1) return best;

    const float h = std::sqrt(d.x * d.x + d.z * d.z);
    if (++epoch_ == 0) { std::fill(seen_.begin(), seen_.end(), 0u); epoch_ = 1; }
    const auto visit = [&](glm::vec2 a, glm::vec2 b) {
        const glm::vec2 lo2 = glm::min(a, b) - glm::vec2(kMargin), hi2 = glm::max(a, b) + glm::vec2(kMargin);
        const int x0 = grid_.cx(lo2.x), x1 = grid_.cx(hi2.x), z0 = grid_.cz(lo2.y), z1 = grid_.cz(hi2.y);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                const std::size_t c = static_cast<std::size_t>(z) * static_cast<std::size_t>(grid_.nx) + static_cast<std::size_t>(x);
                for (std::uint32_t k = grid_.offsets[c]; k < grid_.offsets[c + 1]; ++k) {
                    const std::uint32_t idx = grid_.items[k];
                    if (seen_[idx] == epoch_) continue;
                    seen_[idx] = epoch_;
                    consider(idx);
                }
            }
    };

    if (h < 1e-9f) {  // straight up or down: one column of cells
        visit({o.x, o.z}, {o.x, o.z});
        return best;
    }
    // Walk the segment in pieces about one cell long. Each piece's padded box covers every
    // cell the ray touches inside it, and the walk stops once it is past the best hit.
    const float dt = grid_.cell / h;
    for (int k = 0;; ++k) {
        const float s = t0 + static_cast<float>(k) * dt;
        if (s > t1) break;
        if (best && s > best->t) break;
        const float e = std::min(s + dt, t1);
        visit({o.x + d.x * s, o.z + d.z * s}, {o.x + d.x * e, o.z + d.z * e});
        if (e >= t1) break;
    }
    return best;
}

std::optional<GroundHit> World::ground(glm::vec2 p, float maxSurface) const {
    gather(p, p);
    ++stats_.queries;
    std::optional<GroundHit> best;
    for (const std::uint32_t i : cand_) {
        const Block& b = blocks_[i];
        if (!b.enabled || !b.contains(p)) continue;
        ++stats_.blocksTested;
        const float s = b.surface(p);
        if (s > maxSurface) continue;
        if (!best || s > best->height) best = GroundHit{s, b.normal()};
    }
    return best;
}

std::optional<float> World::ceiling(glm::vec2 p, float feetY, float maxHeadY) const {
    gather(p, p);
    ++stats_.queries;
    std::optional<float> lowest;
    for (const std::uint32_t i : cand_) {
        const Block& b = blocks_[i];
        if (!b.enabled || !b.contains(p)) continue;
        ++stats_.blocksTested;
        if (b.base >= feetY + 0.5f && b.base <= maxHeadY) {
            if (!lowest || b.base < *lowest) lowest = b.base;
        }
    }
    return lowest;
}

std::optional<WallHit> World::probeWall(glm::vec2 p, float feetY, float reach,
                                        float minHeight) const {
    gather(p - glm::vec2(reach), p + glm::vec2(reach));
    ++stats_.queries;
    std::optional<WallHit> best;
    for (const std::uint32_t i : cand_) {
        const Block& b = blocks_[i];
        ++stats_.blocksTested;
        const glm::vec2 c = glm::clamp(p, b.min, b.max);
        const glm::vec2 d = p - c;
        const float dist  = glm::length(d);
        if (!b.enabled || dist < 1e-5f || dist > reach) continue;
        if (b.base >= feetY + kBodyHeight) continue;  // overhead, not a wall
        if (b.surface(c) < feetY + minHeight) continue;
        if (!best || dist < best->distance) best = WallHit{d / dist, dist};
    }
    return best;
}

bool World::resolveWalls(glm::vec2& p, float feetY, float radius, float step,
                         glm::vec2& vel) const {
    ++stats_.queries;
    bool hit = false;
    for (int iteration = 0; iteration < 3; ++iteration) {
        bool any = false;
        // A full scan visits blocks in index order with `p` already moved by earlier pushes.
        // To match it exactly, candidates come from the *current* p, and after a push the
        // candidate list is rebuilt there and the pass carries on from the next higher index.
        std::int64_t cursor = -1;
        gather(p - glm::vec2(radius), p + glm::vec2(radius));
        std::size_t k = 0;
        while (k < cand_.size()) {
            const std::uint32_t idx = cand_[k++];
            if (static_cast<std::int64_t>(idx) <= cursor) continue;
            cursor = idx;
            const Block& b = blocks_[idx];
            ++stats_.blocksTested;

            if (!b.enabled) continue;
            const glm::vec2 c = glm::clamp(p, b.min, b.max);
            if (b.base >= feetY + kBodyHeight) continue;  // overhead slab: walk under it
            if (b.surface(c) <= feetY + step) continue;  // walkable, not a wall

            const glm::vec2 d  = p - c;
            const float     d2 = glm::dot(d, d);
            glm::vec2 n;
            float     push;
            if (d2 > 1e-10f) {
                const float dist = std::sqrt(d2);
                if (dist >= radius) continue;
                n    = d / dist;
                push = radius - dist;
            } else {
                // Centre is inside the rectangle: exit through the nearest side.
                const float dl = p.x - b.min.x, dr = b.max.x - p.x;
                const float db = p.y - b.min.y, dt = b.max.y - p.y;
                const float m  = std::min({dl, dr, db, dt});
                if      (m == dl) n = {-1.0f, 0.0f};
                else if (m == dr) n = { 1.0f, 0.0f};
                else if (m == db) n = {0.0f, -1.0f};
                else              n = {0.0f,  1.0f};
                push = m + radius;
            }
            p += n * push;
            const float vn = glm::dot(vel, n);
            if (vn < 0.0f) vel -= n * vn;
            any = true;

            gather(p - glm::vec2(radius), p + glm::vec2(radius));  // p moved: candidates near the new spot
            k = 0;
        }
        hit = hit || any;
        if (!any) break;
    }
    return hit;
}

std::vector<int> World::doorIds() const {
    std::vector<int> ids;
    for (const Block& b : blocks_)
        if (b.door >= 0) ids.push_back(b.door);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

bool World::hasDoor(int id) const {
    return id >= 0 && std::any_of(blocks_.begin(), blocks_.end(), [&](const Block& b) { return b.door == id; });
}

void World::setDoorOpen(int id, bool open) {
    if (id < 0) return;  // -1 is "not a door": it must never match every ordinary block
    for (Block& b : blocks_) if (b.door == id) b.enabled = !open;
}

} // namespace carnage
