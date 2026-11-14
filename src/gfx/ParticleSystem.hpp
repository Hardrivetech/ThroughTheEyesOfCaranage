#pragma once
#include "core/DrawTypes.hpp"
#include "core/Random.hpp"
#include "game/World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace carnage {

struct Particle {
    glm::vec3 pos{0.0f}, prevPos{0.0f}, vel{0.0f};
    glm::vec3 size{0.02f};            // half-extents
    glm::vec3 color{1.0f};
    glm::quat orient{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 spin{0.0f};             // angular velocity, axis * rad/s
    float life = 0.0f, maxLife = 1.0f;
    float gravity = 0.0f, drag = 0.0f, bounce = 0.0f;
    bool  emissive = false;
    Material material = Material::Plastic;
    bool  fade     = true;            // shrink over the last part of its life
    bool  blood    = false;           // leaves a floor splat on first ground contact
};

// Static surface marks. Drawn as thin boxes lying on the surface (no new
// shader, no per-frame allocation); a fixed ring caps the cost, and the oldest
// mark is overwritten when full.
enum class DecalKind : std::uint8_t { BulletHole, Slash, Blood };

struct Decal {
    glm::vec3 pos{0.0f};      // on the surface
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float     size  = 0.05f;  // half-extent
    float     angle = 0.0f;   // rotation about the normal
    DecalKind kind  = DecalKind::BulletHole;
};

class DecalSystem {
public:
    static constexpr std::size_t kMax = 384;

    void add(const Decal& d) {
        if (decals_.size() < kMax) { decals_.push_back(d); return; }
        decals_[next_] = d;
        next_ = (next_ + 1) % kMax;
    }
    void clear() { decals_.clear(); next_ = 0; }
    std::size_t size() const { return decals_.size(); }
    const std::vector<Decal>& decals() const { return decals_; }

    // Distance-culled so far-away marks cost nothing.
    void collect(std::vector<DrawBox>& out, const glm::vec3& eye, float maxDist = 45.0f) const {
        constexpr float kDepth = 0.004f;
        const float     max2   = maxDist * maxDist;
        for (const Decal& d : decals_) {
            const glm::vec3 r = d.pos - eye;
            if (glm::dot(r, r) > max2) continue;

            const glm::mat3 b = basisFromDirection(d.normal);
            const auto frame = [&](float a) {
                const float c = std::cos(a), s = std::sin(a);
                return glm::mat3(c * b[0] + s * b[1], -s * b[0] + c * b[1], b[2]);
            };
            const glm::vec3 centre = d.pos + d.normal * kDepth;
            const auto push = [&](const glm::mat3& m, glm::vec2 halfXY, glm::vec3 offset,
                                  glm::vec3 col, Material mat) {
                out.push_back({centre + m * offset, {halfXY.x, halfXY.y, kDepth}, m, col, false, mat});
            };

            switch (d.kind) {
                case DecalKind::BulletHole: {  // two squares at 45 degrees = an octagon
                    const glm::vec3 col{0.025f, 0.025f, 0.028f};
                    push(frame(d.angle), {d.size, d.size}, {}, col, Material::Concrete);
                    push(frame(d.angle + glm::quarter_pi<float>()), {d.size, d.size},
                         {0.0f, 0.0f, 0.0005f}, col, Material::Concrete);
                    break;
                }
                case DecalKind::Slash: {
                    const glm::mat3 m = frame(d.angle);
                    const glm::vec3 col{0.04f, 0.04f, 0.045f};
                    push(m, {d.size * 3.0f, 0.010f}, {}, col, Material::Concrete);
                    push(m, {d.size * 2.2f, 0.008f}, {0.0f, 0.035f, 0.0f}, col, Material::Concrete);
                    break;
                }
                case DecalKind::Blood: {
                    const glm::vec3 col{0.22f, 0.012f, 0.012f};
                    push(frame(d.angle), {d.size, d.size * 0.7f}, {}, col, Material::Flesh);
                    push(frame(d.angle + 1.1f), {d.size * 0.7f, d.size * 0.5f},
                         {d.size * 0.5f, d.size * 0.3f, 0.0005f}, col * 0.9f, Material::Flesh);
                    push(frame(d.angle - 0.8f), {d.size * 0.4f, d.size * 0.4f},
                         {-d.size * 0.6f, d.size * 0.2f, 0.001f}, col * 1.1f, Material::Flesh);
                    break;
                }
            }
        }
    }

private:
    std::vector<Decal> decals_;
    std::size_t        next_ = 0;
};

// Pooled cosmetic particles: blood bursts, sparks, debris, casings, smoke.
// Fixed capacity, O(1) spawn (round-robin overwrite when full) and swap-remove
// on death, so a firefight never allocates or shifts memory. Uses its own Rng
// so cosmetics never perturb gameplay randomness.
class ParticleSystem {
public:
    static constexpr std::size_t kMax = 3000;

