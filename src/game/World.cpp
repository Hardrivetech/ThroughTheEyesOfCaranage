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

namespace {

Block box(float x0, float z0, float x1, float z1, float h, glm::vec3 color,
          Material mat = Material::Concrete, float base = -0.2f) {
    return Block{{x0, z0}, {x1, z1}, h, h, 0, color, false, mat, true, base, true, -1};
}

Block ramp(float x0, float z0, float x1, float z1, int axis, float h0, float h1,
           glm::vec3 color, Material mat = Material::Concrete, float base = -0.2f) {
    return Block{{x0, z0}, {x1, z1}, h0, h1, axis, color, false, mat, true, base, true, -1};
}

// Everything past the corridor: a two-storey arena (stairs up the left wall,
// catwalks, a back bridge, a slide ramp down the right side), a gated drop-down
// pit with a stair climb back out. Step rise 0.35 m stays under stepHeight 0.4;
// elevated slabs have their underside at >= 1.9 m so the floor below stays
// walkable. Heights are untested guesses against the movement tuning.
void buildArenaAndPit(World& w) {
    const glm::vec3 concrete{0.48f, 0.48f, 0.46f};
    const glm::vec3 wallCol{0.18f, 0.22f, 0.26f};
    const glm::vec3 steelCol{0.25f, 0.25f, 0.28f};
    const glm::vec3 crateCol{0.55f, 0.24f, 0.12f};
    const glm::vec3 lightCol{3.0f, 2.5f, 1.8f};
    const glm::vec3 redLight{3.2f, 0.5f, 0.35f};
    const Material  C = Material::Concrete, P = Material::PaintedMetal, S = Material::Steel, T = Material::Tile;

    // ---- Arena shell: X -16..16, Z -130..-76 ----
    w.add(box(-16.0f, -131.0f, 16.0f, -75.0f, 0.0f, {0.36f, 0.36f, 0.34f}, T));
    w.add(box(-17.0f, -131.0f, -16.0f, -75.0f, 9.8f, wallCol, P));
    w.add(box( 16.0f, -131.0f,  17.0f, -75.0f, 9.8f, wallCol, P));
    // Back wall at Z -131..-130 with a 8 m gap (gate = door 1).
    w.add(box(-17.0f, -131.0f, -4.0f, -130.0f, 9.8f, concrete, C));
    w.add(box(  4.0f, -131.0f, 17.0f, -130.0f, 9.8f, concrete, C));
    w.add(box( -4.0f, -131.0f,  4.0f, -130.0f, 9.8f, concrete, C, 4.8f));
    {
        Block d = box(-4.0f, -131.0f, 4.0f, -130.0f, 4.8f, steelCol, S);
        d.door = 1;
        d.visible = false;
        w.add(d);
    }
    w.add(box(-17.0f, -131.0f, 17.0f, -75.0f, 10.6f, concrete, C, 9.8f));  // ceiling

    // ---- Stairs up the left wall: 10 steps from Z -78 to -86, top at 3.5 m ----
    for (int i = 1; i <= 10; ++i) {
        const float zHi = -78.0f - 0.8f * static_cast<float>(i - 1);
        w.add(box(-15.5f, zHi - 0.8f, -13.0f, zHi, 0.35f * static_cast<float>(i), steelCol, S));
    }
    // ---- Catwalks and bridge at 3.5 m (underside 2.9 m, walk beneath) ----
    w.add(box(-16.0f, -128.0f, -12.5f, -86.0f, 3.5f, steelCol, S, 2.9f));   // left
    w.add(box(-16.0f, -128.0f,  16.0f, -124.0f, 3.5f, steelCol, S, 2.9f));  // back bridge
    w.add(box( 10.0f, -128.0f,  16.0f, -100.0f, 3.5f, steelCol, S, 2.9f));  // right
    // Slide ramp: from the right catwalk down to the floor (z -100 -> -84).
    w.add(ramp(10.0f, -100.0f, 14.0f, -84.0f, 1, 3.5f, 0.0f, crateCol, P));

    // ---- Floor cover and vault targets ----
    w.add(box(-5.0f, -100.0f, -2.0f,  -98.0f, 1.1f, crateCol, P));
    w.add(box( 3.0f, -110.0f,  6.0f, -108.0f, 1.4f, crateCol, P));
    w.add(box(-9.0f, -116.0f, -6.0f, -114.0f, 1.1f, crateCol, P));
    w.add(box(-1.0f, -108.0f,  1.0f, -106.0f, 3.0f, concrete, C));        // column
    w.add(box(-4.0f,  -92.0f,  4.0f,  -91.4f, 0.9f, steelCol, S));        // vault hurdle

    // ---- Pit room: X -14..14, Z -170..-131, floor -6 ----
    w.add(box(-14.0f, -170.0f, 14.0f, -131.0f, -6.0f, {0.30f, 0.30f, 0.30f}, T, -6.2f));
    w.add(box(-15.0f, -171.0f, -14.0f, -131.0f, 4.8f, wallCol, P, -6.2f));
    w.add(box( 14.0f, -171.0f,  15.0f, -131.0f, 4.8f, wallCol, P, -6.2f));
    w.add(box(-15.0f, -171.0f,  15.0f, -170.0f, 4.8f, concrete, C, -6.2f));
    w.add(box(-15.0f, -171.0f,  15.0f, -131.0f, 5.6f, concrete, C, 4.8f));  // ceiling
    // Ledge you drop from (floor 0) plus the exit stairs (17 steps, -0.35 each).
    w.add(box(-4.0f, -136.0f, 14.0f, -131.0f, 0.0f, concrete, C, -6.2f));
    for (int i = 1; i <= 17; ++i) {
        const float zHi = -136.0f - 0.8f * static_cast<float>(i - 1);
        w.add(box(9.0f, zHi - 0.8f, 13.0f, zHi, -0.35f * static_cast<float>(i), steelCol, S, -6.2f));
    }
    // Cover in the pit.
    w.add(box(-8.0f, -152.0f, -5.0f, -150.0f, -4.9f, crateCol, P, -6.2f));
    w.add(box( 0.0f, -160.0f,  3.0f, -158.0f, -4.6f, crateCol, P, -6.2f));

    // ---- Visible ceiling fixtures and room lights ----
    for (float z : {-90.0f, -108.0f, -124.0f})
        for (float x : {-8.0f, 8.0f})
            w.add(box(x - 1.2f, z - 0.4f, x + 1.2f, z + 0.4f, 9.78f, lightCol, Material::Emissive, 9.65f));
    w.add(box(-1.0f, -152.0f, 1.0f, -148.0f, 4.78f, redLight, Material::Emissive, 4.65f));

    for (float z : {-90.0f, -108.0f, -124.0f})
        for (float x : {-8.0f, 8.0f})
            w.addLight({{x, 8.8f, z}, glm::vec3(10.0f, 8.2f, 6.0f), 24.0f});
    w.addLight({{0.0f, 3.0f, -134.0f}, glm::vec3(5.0f, 2.2f, 1.6f), 14.0f});
    w.addLight({{0.0f, 3.8f, -150.0f}, glm::vec3(8.0f, 1.2f, 0.8f), 20.0f});
    w.addLight({{0.0f, 3.8f, -164.0f}, glm::vec3(6.0f, 1.0f, 0.8f), 18.0f});
}

} // namespace

