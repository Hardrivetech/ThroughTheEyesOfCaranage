#pragma once
#include "game/World.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <initializer_list>
#include <span>
#include <vector>

namespace carnage {

// Static checks on a level's geometry that catch rendering problems a player would see but
// the game logic never would. Pure functions over the blocks; nothing here touches the GPU.

// Two visible blocks whose top faces lie in the same plane over an area (a road slab laid
// flush on a floor, a pad on a foundation). The GPU cannot decide which one is in front, so
// the two flicker and tear (z-fighting). Fix by lifting the overlay a few centimetres or by
// making the footprints not overlap.
struct CoplanarTops {
    std::size_t a = 0, b = 0;      // indices into the block list, a < b
    glm::vec2   min{0.0f}, max{0.0f};  // the overlapping rectangle on the XZ plane
    float       height = 0.0f;     // the shared height (at the centre of the overlap)
};

// Reports every pair of visible blocks (and doors, which are drawn as boxes) whose top
// surfaces coincide to within `eps` metres across a shared area of at least `minOverlap`
// metres in both directions. Touching edges are not overlaps. Sloped tops count too.
std::vector<CoplanarTops> findCoplanarTops(std::span<const Block> blocks, float eps = 0.005f,
                                           float minOverlap = 0.01f);

// A spot on a floor that snow would settle on (its material accumulates snow) although it is
// under a roof, i.e. an interior floor in a snowy level.
struct SnowyInterior {
    std::size_t block = 0;
    glm::vec2   at{0.0f};
    float       roofBase = 0.0f;   // underside of the roof above it
};

// Samples the top of every block whose material accumulates snow on a grid of about one metre.
// A sample counts only where that block is the highest surface (a floor you could stand on),
// and it is "indoors" when another block's underside is between 1 m and `maxCeiling` metres
// above it. Doors count as solid. Tall overhangs (a tower platform, a crane beam) are higher
// than `maxCeiling` and are not roofs, and neither is anything narrower than `minRoofSpan`
// in either direction (a gate lintel, a beam): a roof is a slab you could put a room under.
std::vector<SnowyInterior> findSnowyInteriors(std::span<const Block> blocks, float maxCeiling = 8.0f,
                                              float minRoofSpan = 2.0f);

// Convenience overloads for a literal list of blocks.
inline std::vector<CoplanarTops> findCoplanarTops(std::initializer_list<Block> blocks, float eps = 0.005f,
                                                  float minOverlap = 0.01f) {
    return findCoplanarTops(std::span<const Block>(blocks.begin(), blocks.size()), eps, minOverlap);
}
inline std::vector<SnowyInterior> findSnowyInteriors(std::initializer_list<Block> blocks, float maxCeiling = 8.0f,
                                                     float minRoofSpan = 2.0f) {
    return findSnowyInteriors(std::span<const Block>(blocks.begin(), blocks.size()), maxCeiling, minRoofSpan);
}

} // namespace carnage
