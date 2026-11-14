#include "game/Effects.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {
constexpr float kTracerSpeed  = 220.0f;
constexpr float kTracerLength = 4.0f;
constexpr float kTracerLife   = 0.35f;
} // namespace

void Effects::onHit(const HitEvent& e, const World& world) {
    if (e.tracer) tracers_.push_back({e.from, e.pos, 0.0f});

    switch (e.kind) {
        case HitKind::Miss:
            break;

        case HitKind::World:
            burst(12, e.pos + e.normal * 0.02f, e.normal, 3.0f, 9.0f, 0.7f, 0.012f, 0.026f,
                  {1.0f, 0.75f, 0.25f}, 0.15f, 0.40f, 18.0f, 0.5f, 0.35f, true);
            burst(6, e.pos + e.normal * 0.02f, e.normal, 1.5f, 5.0f, 0.9f, 0.020f, 0.045f,
                  e.color, 0.5f, 1.0f, 16.0f, 0.3f, 0.3f, false);
            flashes_.push_back({e.pos, {1.0f, 0.85f, 0.5f}, 0.09f, 0.09f, 0.035f});
            // Gun hits leave a hole; tracer-less hits (fists, melee) leave a scuff.
            if (e.tracer || e.damage > 0.0f)
                decals_.add({e.pos, e.normal, rng_.range(0.022f, 0.035f),
                             rng_.range(0.0f, glm::two_pi<float>()),
                             e.tracer ? DecalKind::BulletHole : DecalKind::Slash});
            break;

        case HitKind::Body:
        case HitKind::Head: {
            const float scale = e.kind == HitKind::Head ? 1.5f : 1.0f;
            burst(static_cast<int>(14 * scale), e.pos, e.normal, 2.0f, 8.0f, 0.8f, 0.018f,
                  0.04f, {0.65f, 0.02f, 0.02f}, 0.35f, 0.9f, 14.0f, 0.6f, 0.15f, false, true);
            // Mist along the exit direction, plus spatter on whatever is behind.
            {
                const glm::vec3 d = e.pos - e.from;
                const float     l = glm::length(d);
                if (l > 1e-3f) {
                    const glm::vec3 dir = d / l;
                    burst(6, e.pos, dir, 3.0f, 9.0f, 0.35f, 0.012f, 0.03f,
                          {0.55f, 0.02f, 0.02f}, 0.3f, 0.7f, 10.0f, 0.8f, 0.1f, false, true);
                    if (const auto w = world.raycast(e.pos, dir, e.kind == HitKind::Head ? 4.0f : 3.0f))
                        decals_.add({e.pos + dir * w->t, w->normal, rng_.range(0.10f, 0.22f),
                                     rng_.range(0.0f, glm::two_pi<float>()), DecalKind::Blood});
                }
            }
            if (e.killed) {
                burst(30, e.pos, glm::vec3(0.0f, 1.0f, 0.0f), 3.0f, 10.0f, 1.0f, 0.02f, 0.05f,
                      {0.55f, 0.02f, 0.02f}, 0.5f, 1.2f, 14.0f, 0.5f, 0.2f, false, true);
            }
            flashes_.push_back({e.pos, e.kind == HitKind::Head ? glm::vec3(1.0f, 0.9f, 0.4f)
                                                                 : glm::vec3(1.0f, 0.3f, 0.2f),
                                0.10f, 0.10f, e.kind == HitKind::Head ? 0.07f : 0.045f});
            break;
        }

        case HitKind::Explosion:
            burst(26, e.pos, e.normal, 3.0f, 14.0f, 1.2f, 0.08f, 0.22f,
                  {1.0f, 0.45f, 0.08f}, 0.25f, 0.65f, -1.0f, 2.5f, 0.0f, true);   // fireball
            burst(40, e.pos, e.normal, 6.0f, 22.0f, 1.0f, 0.015f, 0.035f,
                  {1.0f, 0.85f, 0.35f}, 0.3f, 0.9f, 14.0f, 0.3f, 0.4f, true);     // sparks
            burst(16, e.pos, e.normal, 4.0f, 12.0f, 1.0f, 0.05f, 0.12f,
                  e.color, 0.8f, 1.6f, 14.0f, 0.4f, 0.3f, false);                 // debris
            burst(10, e.pos, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 3.5f, 0.6f, 0.18f, 0.35f,
                  {0.18f, 0.17f, 0.17f}, 0.9f, 1.8f, -0.8f, 1.8f, 0.0f, false);   // smoke
            flashes_.push_back({e.pos, {1.0f, 0.7f, 0.3f}, 0.25f, 0.25f, 0.45f});
            break;
    }
}

