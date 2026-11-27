#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Level.hpp"
#include "game/World.hpp"
#include "test_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

using namespace carnage;
using namespace carnage::test;
using Catch::Approx;

// ---------------------------------------------------------------------------------------------
// The ORIGINAL brute-force queries, copied verbatim from World.cpp before the broadphase was
// added (only `World::` was removed so they can live in a struct). They scan every block, so
// they are the oracle the grid-accelerated World must agree with exactly.
// ---------------------------------------------------------------------------------------------
struct BruteForce {
    std::span<const Block> blocks_;
    static constexpr float kBodyHeight = 1.9f;

std::optional<WorldHit> raycast(const glm::vec3& o, const glm::vec3& d,
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

std::optional<GroundHit> ground(glm::vec2 p, float maxSurface) const {
    std::optional<GroundHit> best;
    for (const Block& b : blocks_) {
        if (!b.enabled || !b.contains(p)) continue;
        const float s = b.surface(p);
        if (s > maxSurface) continue;
        if (!best || s > best->height) best = GroundHit{s, b.normal()};
    }
    return best;
}

std::optional<float> ceiling(glm::vec2 p, float feetY, float maxHeadY) const {
    std::optional<float> lowest;
    for (const Block& b : blocks_) {
        if (!b.enabled || !b.contains(p)) continue;
        if (b.base >= feetY + 0.5f && b.base <= maxHeadY) {
            if (!lowest || b.base < *lowest) lowest = b.base;
        }
    }
    return lowest;
}

std::optional<WallHit> probeWall(glm::vec2 p, float feetY, float reach,
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

bool resolveWalls(glm::vec2& p, float feetY, float radius, float step,
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

};

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

namespace {

struct Lcg {
    std::uint32_t s;
    explicit Lcg(std::uint32_t seed) : s(seed * 2654435761u + 12345u) {}
    std::uint32_t next() { s = s * 1664525u + 1013904223u; return s >> 8; }
    float f() { return static_cast<float>(next() & 0xFFFFFFu) / 16777216.0f; }                  // [0,1)
    float range(float a, float b) { return a + (b - a) * f(); }
    int   below(int n) { return static_cast<int>(next() % static_cast<std::uint32_t>(n)); }
};

float snap8(float v) { return std::round(v / 8.0f) * 8.0f; }

// A varied world: boxes, ramps, thin walls, overhead slabs, doors and the odd giant block.
// `align` snaps everything to multiples of 8 m (the grid's cell size) to stress cell edges.
World randomWorld(std::uint32_t seed, int count, float extent, bool giants = true, bool align = false) {
    Lcg r(seed);
    World w;
    for (int i = 0; i < count; ++i) {
        Block b;
        float x = r.range(-extent, extent), z = r.range(-extent, extent);
        float sx = r.range(0.3f, 12.0f), sz = r.range(0.3f, 12.0f);
        const int kind = r.below(10);
        b.h0 = b.h1 = r.range(0.2f, 9.0f);
        if (kind == 7) { if (r.below(2)) sx = 0.5f; else sz = 0.5f; (r.below(2) ? sx : sz) = r.range(10.0f, 60.0f); }
        if (kind == 8) { sx = 6.0f; sz = 1.0f; b.h0 = b.h1 = 4.8f; b.door = r.below(4); }
        if (kind == 9 && giants && r.below(8) == 0) { sx = extent * 1.5f; sz = extent * 1.5f; b.h0 = b.h1 = r.range(0.0f, 1.0f); }
        if (kind == 5 || kind == 6) { b.axis = r.below(2); b.h0 = r.range(0.0f, 6.0f); b.h1 = r.range(0.0f, 6.0f); }
        if (r.below(6) == 0) b.base = r.range(2.4f, 5.0f);            // an overhead slab
        if (align) { x = snap8(x); z = snap8(z); sx = std::max(8.0f, snap8(sx)); sz = std::max(8.0f, snap8(sz)); }
        b.min = {x, z};
        b.max = {x + sx, z + sz};
        w.add(b);
    }
    return w;
}

std::ptrdiff_t indexOf(const World& w, const Block* b) { return b ? b - w.blocks().data() : -1; }

bool sameHit(const std::optional<WorldHit>& a, const std::optional<WorldHit>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->t == b->t && a->normal == b->normal && a->block == b->block;
}

glm::vec3 randomDir(Lcg& r) {
    switch (r.below(8)) {
        case 0: return {1.0f, 0.0f, 0.0f};
        case 1: return {-1.0f, 0.0f, 0.0f};
        case 2: return {0.0f, 0.0f, 1.0f};
        case 3: return {0.0f, 0.0f, -1.0f};
        case 4: return {0.0f, r.below(2) ? 1.0f : -1.0f, 0.0f};   // straight up / down
        case 5: { const float a = r.range(0.0f, 6.2831853f); return {std::cos(a), 0.0f, std::sin(a)}; }  // level
        default: {
            glm::vec3 v{r.range(-1, 1), r.range(-1, 1), r.range(-1, 1)};
            return glm::dot(v, v) < 1e-4f ? glm::vec3(0, 0, 1) : glm::normalize(v);
        }
    }
}

// Casts `n` random rays and requires the World and the oracle to agree exactly.
void compareRays(const World& w, Lcg& r, int n, float extent) {
    const BruteForce bf{w.blocks()};
    for (int i = 0; i < n; ++i) {
        const glm::vec3 o{r.range(-1.4f * extent, 1.4f * extent), r.range(-2.0f, 10.0f), r.range(-1.4f * extent, 1.4f * extent)};
        const glm::vec3 d = randomDir(r);
        constexpr float kMaxT[] = {0.0f, 0.5f, 10.0f, 100.0f, 250.0f, 5000.0f, 1e9f};
        const float maxT = kMaxT[r.below(7)];
        const auto a = w.raycast(o, d, maxT);
        const auto b = bf.raycast(o, d, maxT);
        if (!sameHit(a, b))
            FAIL("ray " << i << " from (" << o.x << ", " << o.y << ", " << o.z << ") dir (" << d.x << ", " << d.y << ", " << d.z
                        << ") maxT " << maxT << ": world "
                        << (a ? "hit block " + std::to_string(indexOf(w, a->block)) + " at t=" + std::to_string(a->t) : std::string("miss"))
                        << ", brute force "
                        << (b ? "hit block " + std::to_string(indexOf(w, b->block)) + " at t=" + std::to_string(b->t) : std::string("miss")));
    }
}

void comparePoints(const World& w, Lcg& r, int n, float extent) {
    const BruteForce bf{w.blocks()};
    const auto blocks = w.blocks();
    for (int i = 0; i < n; ++i) {
        glm::vec2 p{r.range(-1.3f * extent, 1.3f * extent), r.range(-1.3f * extent, 1.3f * extent)};
        if (!blocks.empty() && r.below(3) == 0) {  // land exactly on a block's edge or corner
            const Block& b = blocks[static_cast<std::size_t>(r.below(static_cast<int>(blocks.size())))];
            p = {r.below(2) ? b.min.x : b.max.x, r.below(2) ? b.min.y : b.max.y};
            if (r.below(2)) p.x = r.range(b.min.x, b.max.x);
        }
        const float feet = r.range(-1.0f, 6.0f);

        const float maxSurface = r.range(-1.0f, 10.0f);
        const auto ga = w.ground(p, maxSurface), gb = bf.ground(p, maxSurface);
        if (ga.has_value() != gb.has_value() || (ga && (ga->height != gb->height || ga->normal != gb->normal)))
            FAIL("ground mismatch at (" << p.x << ", " << p.y << ") maxSurface " << maxSurface);

        const float head = feet + r.range(0.5f, 6.0f);
        const auto ca = w.ceiling(p, feet, head), cb = bf.ceiling(p, feet, head);
        if (ca != cb) FAIL("ceiling mismatch at (" << p.x << ", " << p.y << ") feet " << feet << " head " << head);

        const float reach = r.range(0.05f, 6.0f), minH = r.range(0.0f, 4.0f);
        const auto wa = w.probeWall(p, feet, reach, minH), wb = bf.probeWall(p, feet, reach, minH);
        if (wa.has_value() != wb.has_value() || (wa && (wa->normal != wb->normal || wa->distance != wb->distance)))
            FAIL("probeWall mismatch at (" << p.x << ", " << p.y << ") reach " << reach);
    }
}

void compareResolve(const World& w, Lcg& r, int n, float extent) {
    const BruteForce bf{w.blocks()};
    const auto blocks = w.blocks();
    for (int i = 0; i < n; ++i) {
        glm::vec2 p{r.range(-1.2f * extent, 1.2f * extent), r.range(-1.2f * extent, 1.2f * extent)};
        if (!blocks.empty() && r.below(2) == 0) {  // start next to, on, or inside a block
            const Block& b = blocks[static_cast<std::size_t>(r.below(static_cast<int>(blocks.size())))];
            p = {r.range(b.min.x - 1.5f, b.max.x + 1.5f), r.range(b.min.y - 1.5f, b.max.y + 1.5f)};
        }
        const float feet = r.range(-0.5f, 5.0f), radius = r.range(0.1f, 2.0f), step = r.range(0.1f, 0.8f);
        const glm::vec2 vel0{r.range(-12.0f, 12.0f), r.range(-12.0f, 12.0f)};

        glm::vec2 pa = p, va = vel0, pb = p, vb = vel0;
        const bool ra = w.resolveWalls(pa, feet, radius, step, va);
        const bool rb = bf.resolveWalls(pb, feet, radius, step, vb);
        if (ra != rb || pa != pb || va != vb)
            FAIL("resolveWalls mismatch from (" << p.x << ", " << p.y << ") feet " << feet << " radius " << radius
                                                << ": world (" << pa.x << ", " << pa.y << ") vs brute force (" << pb.x << ", " << pb.y << ")");
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Equivalence with the brute-force scan
// ---------------------------------------------------------------------------------------------

TEST_CASE("Broadphase rays match brute force across world sizes and shapes", "[broadphase][raycast]") {
    struct Row { int count; float extent; bool giants; bool align; };
    const Row rows[] = {
        {1, 20.0f, false, false},   {5, 20.0f, true, false},     {50, 60.0f, true, false},
        {400, 200.0f, true, false}, {400, 200.0f, true, true},   {2000, 1000.0f, true, false},
        {2000, 1000.0f, false, true}, {300, 50000.0f, true, false},  // a huge island: the grid must coarsen
    };
    std::uint32_t seed = 1;
    for (const Row& row : rows) {
        INFO("blocks " << row.count << " extent " << row.extent << (row.align ? " aligned to cells" : ""));
        const World w = randomWorld(seed++, row.count, row.extent, row.giants, row.align);
        Lcg r(seed * 7);
        compareRays(w, r, 3000, row.extent);
    }
}

TEST_CASE("Broadphase ground, ceiling and wall probes match brute force", "[broadphase][queries]") {
    struct Row { int count; float extent; bool align; };
    const Row rows[] = {{1, 20.0f, false}, {30, 40.0f, false}, {400, 200.0f, false}, {400, 200.0f, true}, {2000, 1000.0f, false}};
    std::uint32_t seed = 100;
    for (const Row& row : rows) {
        INFO("blocks " << row.count << " extent " << row.extent << (row.align ? " aligned" : ""));
        const World w = randomWorld(seed++, row.count, row.extent, true, row.align);
        Lcg r(seed * 13);
        comparePoints(w, r, 2500, row.extent);
    }
}

TEST_CASE("Broadphase wall resolution matches brute force, including pushes that chain", "[broadphase][resolve]") {
    struct Row { int count; float extent; bool align; };
    // Small, dense worlds make blocks overlap and corners where one push leads into the next.
    const Row rows[] = {{6, 12.0f, false}, {40, 25.0f, false}, {40, 25.0f, true}, {400, 100.0f, false}, {1500, 500.0f, false}};
    std::uint32_t seed = 200;
    for (const Row& row : rows) {
        INFO("blocks " << row.count << " extent " << row.extent << (row.align ? " aligned" : ""));
        const World w = randomWorld(seed++, row.count, row.extent, true, row.align);
        Lcg r(seed * 17);
        compareResolve(w, r, 4000, row.extent);
    }
}

TEST_CASE("Rays along and across cell boundaries agree with brute force", "[broadphase][raycast][edge]") {
    // Everything sits on the 8 m grid, so rays can run exactly along cell edges.
    World w;
    for (int ix = -4; ix < 4; ++ix)
        for (int iz = -4; iz < 4; ++iz) {
            if ((ix + iz) % 3 != 0) continue;
            Block b;
            b.min = {static_cast<float>(ix) * 8.0f, static_cast<float>(iz) * 8.0f};
            b.max = {b.min.x + 8.0f, b.min.y + 8.0f};
            b.h0 = b.h1 = 1.0f + static_cast<float>((ix + 5) % 4);
            w.add(b);
        }
    const BruteForce bf{w.blocks()};

    int checked = 0, hits = 0;
    for (float line = -40.0f; line <= 40.0f; line += 4.0f) {                 // on and between cell edges
        for (const glm::vec3 d : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)}) {
            const glm::vec3 o = (d.x != 0.0f) ? glm::vec3(-d.x * 50.0f, 0.5f, line) : glm::vec3(line, 0.5f, -d.z * 50.0f);
            const auto a = w.raycast(o, d, 200.0f), b = bf.raycast(o, d, 200.0f);
            REQUIRE(sameHit(a, b));
            ++checked;
            hits += a ? 1 : 0;
        }
    }
    // Exact diagonals through cell corners, and rays that start exactly on a block face or edge.
    const glm::vec3 diag = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    for (float k = -32.0f; k <= 32.0f; k += 8.0f) {
        for (const glm::vec3 o : {glm::vec3(k - 40.0f, 0.5f, k - 40.0f), glm::vec3(k - 40.0f, 0.5f, k - 32.0f)}) {
            REQUIRE(sameHit(w.raycast(o, diag, 300.0f), bf.raycast(o, diag, 300.0f)));
            ++checked;
        }
    }
    for (const Block& blk : w.blocks()) {
        for (const glm::vec3 o : {glm::vec3(blk.min.x, 0.5f, blk.min.y), glm::vec3(blk.max.x, 0.5f, blk.max.y),
                                  glm::vec3(blk.min.x, 5.0f, blk.max.y), glm::vec3((blk.min.x + blk.max.x) * 0.5f, 20.0f, blk.min.y)}) {
            for (const glm::vec3 d : {glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, -1, 0), diag}) {
                REQUIRE(sameHit(w.raycast(o, d, 100.0f), bf.raycast(o, d, 100.0f)));
                ++checked;
            }
        }
    }
    CHECK(checked > 300);
    CHECK(hits > 20);  // the test really does hit things
}

TEST_CASE("Very long, vertical and out-of-bounds rays", "[broadphase][raycast][edge]") {
    // A 2 km island: a long chain of walls along X, plus a floor that spans it all.
    World w;
    Block floor;
    floor.min = {-1000.0f, -50.0f};
    floor.max = {1000.0f, 50.0f};
    floor.h0 = floor.h1 = 0.0f;
    w.add(floor);
    for (int i = 0; i < 100; ++i) {
        Block b;
        b.min = {-1000.0f + static_cast<float>(i) * 20.0f, -3.0f};
        b.max = {b.min.x + 1.0f, 3.0f};
        b.h0 = b.h1 = 4.0f;
        w.add(b);
    }
    const BruteForce bf{w.blocks()};

    SECTION("end to end along the island") {
        const auto a = w.raycast({-1500.0f, 1.0f, 0.0f}, {1, 0, 0}, 3000.0f);
        REQUIRE(sameHit(a, bf.raycast({-1500.0f, 1.0f, 0.0f}, {1, 0, 0}, 3000.0f)));
        REQUIRE(a.has_value());
        CHECK(a->t == Approx(500.0f));   // the first wall starts at x = -1000
        CHECK(indexOf(w, a->block) == 1);
        const auto back = w.raycast({1500.0f, 1.0f, 0.0f}, {-1, 0, 0}, 3000.0f);
        REQUIRE(sameHit(back, bf.raycast({1500.0f, 1.0f, 0.0f}, {-1, 0, 0}, 3000.0f)));
    }
    SECTION("an effectively infinite ray ends quickly and correctly") {
        for (const float maxT : {1e6f, 1e9f, 3.4e38f}) {
            REQUIRE(sameHit(w.raycast({-1500.0f, 1.0f, 0.0f}, {1, 0, 0}, maxT), bf.raycast({-1500.0f, 1.0f, 0.0f}, {1, 0, 0}, maxT)));
            REQUIRE(sameHit(w.raycast({0.0f, 50.0f, 0.0f}, glm::normalize(glm::vec3(1, -0.01f, 0.2f)), maxT),
                            bf.raycast({0.0f, 50.0f, 0.0f}, glm::normalize(glm::vec3(1, -0.01f, 0.2f)), maxT)));
        }
    }
    SECTION("vertical rays") {
        for (const float x : {-1000.0f, -999.5f, 0.0f, 500.5f, 999.0f, 2000.0f}) {
            for (const float dy : {-1.0f, 1.0f}) {
                const glm::vec3 o{x, dy < 0.0f ? 20.0f : -20.0f, 0.0f};
                REQUIRE(sameHit(w.raycast(o, {0, dy, 0}, 100.0f), bf.raycast(o, {0, dy, 0}, 100.0f)));
            }
        }
    }
    SECTION("rays that start or stay outside the island") {
        REQUIRE(sameHit(w.raycast({-5000.0f, 1.0f, 500.0f}, {1, 0, 0}, 20000.0f), bf.raycast({-5000.0f, 1.0f, 500.0f}, {1, 0, 0}, 20000.0f)));
        REQUIRE(sameHit(w.raycast({0.0f, 1.0f, 500.0f}, {0, 0, 1}, 1000.0f), bf.raycast({0.0f, 1.0f, 500.0f}, {0, 0, 1}, 1000.0f)));
        REQUIRE(sameHit(w.raycast({0.0f, 1.0f, 500.0f}, {0, 0, -1}, 1000.0f), bf.raycast({0.0f, 1.0f, 500.0f}, {0, 0, -1}, 1000.0f)));
        CHECK_FALSE(w.raycast({0.0f, 1.0f, 500.0f}, {1, 0, 0}, 1000.0f).has_value());
    }
    SECTION("a zero-length range finds nothing") {
        CHECK_FALSE(w.raycast({-1001.0f, 1.0f, 0.0f}, {1, 0, 0}, 0.0f).has_value());
    }
}

TEST_CASE("Doors opening and closing change results exactly as a full scan would", "[broadphase][doors]") {
    World w = randomWorld(31, 200, 60.0f, true, false);
    REQUIRE_FALSE(w.doorIds().empty());
    Lcg r(77);

    for (int step = 0; step < 150; ++step) {
        const auto ids = w.doorIds();
        const int roll = r.below(6);
        if (roll == 0) w.resetDoors();
        else w.setDoorOpen(ids[static_cast<std::size_t>(r.below(static_cast<int>(ids.size())))], roll % 2 == 0);

        Lcg a(static_cast<std::uint32_t>(step) + 1000u), b = a, c = a;
        compareRays(w, a, 40, 60.0f);
        comparePoints(w, b, 25, 60.0f);
        compareResolve(w, c, 25, 60.0f);
    }

    SECTION("a closed door blocks a ray and an open one lets it through") {
        World d;
        Block floor;
        floor.min = {-20, -20}; floor.max = {20, 20};
        d.add(floor);
        Block door;
        door.min = {5, -3}; door.max = {6, 3}; door.h0 = door.h1 = 4.0f; door.door = 3;
        d.add(door);
        const glm::vec3 o{0, 1, 0}, dir{1, 0, 0};
        REQUIRE(d.raycast(o, dir, 50.0f).has_value());
        CHECK(d.raycast(o, dir, 50.0f)->t == Approx(5.0f));
        d.setDoorOpen(3, true);
        CHECK_FALSE(d.raycast(o, dir, 50.0f).has_value());
        d.setDoorOpen(3, false);
        CHECK(d.raycast(o, dir, 50.0f)->t == Approx(5.0f));

        glm::vec2 p{4.8f, 0.0f}, vel{2.0f, 0.0f};
        CHECK(d.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
        d.setDoorOpen(3, true);
        p = {4.8f, 0.0f};
        vel = {2.0f, 0.0f};
        CHECK_FALSE(d.resolveWalls(p, 0.0f, 0.4f, 0.4f, vel));
    }
}

TEST_CASE("Blocks added after queries are seen immediately", "[broadphase][add]") {
    World w;
    const auto wall = [](float x, float h) {
        Block b;
        b.min = {x, -5.0f}; b.max = {x + 1.0f, 5.0f}; b.h0 = b.h1 = h;
        return b;
    };
    CHECK_FALSE(w.raycast({0, 1, 0}, {1, 0, 0}, 100.0f).has_value());   // empty world

    w.add(wall(10.0f, 3.0f));
    REQUIRE(w.raycast({0, 1, 0}, {1, 0, 0}, 100.0f).has_value());
    CHECK(w.raycast({0, 1, 0}, {1, 0, 0}, 100.0f)->t == Approx(10.0f));

    w.add(wall(4.0f, 3.0f));            // a nearer wall, added after the grid was built
    CHECK(w.raycast({0, 1, 0}, {1, 0, 0}, 100.0f)->t == Approx(4.0f));

    w.add(wall(500.0f, 3.0f));          // far outside the old bounds
    CHECK(w.raycast({100, 1, 0}, {1, 0, 0}, 1000.0f)->t == Approx(400.0f));
    CHECK(w.ground({500.5f, 0.0f}, 10.0f).has_value());

    // And throughout growth, results never diverge from the full scan.
    Lcg r(5);
    for (int i = 0; i < 40; ++i) {
        w.add(wall(r.range(-300.0f, 300.0f), r.range(1.0f, 6.0f)));
        const BruteForce bf{w.blocks()};
        const glm::vec3 o{r.range(-350.0f, 350.0f), 1.0f, r.range(-8.0f, 8.0f)};
        REQUIRE(sameHit(w.raycast(o, {1, 0, 0}, 800.0f), bf.raycast(o, {1, 0, 0}, 800.0f)));
    }
}

TEST_CASE("Ties are broken exactly as a full scan breaks them", "[broadphase][ties]") {
    SECTION("two blocks hit at the same distance: the later one wins") {
        World w;
        for (int i = 0; i < 3; ++i) {              // identical, stacked blocks
            Block b;
            b.min = {5, -2}; b.max = {6, 2}; b.h0 = b.h1 = 3.0f;
            b.color = glm::vec3(static_cast<float>(i) * 0.1f);
            w.add(b);
        }
        const auto hit = w.raycast({0, 1, 0}, {1, 0, 0}, 50.0f);
        REQUIRE(hit.has_value());
        CHECK(indexOf(w, hit->block) == 2);
        CHECK(sameHit(hit, BruteForce{w.blocks()}.raycast({0, 1, 0}, {1, 0, 0}, 50.0f)));
    }
    SECTION("two surfaces at the same height: the earlier one wins") {
        World w;
        Block flat;
        flat.min = {0, 0}; flat.max = {4, 4}; flat.h0 = flat.h1 = 2.0f;
        Block ramp;                              // rises from 0 to 4 along Z: 2.0 exactly at z = 2
        ramp.min = {0, 0}; ramp.max = {4, 4}; ramp.axis = 1; ramp.h0 = 0.0f; ramp.h1 = 4.0f;
        w.add(flat);
        w.add(ramp);
        const auto g = w.ground({1.0f, 2.0f}, 10.0f);
        const auto o = BruteForce{w.blocks()}.ground({1.0f, 2.0f}, 10.0f);
        REQUIRE(g.has_value());
        REQUIRE(o.has_value());
        CHECK(g->height == Approx(2.0f));
        CHECK(g->normal == o->normal);
        CHECK(near(g->normal, {0.0f, 1.0f, 0.0f}));  // the flat block, the earlier one
    }
    SECTION("random worlds with many coincident blocks") {
        Lcg r(9);
        World w;
        for (int i = 0; i < 60; ++i) {
            Block b;
            const float x = std::round(r.range(-4.0f, 4.0f)) * 2.0f, z = std::round(r.range(-4.0f, 4.0f)) * 2.0f;
            b.min = {x, z}; b.max = {x + 2.0f, z + 2.0f};
            b.h0 = b.h1 = std::round(r.range(1.0f, 3.0f));
            b.axis = r.below(2);
            if (r.below(3) == 0) b.h1 = b.h0 + 1.0f;
            w.add(b);
        }
        Lcg a(1), b2(2), c(3);
        compareRays(w, a, 3000, 10.0f);
        comparePoints(w, b2, 3000, 10.0f);
        compareResolve(w, c, 3000, 10.0f);
    }
}

TEST_CASE("Empty and degenerate worlds", "[broadphase][edge]") {
    SECTION("an empty world") {
        World w;
        CHECK_FALSE(w.raycast({0, 1, 0}, {1, 0, 0}, 100.0f).has_value());
        CHECK_FALSE(w.ground({0, 0}, 10.0f).has_value());
        CHECK_FALSE(w.ceiling({0, 0}, 0.0f, 10.0f).has_value());
        CHECK_FALSE(w.probeWall({0, 0}, 0.0f, 1.0f, 1.0f).has_value());
        glm::vec2 p{1, 2}, v{3, 4};
        CHECK_FALSE(w.resolveWalls(p, 0.0f, 0.4f, 0.4f, v));
        CHECK(p == glm::vec2(1, 2));
        CHECK(v == glm::vec2(3, 4));
    }
    SECTION("a single zero-area block") {
        World w;
        Block b;
        b.min = b.max = {3.0f, 3.0f};
        b.h0 = b.h1 = 2.0f;
        w.add(b);
        Lcg r(4);
        compareRays(w, r, 500, 10.0f);
        comparePoints(w, r, 500, 10.0f);
    }
    SECTION("only giant blocks") {
        World w = randomWorld(12, 25, 100.0f, true, false);
        World giants;
        for (const Block& b : w.blocks()) {
            Block g = b;
            g.min -= glm::vec2(400.0f);
            g.max += glm::vec2(400.0f);
            giants.add(g);
        }
        Lcg r(6);
        compareRays(giants, r, 800, 200.0f);
        comparePoints(giants, r, 800, 200.0f);
        compareResolve(giants, r, 800, 200.0f);
    }
    SECTION("negative and enormous coordinates") {
        World w;
        for (int i = 0; i < 50; ++i) {
            Block b;
            b.min = {-50000.0f + static_cast<float>(i) * 2000.0f, -50000.0f + static_cast<float>((i * 7) % 50) * 2000.0f};
            b.max = b.min + glm::vec2(30.0f, 30.0f);
            b.h0 = b.h1 = 5.0f;
            w.add(b);
        }
        Lcg r(8);
        compareRays(w, r, 1500, 50000.0f);
        comparePoints(w, r, 1500, 50000.0f);
    }
}

TEST_CASE("A World that was copied or moved after its grid was built still answers correctly", "[broadphase][lifetime]") {
    World a = randomWorld(55, 300, 120.0f);
    Lcg r1(1);
    compareRays(a, r1, 200, 120.0f);        // build the grid

    World copy = a;
    Lcg r2(2);
    compareRays(copy, r2, 400, 120.0f);
    comparePoints(copy, r2, 400, 120.0f);

    World moved = std::move(copy);
    Lcg r3(3);
    compareRays(moved, r3, 400, 120.0f);
    compareResolve(moved, r3, 400, 120.0f);

    World assigned;
    assigned = std::move(moved);            // what a level switch does (LevelLoad assignment)
    Lcg r4(4);
    compareRays(assigned, r4, 400, 120.0f);

    // The same queries always give the same answers.
    const glm::vec3 o{-100.0f, 1.0f, -100.0f};
    const glm::vec3 d = glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f));
    const auto first = assigned.raycast(o, d, 500.0f);
    for (int i = 0; i < 20; ++i) REQUIRE(sameHit(first, assigned.raycast(o, d, 500.0f)));
}

TEST_CASE("The shipped levels give identical answers with the broadphase", "[broadphase][shipped]") {
    for (const char* id : {"level01_escape", "kodiak_hub", "kodiak_range"}) {
        INFO("level " << id);
        LevelLoad load = loadLevelFile(levelPath(CARNAGE_ASSET_DIR, id));
        REQUIRE(load.clean());
        World& w = load.level.world;
        Lcg r(static_cast<std::uint32_t>(std::string(id).size()) * 31u);
        compareRays(w, r, 4000, 60.0f);
        comparePoints(w, r, 3000, 60.0f);
        compareResolve(w, r, 3000, 60.0f);

        // ...with every door closed and every door open.
        for (const bool open : {false, true}) {
            for (const int door : w.doorIds()) w.setDoorOpen(door, open);
            compareRays(w, r, 1500, 60.0f);
            compareResolve(w, r, 1500, 60.0f);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// The point of it all: queries examine only nearby blocks
// ---------------------------------------------------------------------------------------------

TEST_CASE("Queries in a big world examine a small fraction of its blocks", "[broadphase][performance]") {
    // About 3000 blocks scattered over a 2 km x 2 km basin, plus one huge ground slab.
    World w = randomWorld(404, 3000, 1000.0f, /*giants=*/false);
    Block ground;
    ground.min = {-1200.0f, -1200.0f};
    ground.max = {1200.0f, 1200.0f};
    ground.h0 = ground.h1 = -0.5f;
    ground.base = -5.0f;
    w.add(ground);
    const std::size_t total = w.blocks().size();
    REQUIRE(total > 3000);

    Lcg r(3);
    SECTION("rays") {
        w.resetQueryStats();
        constexpr int kRays = 2000;
        for (int i = 0; i < kRays; ++i) {
            const glm::vec3 o{r.range(-900.0f, 900.0f), 1.5f, r.range(-900.0f, 900.0f)};
            const float a = r.range(0.0f, 6.2831853f);
            (void)w.raycast(o, {std::cos(a), 0.0f, std::sin(a)}, 60.0f);   // line-of-sight / weapon-sized rays
        }
        const auto s = w.queryStats();
        CHECK(s.queries == kRays);
        const double perRay = static_cast<double>(s.blocksTested) / kRays;
        INFO("blocks tested per 60 m ray: " << perRay << " of " << total);
        CHECK(perRay < 40.0);                       // a full scan would examine all ~3000
        CHECK(perRay < 0.02 * static_cast<double>(total));
    }
    SECTION("ground, ceiling, wall probes and collision") {
        w.resetQueryStats();
        constexpr int kEach = 1000;
        for (int i = 0; i < kEach; ++i) {
            const glm::vec2 p{r.range(-900.0f, 900.0f), r.range(-900.0f, 900.0f)};
            (void)w.ground(p, 5.0f);
            (void)w.ceiling(p, 0.0f, 4.0f);
            (void)w.probeWall(p, 0.0f, 0.75f, 2.5f);
            glm::vec2 q = p, v{1.0f, 0.0f};
            (void)w.resolveWalls(q, 0.0f, 0.4f, 0.4f, v);
        }
        const auto s = w.queryStats();
        const double per = static_cast<double>(s.blocksTested) / static_cast<double>(s.queries);
        INFO("blocks tested per query: " << per << " of " << total);
        CHECK(per < 10.0);
    }
    SECTION("a ray that runs far still stops at the first thing it hits") {
        World wall;
        Block floor;
        floor.min = {-1000.0f, -50.0f}; floor.max = {1000.0f, 50.0f};
        wall.add(floor);
        for (int i = 0; i < 100; ++i) {
            Block b;
            b.min = {-1000.0f + static_cast<float>(i) * 20.0f, -3.0f};
            b.max = {b.min.x + 1.0f, 3.0f};
            b.h0 = b.h1 = 4.0f;
            wall.add(b);
        }
        wall.resetQueryStats();
        const auto hit = wall.raycast({-990.0f, 1.0f, 0.0f}, {1, 0, 0}, 5000.0f);   // first wall is 10 m away
        REQUIRE(hit.has_value());
        CHECK(hit->t == Approx(10.0f));
        CHECK(wall.queryStats().blocksTested < 10);   // it did not walk the whole 2 km
    }
}
