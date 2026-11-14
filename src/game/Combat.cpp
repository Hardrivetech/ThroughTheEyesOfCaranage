#include "game/Combat.hpp"

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

const glm::vec3 kBlood{0.55f, 0.02f, 0.02f};

// Slab test. Rays starting inside the box report t = 0 so point-blank
// shots and fast projectiles still register.
bool rayAabb(const glm::vec3& o, const glm::vec3& d, const glm::vec3& bmin,
             const glm::vec3& bmax, float maxT, float& tOut, glm::vec3& nOut) {
    float     tMin = 0.0f;
    float     tMax = maxT;
    glm::vec3 n{0.0f};
    bool      entered = false;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-8f) {
            if (o[i] < bmin[i] || o[i] > bmax[i]) return false;
            continue;
        }
        const float inv = 1.0f / d[i];
        float t1 = (bmin[i] - o[i]) * inv;
        float t2 = (bmax[i] - o[i]) * inv;
        float sign = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); sign = 1.0f; }
        if (t1 > tMin) {
            tMin    = t1;
            n       = glm::vec3(0.0f);
            n[i]    = sign;
            entered = true;
        }
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return false;
    }
    if (!entered) {
        tOut = 0.0f;
        nOut = -d;
    } else {
        tOut = tMin;
        nOut = n;
    }
    return true;
}

} // namespace

Target Target::dummy(glm::vec3 feet, float amp) {
    Target t;
    t.center       = feet + glm::vec3(0.0f, t.half.y, 0.0f);
    t.prevCenter   = t.center;
    t.patrolOrigin = t.center;
    t.patrolAmp    = amp;
    return t;
}

bool Combat::applyDamage(Target& t, float damage) {
    if (!t.alive()) return false;
    t.hp -= damage;
    t.flash = 1.0f;
    if (t.hp <= 0.0f) {
        t.hp           = 0.0f;
        t.respawnTimer = 3.0f;
        return true;
    }
    return false;
}

void Combat::registerTargetHit(Target& t, const glm::vec3& point, const glm::vec3& normal,
                               float damage, bool head, bool tracer, const glm::vec3& from) {
    HitEvent e;
    e.kind   = head ? HitKind::Head : HitKind::Body;
    e.pos    = point;
    e.normal = normal;
    e.from   = from;
    e.color  = kBlood;
    e.tracer = tracer;
    e.damage = damage;
    e.killed = applyDamage(t, damage);
    events_.push_back(e);
}

void Combat::hitscan(const glm::vec3& o, const glm::vec3& d, const glm::vec3& muzzle,
                     float range, float damage, bool tracer) {
    float     bestT = range;
    HitKind   kind  = HitKind::Miss;
    glm::vec3 n{0.0f, 1.0f, 0.0f};
    glm::vec3 color{0.5f};
    Target*   target = nullptr;

    if (const auto w = world_.raycast(o, d, range)) {
        bestT = w->t;
        kind  = HitKind::World;
        n     = w->normal;
        color = w->block->color;
    }
    for (Target& t : targets_) {
        if (!t.alive()) continue;
        float     tt = 0.0f;
        glm::vec3 nn{0.0f};
        if (rayAabb(o, d, t.min(), t.max(), bestT, tt, nn)) {
            bestT  = tt;
            n      = nn;
            target = &t;
        }
    }

    const glm::vec3 point = o + d * bestT;
    if (target) {
        const bool head = point.y >= target->headBottom();
        registerTargetHit(*target, point, n, damage * (head ? 2.0f : 1.0f), head, tracer, muzzle);
        return;
    }

    HitEvent e;
    e.kind   = kind;
    e.pos    = point;
    e.normal = n;
    e.from   = muzzle;
    e.color  = color;
    e.tracer = tracer;
    e.damage = damage;
    events_.push_back(e);
}

int Combat::strike(const glm::vec3& c, float radius, float damage, const glm::vec3& dir) {
    int hits = 0;
    for (Target& t : targets_) {
        if (!t.alive()) continue;
        const glm::vec3 closest = glm::clamp(c, t.min(), t.max());
        if (glm::length(closest - c) > radius) continue;
        const bool head = closest.y >= t.headBottom();
        registerTargetHit(t, closest, -dir, damage * (head ? 1.5f : 1.0f), head, false, c);
        ++hits;
    }
    return hits;
}