    explicit ParticleSystem(std::uint32_t seed = 0xC0FFEEu) : rng_(seed) { pool_.reserve(kMax); }

    Particle& spawn() {
        if (pool_.size() < kMax) return pool_.emplace_back();
        Particle& p = pool_[next_];
        next_ = (next_ + 1) % kMax;
        p = Particle{};
        return p;
    }

    void burst(int count, const glm::vec3& pos, const glm::vec3& normal, float speedMin,
               float speedMax, float spread, float sizeMin, float sizeMax,
               const glm::vec3& color, float lifeMin, float lifeMax, float gravity,
               float drag, float bounce, bool emissive, bool blood = false) {
        for (int i = 0; i < count; ++i) {
            Particle& p = spawn();
            const glm::vec3 dir = glm::normalize(normal + rng_.unitSphere() * spread);
            p.pos = p.prevPos = pos;
            p.vel     = dir * rng_.range(speedMin, speedMax);
            p.size    = glm::vec3(rng_.range(sizeMin, sizeMax));
            p.color   = color * rng_.range(0.7f, 1.1f);
            p.maxLife = p.life = rng_.range(lifeMin, lifeMax);
            p.gravity = gravity;
            p.drag    = drag;
            p.bounce  = bounce;
            p.emissive = emissive;
            p.blood   = blood;
            p.spin    = rng_.unitSphere() * rng_.range(2.0f, 14.0f);
        }
    }

    // Blood drops that touch the floor stamp a splat decal (probabilistic, so
    // a burst doesn't flood the ring).
    void update(float dt, const World& world, DecalSystem* decals = nullptr) {
        for (Particle& p : pool_) {
            p.prevPos = p.pos;
            p.life   -= dt;

            p.vel.y -= p.gravity * dt;
            p.vel   *= std::exp(-p.drag * dt);
            p.pos   += p.vel * dt;

            const float radius = std::max({p.size.x, p.size.y, p.size.z}) * 0.6f;
            if (p.gravity > 0.0f) {
                const auto g = world.ground({p.pos.x, p.pos.z}, p.prevPos.y + radius + 0.3f);
                if (g && p.pos.y - radius < g->height) {
                    p.pos.y = g->height + radius;
                    if (p.blood) {
                        if (decals && rng_.uniform() < 0.35f)
                            decals->add({{p.pos.x, g->height + 0.002f, p.pos.z}, g->normal,
                                         rng_.range(0.05f, 0.14f),
                                         rng_.range(0.0f, glm::two_pi<float>()), DecalKind::Blood});
                        p.blood = false;
                        p.life  = std::min(p.life, 0.15f);  // the drop becomes the splat
                    }
                    if (std::abs(p.vel.y) > 0.8f) {
                        p.vel.y = -p.vel.y * p.bounce;
                        p.vel.x *= 0.7f;
                        p.vel.z *= 0.7f;
                        p.spin  *= 0.6f;
                    } else {
                        p.vel  = glm::vec3(0.0f);
                        p.spin = glm::vec3(0.0f);
                    }
                }
            }

            const float w = glm::length(p.spin);
            if (w > 1e-4f)
                p.orient = glm::normalize(glm::angleAxis(w * dt, p.spin / w) * p.orient);
        }
        // Swap-remove: order is irrelevant for cosmetics.
        for (std::size_t i = 0; i < pool_.size();) {
            if (pool_[i].life <= 0.0f) { pool_[i] = pool_.back(); pool_.pop_back(); }
            else ++i;
        }
        if (next_ >= pool_.size()) next_ = 0;
    }

    void collect(float alpha, std::vector<DrawBox>& out) const {
        for (const Particle& p : pool_) {
            const float remaining = p.life / p.maxLife;
            const float shrink    = p.fade ? std::clamp(remaining * 4.0f, 0.0f, 1.0f) : 1.0f;
            out.push_back({glm::mix(p.prevPos, p.pos, alpha), p.size * shrink,
                           glm::mat3_cast(p.orient), p.color, p.emissive,
                           p.emissive ? Material::Emissive : p.material});
        }
    }

    std::size_t size() const { return pool_.size(); }
    Rng&        rng() { return rng_; }

private:
    Rng                   rng_;
    std::vector<Particle> pool_;
    std::size_t           next_ = 0;
};

} // namespace carnage
