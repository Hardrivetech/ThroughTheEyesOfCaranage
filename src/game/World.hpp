#pragma once
#include "core/Material.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace carnage {

// A solid column on the XZ plane whose top surface is flat or linearly
// sloped along one axis (axis 0 = X, 1 = Z). Boxes, walls and ramps are all
// Blocks: h0 is the top height at min[axis], h1 the top height at max[axis].
struct Block {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    float     h0 = 0.0f;
    float     h1 = 0.0f;
    int       axis = 0;
    glm::vec3 color{0.6f};
    bool      checker = false;  // legacy flag, unused by the PBR renderer
    Material  material = Material::Concrete;
    bool      visible  = true;  // false: collision only, a prop mesh is drawn instead
    float     base     = -0.2f; // underside/base height of the block
    bool      enabled  = true;  // false: ignored by every query (an opened door)
    int       door     = -1;    // >= 0: dynamic door id (invisible to the static upload)

    float     surface(glm::vec2 p) const;
    bool      contains(glm::vec2 p) const;
    glm::vec3 normal() const;  // up-facing surface normal
    float     topHeight() const { return std::max(h0, h1); }

    // Ray vs this block (a convex prism). Returns the entry distance and
    // surface normal; rays starting inside the block report no hit.
    bool raycast(const glm::vec3& origin, const glm::vec3& dir, float maxT,
                 float& tOut, glm::vec3& nOut) const;
};

struct GroundHit {
    float     height;
    glm::vec3 normal;
};

struct WallHit {
    glm::vec2 normal;    // horizontal, pointing away from the wall
    float     distance;  // from the query point to the wall
};

struct WorldHit {
    float        t;
    glm::vec3    normal;
    const Block* block;
};

struct WorldLight {
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
    float     radius = 12.0f;
};

// Static geometry plus the queries the game runs every tick (ground, ceiling, walls,
// rays). Queries go through a uniform 2D grid over the blocks' XZ footprints (a
// broadphase), so they only examine blocks near the query instead of every block.
// The grid only decides WHICH blocks are examined, always in ascending index order
// with the original per-block logic, so results (including tie-breaks) are identical
// to scanning every block. It is built lazily after blocks are added and does not
// depend on a block's `enabled` flag, so opening and closing doors never touches it.
//
// Thread safety: queries use scratch state inside the World, so they must not run
// concurrently with each other (the game runs them all on the simulation thread).
class World {
public:
    void add(const Block& b) { blocks_.push_back(b); gridDirty_ = true; }
    void addLight(const WorldLight& l) { lights_.push_back(l); }
    std::span<const Block> blocks() const { return blocks_; }
    std::span<const WorldLight> lights() const { return lights_; }

    // Doors are Blocks sharing a `door` id. Closed = enabled (solid).
    void setDoorOpen(int id, bool open);
    void resetDoors() { for (Block& b : blocks_) if (b.door >= 0) b.enabled = true; }
    // Every door id in this world, ascending and without duplicates.
    std::vector<int> doorIds() const;
    bool hasDoor(int id) const;

    // Highest surface under p that is <= maxSurface.
    std::optional<GroundHit> ground(glm::vec2 p, float maxSurface) const;

    // Lowest ceiling surface above feetY that is <= maxHeadY.
    std::optional<float> ceiling(glm::vec2 p, float feetY, float maxHeadY) const;

    // Nearest wall within `reach` of p whose top is at least `minHeight`
    // above feetY.
    std::optional<WallHit> probeWall(glm::vec2 p, float feetY, float reach,
                                     float minHeight) const;

    // Nearest block hit along a ray (dir must be normalised).
    std::optional<WorldHit> raycast(const glm::vec3& origin, const glm::vec3& dir,
                                    float maxT) const;

    // Pushes a circle out of every block taller than feetY + step and removes
    // the velocity component driving into it. Returns true on any contact.
    bool resolveWalls(glm::vec2& p, float feetY, float radius, float step,
                      glm::vec2& vel) const;

    // How much work the broadphase is doing (for tests and the profiler).
    struct QueryStats {
        std::uint64_t queries     = 0;  // queries answered
        std::uint64_t blocksTested = 0;  // blocks actually examined across them
    };
    QueryStats queryStats() const { return stats_; }
    void       resetQueryStats() const { stats_ = {}; }

private:
    struct Grid {
        glm::vec2 lo{0.0f}, hi{0.0f};   // bounds of every block footprint
        float     cell = 8.0f, inv = 0.125f;
        int       nx = 0, nz = 0;
        std::vector<std::uint32_t> offsets;  // nx*nz + 1 (CSR)
        std::vector<std::uint32_t> items;    // block indices per cell, ascending within a cell
        std::vector<std::uint32_t> large;    // blocks covering too many cells: always examined

        int cx(float x) const;
        int cz(float z) const;
    };

    void ensureGrid() const { if (gridDirty_) buildGrid(); }
    void buildGrid() const;
    // Fills cand_ with the indices (ascending, unique) of every block whose footprint
    // may overlap the XZ box [lo, hi].
    void gather(glm::vec2 lo, glm::vec2 hi) const;

    std::vector<Block> blocks_;
    std::vector<WorldLight> lights_;

    mutable Grid                       grid_;
    mutable bool                       gridDirty_ = true;
    mutable std::vector<std::uint32_t> cand_;   // scratch: gather() output
    mutable std::vector<std::uint32_t> seen_;   // scratch: per-ray de-duplication
    mutable std::uint32_t              epoch_ = 0;
    mutable QueryStats                 stats_;
};

} // namespace carnage
