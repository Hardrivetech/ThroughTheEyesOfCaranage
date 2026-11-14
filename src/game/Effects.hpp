#pragma once
#include "core/DrawTypes.hpp"
#include "core/Random.hpp"
#include "game/Combat.hpp"
#include "game/Weapon.hpp"
#include "gfx/ParticleSystem.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace carnage {

struct Tracer {
    glm::vec3 from{0.0f}, to{0.0f};
    float age = 0.0f;
};

// Brief world-anchored flash, rendered in screen space by the HUD.
struct ScreenFlash {
    glm::vec3 pos{0.0f};
    glm::vec3 color{1.0f};
    float life = 0.0f, maxLife = 0.12f;
    float size = 0.08f;               // NDC half-height at full strength
};

// Cosmetic only: uses its own Rng so it never perturbs gameplay randomness.
class Effects {
public:
    explicit Effects(std::uint32_t seed = 0xC0FFEEu) : particles_(seed), rng_(seed ^ 0x5BD1E995u) {}

    // `world` is used to project blood spatter and decals onto nearby surfaces.
    void onHit(const HitEvent& e, const World& world);
    // Surface mark from a melee swing at a wall (slash) without a HitEvent.
    void addDecal(const Decal& d) { decals_.add(d); }
    void onShot(const WeaponStats& stats, const glm::vec3& port, const glm::vec3& right,
                const glm::vec3& up, const glm::vec3& forward, const glm::vec3& playerVel);

    void update(float dt, const World& world);
    void collect(float alpha, std::vector<DrawBox>& out) const;
    void collectDecals(const glm::vec3& eye, std::vector<DrawBox>& out) const { decals_.collect(out, eye); }
    std::size_t decalCount() const { return decals_.size(); }
    void clearDecals() { decals_.clear(); }

    const std::vector<ScreenFlash>& flashes() const { return flashes_; }

    // Short-lived world lights (hits, explosions, sparks), already faded.
    void collectLights(float alpha, std::vector<PointLight>& out) const;
    std::size_t particleCount() const { return particles_.size(); }

private:
    void burst(int count, const glm::vec3& pos, const glm::vec3& normal, float speedMin,
               float speedMax, float spread, float sizeMin, float sizeMax,
               const glm::vec3& color, float lifeMin, float lifeMax, float gravity,
               float drag, float bounce, bool emissive, bool blood = false) {
        particles_.burst(count, pos, normal, speedMin, speedMax, spread, sizeMin, sizeMax, color,
                         lifeMin, lifeMax, gravity, drag, bounce, emissive, blood);
    }

    ParticleSystem           particles_;
    DecalSystem              decals_;
    Rng                      rng_;
    std::vector<Tracer>      tracers_;
    std::vector<ScreenFlash> flashes_;
};

} // namespace carnage
