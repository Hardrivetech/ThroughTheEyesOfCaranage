#pragma once
#include "game/World.hpp"

#include <glm/glm.hpp>

#include <cmath>

namespace carnage {

// Places a piece of level (a POI) into a bigger one: turn it by a whole number of quarter
// turns about the vertical axis, then move it by `offset`. Blocks are axis-aligned boxes,
// so only quarter turns keep them as blocks; the maths is integer-exact (no trigonometry),
// which is what lets tests demand exact answers.
//
// Orientation follows the engine's yaw convention: turning by +90 degrees (one quarter
// turn) makes what faced -Z face -X, and what was on the right (+X) end up facing -Z.
// In (x, z): (x, z) -> (z, -x).
struct Placement {
    glm::vec3 offset{0.0f};
    int       turns = 0;  // quarter turns; any integer, normalised to 0..3 by quarter()

    int quarter() const { return ((turns % 4) + 4) % 4; }
    float yawRadians() const { return static_cast<float>(quarter()) * 1.5707963267948966f; }

    // A point in the piece's own coordinates -> the parent's.
    glm::vec2 turn(glm::vec2 p) const {
        switch (quarter()) {
            case 1:  return { p.y, -p.x};
            case 2:  return {-p.x, -p.y};
            case 3:  return {-p.y,  p.x};
            default: return p;
        }
    }
    glm::vec3 point(const glm::vec3& p) const {
        const glm::vec2 t = turn({p.x, p.z});
        return {t.x + offset.x, p.y + offset.y, t.y + offset.z};
    }
    // The inverse: a parent-space point back into the piece's own coordinates.
    glm::vec3 unplace(const glm::vec3& p) const {
        const glm::vec2 d{p.x - offset.x, p.z - offset.z};
        glm::vec2 l;
        switch (quarter()) {
            case 1:  l = {-d.y,  d.x}; break;
            case 2:  l = {-d.x, -d.y}; break;
            case 3:  l = { d.y, -d.x}; break;
            default: l = d; break;
        }
        return {l.x, p.y - offset.y, l.y};
    }
    // A direction (no offset).
    glm::vec3 direction(const glm::vec3& d) const {
        const glm::vec2 t = turn({d.x, d.z});
        return {t.x, d.y, t.y};
    }
    // An axis-aligned box [lo, hi] in the piece's coordinates -> the parent's box.
    void box(const glm::vec3& lo, const glm::vec3& hi, glm::vec3& outLo, glm::vec3& outHi) const {
        const glm::vec3 a = point(lo), b = point(hi);
        outLo = glm::min(a, b);
        outHi = glm::max(a, b);
    }

    // The block placed in the parent. A ramp whose axis turns end-for-end swaps its
    // two heights so the slope still rises the same way relative to the piece.
    Block block(const Block& b) const {
        Block r = b;
        const glm::vec2 a = turn(b.min), c = turn(b.max);
        r.min = glm::min(a, c) + glm::vec2(offset.x, offset.z);
        r.max = glm::max(a, c) + glm::vec2(offset.x, offset.z);
        r.h0   = b.h0 + offset.y;
        r.h1   = b.h1 + offset.y;
        r.base = b.base + offset.y;

        const glm::vec2 along = turn(b.axis == 0 ? glm::vec2(1.0f, 0.0f) : glm::vec2(0.0f, 1.0f));
        r.axis = along.x != 0.0f ? 0 : 1;
        if (along.x + along.y < 0.0f) std::swap(r.h0, r.h1);
        return r;
    }

    // Composition: `inner` is placed in a piece that is itself placed by `*this`.
    Placement compose(const Placement& inner) const {
        Placement p;
        p.turns  = quarter() + inner.quarter();
        p.offset = point(inner.offset);
        return p;
    }
};

} // namespace carnage
