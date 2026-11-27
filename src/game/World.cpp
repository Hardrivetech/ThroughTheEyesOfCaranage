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

std::optional<WorldHit> World::raycast(const glm::vec3& o, const glm::vec3& d,
                                       float maxT) const {
    std::optional<WorldHit> best;
    float limit = maxT;
    for (const Block& b : blocks_) {
        float     t = 0.0f;
        glm::vec3 n{0.0f};
        if (b.raycast(o, d, limit, t, n) && b.enabled) {
            best  = WorldHit{t, n, &b};
            limit = t;
        }
    }
    return best;
}

std::optional<GroundHit> World::ground(glm::vec2 p, float maxSurface) const {
    std::optional<GroundHit> best;
    for (const Block& b : blocks_) {
        if (!b.enabled || !b.contains(p)) continue;
        const float s = b.surface(p);
        if (s > maxSurface) continue;
        if (!best || s > best->height) best = GroundHit{s, b.normal()};
    }
    return best;
}

std::optional<float> World::ceiling(glm::vec2 p, float feetY, float maxHeadY) const {
    std::optional<float> lowest;
    for (const Block& b : blocks_) {
        if (!b.enabled || !b.contains(p)) continue;
        if (b.base >= feetY + 0.5f && b.base <= maxHeadY) {
            if (!lowest || b.base < *lowest) lowest = b.base;
        }
    }
    return lowest;
}

std::optional<WallHit> World::probeWall(glm::vec2 p, float feetY, float reach,
                                        float minHeight) const {
    std::optional<WallHit> best;
    for (const Block& b : blocks_) {
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
    bool hit = false;
    for (int iteration = 0; iteration < 3; ++iteration) {
        bool any = false;
        for (const Block& b : blocks_) {
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
        }
        hit = hit || any;
        if (!any) break;
    }
    return hit;
}

void World::setDoorOpen(int id, bool open) {
    for (Block& b : blocks_) if (b.door == id) b.enabled = !open;
}

} // namespace carnage