World World::testLevel() {
    World w;
    // Palette: industrial concrete, dark oxidized steel, tactical red & orange paint.
    const glm::vec3 concrete{0.48f, 0.48f, 0.46f};
    const glm::vec3 wallCol{0.18f, 0.22f, 0.26f};
    const glm::vec3 steelCol{0.25f, 0.25f, 0.28f};
    const glm::vec3 crateCol{0.55f, 0.24f, 0.12f};
    const glm::vec3 lightCol{3.0f, 2.5f, 1.8f};
    const Material  C = Material::Concrete, P = Material::PaintedMetal, S = Material::Steel, T = Material::Tile;

    // --- Main Floor (XZ: -6 to 6, -75 to 15) ---
    w.add(box(-6.0f, -75.0f, 6.0f, 15.0f, 0.0f, {0.38f, 0.38f, 0.36f}, T));

    // --- Side Walls (Left x: -4.0 to -3.0, Right x: 3.0 to 4.0, Height: 4.8m) ---
    w.add(box(-4.0f, -75.0f, -3.0f, 15.0f, 4.8f, wallCol, P));
    w.add(box( 3.0f, -75.0f,  4.0f, 15.0f, 4.8f, wallCol, P));

    // --- End Cap Walls (Z = +15, and the Z = -75 partition with door 0) ---
    // Partition: two wall pieces, a lintel and the door itself.
    w.add(box(-16.0f, -76.0f, -3.0f, -75.0f, 9.8f, concrete, C));
    w.add(box(  3.0f, -76.0f, 16.0f, -75.0f, 9.8f, concrete, C));
    w.add(box( -3.0f, -76.0f,  3.0f, -75.0f, 9.8f, concrete, C, 4.8f));
    {
        Block d = box(-3.0f, -76.0f, 3.0f, -75.0f, 4.8f, steelCol, S);
        d.door = 0;
        d.visible = false;  // drawn dynamically so it can open
        w.add(d);
    }
    w.add(box(-4.0f,  15.0f,  4.0f,  16.0f, 4.8f, concrete, C));

    // --- Ceiling Slabs (Overhead at y = 4.8m to 5.6m) ---
    w.add(box(-4.0f, -75.0f, 4.0f, 15.0f, 5.6f, concrete, C, 4.8f));

    // --- Overhead Light Fixture Strips (Emissive boxes attached to ceiling) ---
    for (float z = -65.0f; z <= 10.0f; z += 15.0f) {
        w.add(box(-0.6f, z - 1.5f, 0.6f, z + 1.5f, 4.78f, lightCol, Material::Emissive, 4.65f));
    }

    // --- Tactical Obstacles & Cover inside the Corridor ---
    // 1. Entry cover crates (Z = 0m)
    w.add(box(-2.8f, -2.0f, -1.2f, 0.0f, 1.1f, crateCol, P));
    w.add(box( 1.5f,  2.0f,  2.8f, 4.0f, 1.4f, crateCol, P));

    // 2. Low hurdle (Z = -15m): jump or vault over it
    w.add(box(-3.0f, -15.5f, 3.0f, -14.8f, 0.9f, steelCol, S));

    // 3. Wall-Run Elevated Panels (Z = -28m to -40m)
    w.add(box(-3.0f, -40.0f, -2.7f, -28.0f, 4.5f, steelCol, S));
    w.add(box( 2.7f, -40.0f,  3.0f, -28.0f, 4.5f, steelCol, S));

    // 4. Catwalk & Ramp (Z = -50m to -62m)
    w.add(ramp(-2.0f, -50.0f, -0.2f, -44.0f, 1, 2.2f, 0.0f, crateCol, P));
    w.add(box (-2.0f, -58.0f, -0.2f, -50.0f, 2.2f, concrete, C));
    w.add(box ( 1.2f, -62.0f,  2.8f, -56.0f, 1.2f, crateCol, P));

    // --- Structural Pillars (Invisible collision blocks; props drawn in main) ---
    for (float z : {-60.0f, -45.0f, -30.0f, -15.0f, 0.0f}) {
        Block bL = box(-3.0f, z - 0.4f, -2.2f, z + 0.4f, 4.8f, concrete, C);
        bL.visible = false;
        w.add(bL);

        Block bR = box( 2.2f, z - 0.4f,  3.0f, z + 0.4f, 4.8f, concrete, C);
        bR.visible = false;
        w.add(bR);
    }

    buildArenaAndPit(w);

    // Corridor lights (nearest few are picked per frame by the renderer loop).
    for (float z = -65.0f; z <= 10.0f; z += 15.0f)
        w.addLight({{0.0f, 4.4f, z}, glm::vec3(9.0f, 7.4f, 5.4f), 16.0f});

    return w;
}

} // namespace carnage