void Effects::onShot(const WeaponStats& s, const glm::vec3& port, const glm::vec3& right,
                     const glm::vec3& up, const glm::vec3& forward, const glm::vec3& vel) {
    if (s.casing == CasingKind::None) return;

    const bool shell = s.casing == CasingKind::Shell;
    Particle& p = particles_.spawn();
    p.pos = p.prevPos = port;
    p.vel = vel * 0.8f + right * rng_.range(2.0f, 3.8f) + up * rng_.range(1.8f, 3.2f) -
            forward * rng_.range(0.0f, 0.8f);
    p.size    = shell ? glm::vec3(0.012f, 0.012f, 0.035f) : glm::vec3(0.005f, 0.005f, 0.015f);
    p.color   = shell ? glm::vec3(0.75f, 0.12f, 0.08f) : glm::vec3(0.85f, 0.65f, 0.15f);
    p.material = shell ? Material::Plastic : Material::Brass;
    p.orient  = glm::angleAxis(rng_.range(0.0f, glm::two_pi<float>()), rng_.unitSphere());
    p.spin    = rng_.unitSphere() * rng_.range(14.0f, 30.0f);
    p.maxLife = p.life = rng_.range(3.5f, 5.0f);
    p.gravity = 16.0f;
    p.drag    = 0.1f;
    p.bounce  = 0.38f;
}

void Effects::update(float dt, const World& world) {
    particles_.update(dt, world, &decals_);

    for (Tracer& t : tracers_) t.age += dt;
    std::erase_if(tracers_, [](const Tracer& t) { return t.age > kTracerLife; });

    for (ScreenFlash& f : flashes_) f.life -= dt;
    std::erase_if(flashes_, [](const ScreenFlash& f) { return f.life <= 0.0f; });
}

void Effects::collectLights(float alpha, std::vector<PointLight>& out) const {
    (void)alpha;
    for (const ScreenFlash& f : flashes_) {
        const float t = std::clamp(f.life / f.maxLife, 0.0f, 1.0f);
        // size is the screen-flash scale: ~0.045 body hit, ~0.45 explosion.
        const float power  = 1.0f + f.size * 40.0f;
        const float radius = 3.0f + f.size * 30.0f;
        out.push_back({f.pos + glm::vec3(0.0f, 0.15f, 0.0f), f.color * power * t * t, radius});
    }
}

void Effects::collect(float alpha, std::vector<DrawBox>& out) const {
    particles_.collect(alpha, out);

    for (const Tracer& t : tracers_) {
        const glm::vec3 seg = t.to - t.from;
        const float     len = glm::length(seg);
        if (len < 1e-3f) continue;
        const glm::vec3 dir = seg / len;

        const float head = std::min(len, t.age * kTracerSpeed);
        const float tail = std::max(0.0f, head - kTracerLength);
        if (head - tail < 1e-3f) continue;
        const float fade = std::clamp(1.0f - t.age / kTracerLife, 0.0f, 1.0f);

        const glm::vec3 a = t.from + dir * tail;
        const glm::vec3 b = t.from + dir * head;
        out.push_back({(a + b) * 0.5f, glm::vec3(0.006f * fade + 0.002f, 0.006f * fade + 0.002f,
                                                 (head - tail) * 0.5f),
                       basisFromDirection(dir), glm::vec3(1.0f, 0.9f, 0.55f), true,
                                              Material::Emissive});
    }
}

} // namespace carnage