void Combat::detonate(const glm::vec3& pos, float radius, float damage, float knockback) {
    explode(pos, {0.0f, 1.0f, 0.0f}, radius, damage, knockback);
}

std::optional<glm::vec3> Combat::seize(const glm::vec3& c, float radius) {
    Target* best = nullptr;
    float   bestD = radius;
    for (Target& t : targets_) {
        if (!t.isCorpse()) continue;  // live enemies are never grabbable
        const glm::vec3 cc = t.corpseCenter(), h = t.corpseHalf();
        const float d = glm::length(glm::clamp(c, cc - h, cc + h) - c);
        if (d < bestD) { bestD = d; best = &t; }
    }
    if (!best) return std::nullopt;
    const glm::vec3 feet = best->center - glm::vec3(0.0f, best->half.y, 0.0f);
    best->claimed = true;
    if (best->respawns) best->respawnTimer = std::max(best->respawnTimer, 6.0f);
    return feet;
}

bool Combat::canSeize(const glm::vec3& c, float radius) const {
    for (const Target& t : targets_) {
        if (!t.isCorpse()) continue;
        const glm::vec3 cc = t.corpseCenter(), h = t.corpseHalf();
        if (glm::length(glm::clamp(c, cc - h, cc + h) - c) < radius) return true;
    }
    return false;
}

void Combat::spawn(const Projectile& proto, const glm::vec3& origin) {
    Projectile p = proto;
    p.prevPos    = p.pos;
    if (advance(p, origin, p.pos)) return;  // detonated inside the muzzle gap
    projectiles_.push_back(p);
}

bool Combat::advance(Projectile& p, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 seg = to - from;
    const float     len = glm::length(seg);
    if (len < 1e-6f) return false;
    const glm::vec3 dir = seg / len;

    float     bestT = len;
    glm::vec3 n{0.0f, 1.0f, 0.0f};
    glm::vec3 color{0.5f};
    Target*   target = nullptr;
    bool      hit    = false;

    if (const auto w = world_.raycast(from, dir, len)) {
        bestT = w->t;
        n     = w->normal;
        color = w->block->color;
        hit   = true;
    }
    const glm::vec3 inflate(p.radius);
    for (Target& t : targets_) {
        if (!t.alive()) continue;
        float     tt = 0.0f;
        glm::vec3 nn{0.0f};
        if (rayAabb(from, dir, t.min() - inflate, t.max() + inflate, bestT, tt, nn)) {
            bestT  = tt;
            n      = nn;
            target = &t;
            hit    = true;
        }
    }
    if (!hit) return false;

    const glm::vec3 point = from + dir * bestT;
    if (p.blastRadius > 0.0f) {
        explode(point + n * 0.1f, n, p.blastRadius, p.blastDamage, p.knockback);
    } else if (target) {
        const bool head = point.y >= target->headBottom();
        registerTargetHit(*target, point, n, p.damage * (head ? 2.0f : 1.0f), head, false, from);
    } else {
        HitEvent e;
        e.kind   = HitKind::World;
        e.pos    = point;
        e.normal = n;
        e.color  = color;
        e.damage = p.damage;
        events_.push_back(e);
    }
    return true;
}

void Combat::explode(const glm::vec3& pos, const glm::vec3& normal, float radius,
                     float damage, float knockback) {
    // No line-of-sight check: blasts go through thin cover.
    for (Target& t : targets_) {
        if (!t.alive()) continue;
        const glm::vec3 closest = glm::clamp(pos, t.min(), t.max());
        const float     d       = glm::length(closest - pos);
        if (d >= radius) continue;

        const float dmg = damage * (1.0f - d / radius);
        HitEvent e;
        e.kind   = HitKind::Body;
        e.pos    = closest;
        e.normal = glm::normalize(closest - pos + glm::vec3(0.0f, 1e-3f, 0.0f));
        e.color  = kBlood;
        e.damage = dmg;
        e.killed = applyDamage(t, dmg);
        events_.push_back(e);
    }

    HitEvent e;
    e.kind      = HitKind::Explosion;
    e.pos       = pos;
    e.normal    = normal;
    e.color     = glm::vec3(0.35f);
    e.damage    = damage;
    e.radius    = radius;
    e.knockback = knockback;
    events_.push_back(e);
}

void Combat::update(float dt) {
    for (Target& t : targets_) {
        t.prevCenter = t.center;
        if (t.patrolAmp > 0.0f) {
            t.patrolPhase += t.patrolSpeed * dt;
            t.center = t.patrolOrigin + t.patrolAxis * (std::sin(t.patrolPhase) * t.patrolAmp);
        }
        t.flash = std::max(0.0f, t.flash - dt * 5.0f);
        if (!t.alive() && t.respawns) {
            t.respawnTimer -= dt;
            if (t.respawnTimer <= 0.0f) {
                t.hp         = t.maxHp;
                t.claimed    = false;
                t.prevCenter = t.center;
            }
        }
    }

    for (Projectile& p : projectiles_) {
        p.prevPos = p.pos;
        p.vel.y  -= p.gravity * dt;
        p.age    += dt;

        const glm::vec3 next = p.pos + p.vel * dt;
        if (advance(p, p.pos, next)) {
            p.alive = false;
            continue;
        }
        p.pos = next;

        if (p.age >= p.lifetime || p.pos.y < -60.0f) {
            if (p.blastRadius > 0.0f && p.pos.y >= -60.0f)
                explode(p.pos, glm::vec3(0.0f, 1.0f, 0.0f), p.blastRadius, p.blastDamage, p.knockback);
            p.alive = false;
        }
    }
    std::erase_if(projectiles_, [](const Projectile& p) { return !p.alive; });
}

void Combat::collect(float alpha, std::vector<DrawBox>& out) const {
    const glm::mat3 identity(1.0f);

    for (const Target& t : targets_) {
        if (t.isCorpse()) {
            out.push_back({t.corpseCenter(), t.corpseHalf(), identity,
                           glm::vec3(0.30f, 0.10f, 0.08f), false, Material::Flesh});
            continue;
        }
        if (!t.alive()) continue;
        const glm::vec3 c = glm::mix(t.prevCenter, t.center, alpha);

        const float fullH = 2.0f * t.half.y;
        const float headH = fullH * t.headFraction;
        const float bodyH = fullH - headH;

        const float     health = t.hp / t.maxHp;
        const glm::vec3 base   = t.color * (0.35f + 0.65f * health);
        const glm::vec3 col    = glm::mix(base, glm::vec3(1.0f), t.flash * 0.85f);

        out.push_back({glm::vec3(c.x, c.y - t.half.y + bodyH * 0.5f, c.z),
                       glm::vec3(t.half.x, bodyH * 0.5f, t.half.z), identity, col, false,
                                              Material::Flesh});
        out.push_back({glm::vec3(c.x, c.y + t.half.y - headH * 0.5f, c.z),
                       glm::vec3(t.half.x * 0.7f, headH * 0.5f, t.half.z * 0.8f), identity,
                       glm::mix(col * 1.25f, glm::vec3(1.0f), 0.1f), false, Material::Flesh});
    }

    for (const Projectile& p : projectiles_) {
        const float speed = glm::length(p.vel);
        if (speed < 1e-3f) continue;
        const glm::vec3 dir = p.vel / speed;
        const glm::vec3 c   = glm::mix(p.prevPos, p.pos, alpha);
        const glm::mat3 ax  = basisFromDirection(dir);
        out.push_back({c, glm::vec3(0.06f, 0.06f, 0.24f), ax, glm::vec3(0.45f, 0.47f, 0.42f), false,
                               Material::Steel});
        out.push_back({c - dir * 0.26f, glm::vec3(0.07f, 0.07f, 0.07f), ax,
                       glm::vec3(1.0f, 0.55f, 0.1f), true, Material::Emissive});
    }
}

void Combat::collectLights(float alpha, std::vector<PointLight>& out) const {
    for (const Projectile& p : projectiles_)
        out.push_back({glm::mix(p.prevPos, p.pos, alpha), glm::vec3(6.0f, 2.6f, 0.6f), 9.0f});
}

glm::vec3 blastImpulse(const HitEvent& e, const glm::vec3& c) {
    if (e.kind != HitKind::Explosion || e.radius <= 0.0f) return glm::vec3(0.0f);
    const glm::vec3 d    = c - e.pos;
    const float     dist = glm::length(d);
    if (dist >= e.radius) return glm::vec3(0.0f);

    glm::vec3 dir = dist > 1e-3f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    dir.y += 0.3f;  // bias upwards so rocket jumps work from the floor
    dir = glm::normalize(dir);
    return dir * e.knockback * (1.0f - dist / e.radius);
}

} // namespace carnage
