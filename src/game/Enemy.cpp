#include "game/Enemy.hpp"

#include <algorithm>
#include <cmath>

namespace carnage {

void Enemies::add(Combat& combat, glm::vec3 feet, EnemyType type, int sector) {
    Enemy e;
    e.pos    = feet;
    e.origin = feet;
    e.type   = type;
    e.sector = sector;
    e.target = combat.addTarget(Target::dummy(feet));
    if (sector >= 0) {
        if (Target* t = combat.targetAt(e.target)) t->respawns = false;
    }
    enemies_.push_back(e);
}

bool Enemies::stagger(std::size_t targetIndex) {
    for (Enemy& e : enemies_) {
        if (e.target != targetIndex) continue;
        if (e.state == AiState::Seized) return false;
        enterState(e, AiState::Seized);
        e.sub        = SeizeSub::Staggered;
        e.seizeTimer = cfg_.staggerTime;
        e.vel        = glm::vec2(0.0f);
        return true;
    }
    return false;
}

int Enemies::findStaggered(const glm::vec3& eye, const glm::vec3& fwd) const {
    int   best  = -1;
    float bestT = cfg_.grabReach;
    for (std::size_t i = 0; i < enemies_.size(); ++i) {
        const Enemy& e = enemies_[i];
        if (e.state != AiState::Seized || e.sub != SeizeSub::Staggered) continue;
        const glm::vec3 d = e.pos + glm::vec3(0.0f, 0.9f, 0.0f) - eye;
        const float     t = glm::dot(d, fwd);
        if (t < 0.0f || t > bestT) continue;
        if (glm::length(d - fwd * t) > cfg_.grabRadius) continue;
        bestT = t;
        best  = static_cast<int>(i);
    }
    return best;
}

void Enemies::grab(int i) {
    if (held_ >= 0 || i < 0 || static_cast<std::size_t>(i) >= enemies_.size()) return;
    Enemy& e = enemies_[static_cast<std::size_t>(i)];
    if (e.state != AiState::Seized || e.sub != SeizeSub::Staggered) return;
    e.sub = SeizeSub::Held;
    e.vel = glm::vec2(0.0f);
    held_ = i;
}

void Enemies::throwHeld(const glm::vec3& velocity) {
    if (held_ < 0) return;
    Enemy& e = enemies_[static_cast<std::size_t>(held_)];
    e.sub  = SeizeSub::Thrown;
    e.vel3 = velocity;
    held_  = -1;
}

void Enemies::stepSeized(Enemy& e, Target& t, Combat& combat, const World& world, float dt) {
    t.prevCenter = t.center;
    switch (e.sub) {
        case SeizeSub::None:
            enterState(e, AiState::Chase);
            return;
        case SeizeSub::Staggered:
            e.seizeTimer -= dt;
            move(e, glm::vec2(0.0f), 0.0f, world, dt);
            if (e.seizeTimer <= 0.0f) { e.sub = SeizeSub::None; enterState(e, AiState::Chase); }
            break;
        case SeizeSub::Held:
            e.pos = carry_;
            e.vel = glm::vec2(0.0f);
            break;
        case SeizeSub::Thrown: {
            constexpr float kGravity = 24.0f, kRadius = 0.5f;
            e.vel3.y -= kGravity * dt;
            const float     speed = glm::length(e.vel3);
            const glm::vec3 c     = e.pos + glm::vec3(0.0f, t.half.y, 0.0f);
            glm::vec3       next  = c + e.vel3 * dt;
            glm::vec3       n{0.0f, 1.0f, 0.0f};
            bool            hitWorld = false;

            const glm::vec3 seg = next - c;
            const float     len = glm::length(seg);
            const glm::vec3 dir = len > 1e-5f ? seg / len : glm::vec3(0.0f, -1.0f, 0.0f);
            if (len > 1e-5f) {
                if (const auto h = world.raycast(c, dir, len + kRadius); h && h->t < len + kRadius) {
                    next = c + dir * std::max(0.0f, h->t - kRadius);
                    n = h->normal;
                    hitWorld = true;
                }
            }
            if (const auto g = world.ground({next.x, next.z}, next.y - t.half.y + 0.3f);
                g && next.y - t.half.y <= g->height) {
                next.y = g->height + t.half.y;
                n = {0.0f, 1.0f, 0.0f};
                hitWorld = true;
            }

            const float dmg = std::min(cfg_.throwMaxDmg,
                cfg_.throwBase + cfg_.throwEnergy * 0.5f * cfg_.throwMass * speed * speed);
            int hits = 0;
            if (speed > 5.0f) hits = combat.strike(next, 0.8f, dmg, dir, static_cast<int>(e.target), false);

            e.pos = next - glm::vec3(0.0f, t.half.y, 0.0f);
            if (hitWorld || hits > 0) {
                if (speed > 6.0f) {
                    combat.damageTarget(e.target, dmg, next, n, c);
                    impact_ = std::max(impact_, std::clamp(speed / 18.0f, 0.3f, 1.0f));
                }
                e.vel3 = glm::vec3(0.0f);
                if (t.alive()) {
                    e.sub = SeizeSub::Staggered;  // dazed on the floor, can be re-grabbed
                    e.seizeTimer = 1.5f;
                } else {
                    e.sub = SeizeSub::None;
                }
            }
            break;
        }
    }
    t.center = e.pos + glm::vec3(0.0f, t.half.y, 0.0f);
}

int Enemies::totalInSector(int sector) const {
    int n = 0;
    for (const Enemy& e : enemies_) n += e.sector == sector ? 1 : 0;
    return n;
}

int Enemies::aliveInSector(const Combat& combat, int sector) const {
    int n = 0;
    for (const Enemy& e : enemies_) {
        if (e.sector != sector) continue;
        const auto& ts = combat.targets();
        if (e.target < ts.size() && ts[e.target].alive()) ++n;
    }
    return n;
}

Perception Enemies::perceive(const Enemy& e, const World& world, glm::vec3 playerFeet,
                             float playerEyeHeight) const {
    const glm::vec3 eye    = e.pos + glm::vec3(0.0f, cfg_.eyeHeight, 0.0f);
    const glm::vec3 chest  = playerFeet + glm::vec3(0.0f, playerEyeHeight * 0.8f, 0.0f);
    const glm::vec3 toP    = chest - eye;
    const float     dist   = glm::length(toP);
    Perception      p;
    p.dist = glm::length(glm::vec2(playerFeet.x - e.pos.x, playerFeet.z - e.pos.z));
    if (dist < 1e-3f) { p.visible = true; return p; }

    if (p.dist <= cfg_.hearRange) { p.visible = true; return p; }  // proximity
    if (dist > cfg_.sightRange) return p;

    const glm::vec3 dir = toP / dist;
    const glm::vec2 fwd{std::sin(e.yaw), std::cos(e.yaw)};
    const glm::vec2 flat = glm::vec2(dir.x, dir.z);
    const float     fl   = glm::length(flat);
    if (fl > 1e-3f && glm::dot(fwd, flat / fl) < cfg_.fovCos) return p;  // outside the cone

    const auto hit = world.raycast(eye, dir, dist);
    p.visible = !hit || hit->t >= dist - 0.1f;
    return p;
}

void Enemies::move(Enemy& e, glm::vec2 wish, float speed, const World& world, float dt) {
    const glm::vec2 desired = wish * speed;
    const float     k       = 1.0f - std::exp(-cfg_.accel * dt);
    e.vel += (desired - e.vel) * k;

    glm::vec2 p{e.pos.x + e.vel.x * dt, e.pos.z + e.vel.y * dt};
    // The target box is approximated by a circle covering its XZ extents.
    world.resolveWalls(p, e.pos.y, cfg_.radius, cfg_.stepHeight, e.vel);
    e.pos.x = p.x;
    e.pos.z = p.y;

    if (const auto g = world.ground(p, e.pos.y + cfg_.stepHeight)) {
        // Snap up steps instantly, fall back down under gravity-ish speed.
        e.pos.y = e.pos.y > g->height ? std::max(g->height, e.pos.y - 12.0f * dt) : g->height;
    } else {
        e.pos.y -= 12.0f * dt;
    }
    if (glm::dot(e.vel, e.vel) > 0.04f) e.yaw = std::atan2(e.vel.x, e.vel.y);
}

void Enemies::tick(float dt, Combat& combat, const World& world,
                   glm::vec3 playerFeet, float playerEyeHeight) {
    attacks_.clear();
    footsteps_.clear();
    const glm::vec3 chest = playerFeet + glm::vec3(0.0f, playerEyeHeight * 0.8f, 0.0f);
    for (Enemy& e : enemies_) {
        Target* t = combat.targetAt(e.target);
        if (!t) continue;

        if (!t->alive()) {
            e.wasAlive = false;
            e.vel = glm::vec2(0.0f);
            if (e.state == AiState::Seized) {  // died while staggered / held / in flight
                if (static_cast<int>(&e - enemies_.data()) == held_) held_ = -1;
                e.sub = SeizeSub::None;
                e.state = AiState::Idle;
            }
            t->held = false;
            continue;
        }
        if (!e.wasAlive) {  // respawned: back to the post
            e.wasAlive = true;
            e.pos      = e.origin;
            e.sub      = SeizeSub::None;
            enterState(e, AiState::Idle);
        }
        if (e.state == AiState::Seized) {
            stepSeized(e, *t, combat, world, dt);
            t->held  = e.state == AiState::Seized && e.sub == SeizeSub::Held;
            t->color = e.sub == SeizeSub::Staggered ? glm::vec3(0.9f, 0.85f, 0.35f)
                                                    : glm::vec3(0.95f, 0.95f, 0.95f);
            continue;
        }
        t->held = false;

        const Perception p = perceive(e, world, playerFeet, playerEyeHeight);
        if (stepState(e, cfg_, p, dt)) {
            EnemyAttack a;
            a.from   = e.pos;
            a.ranged = e.type == EnemyType::Ranged;
            if (!a.ranged) {
                a.damage = cfg_.meleeDamage * (held_ >= 0 ? cfg_.shieldMeleeScale : 1.0f);
            } else {
                // Hitscan from the eye toward the chest, perturbed by spread.
                const glm::vec3 eye = e.pos + glm::vec3(0.0f, cfg_.eyeHeight, 0.0f);
                const glm::vec3 toC = chest - eye;
                const float     d   = glm::length(toC);
                glm::vec3       dir = d > 1e-3f ? toC / d : glm::vec3(0.0f, 0.0f, -1.0f);
                dir = glm::normalize(dir + rng_.unitSphere() * cfg_.spread);
                a.from = eye + dir * 0.5f;
                const float maxT = cfg_.rangedRange * 1.5f;
                const auto  hit  = world.raycast(eye, dir, maxT);
                // Closest approach of the ray to the player's chest.
                const float along = glm::dot(chest - eye, dir);
                const float miss  = glm::length(chest - (eye + dir * along));
                const bool  reaches = along > 0.0f && (!hit || hit->t >= along - 0.05f);
                // Human shield: the held enemy's body intercepts shots on the way in.
                float     shieldT = 0.0f;
                glm::vec3 shieldN{0.0f};
                bool      shielded = false;
                if (held_ >= 0) {
                    const Enemy*  se = &enemies_[static_cast<std::size_t>(held_)];
                    const Target* st = combat.targetAt(se->target);
                    shielded = st && st->alive() && along > 0.0f &&
                               rayAabb(eye, dir, st->min(), st->max(), maxT, shieldT, shieldN) &&
                               shieldT < along && (!hit || shieldT < hit->t);
                    if (shielded) {
                        a.shielded = true;
                        a.to       = eye + dir * shieldT;
                        a.normal   = shieldN;
                        combat.damageTarget(se->target, cfg_.rangedDamage, a.to, shieldN, a.from);
                    }
                }
                if (shielded) {
                    // absorbed: no damage to the player
                } else if (reaches && miss <= cfg_.playerRadius) {
                    a.damage = cfg_.rangedDamage;
                    a.to     = eye + dir * along;
                } else if (hit) {
                    a.to      = eye + dir * hit->t;
                    a.blocked = true;
                    a.normal  = hit->normal;
                } else {
                    a.to = eye + dir * maxT;
                }
            }
            attacks_.push_back(a);
        }

        const glm::vec2 toP{playerFeet.x - e.pos.x, playerFeet.z - e.pos.z};
        const float     len = glm::length(toP);
        const glm::vec2 dirP = len > 1e-3f ? toP / len : glm::vec2(0.0f);

        switch (e.state) {
            case AiState::Idle:
                move(e, glm::vec2(0.0f), 0.0f, world, dt);
                break;
            case AiState::Patrol: {
                const float goalZ = e.origin.z + e.patrolDir * cfg_.patrolRadius;
                if (std::abs(goalZ - e.pos.z) < 0.25f) e.patrolDir = -e.patrolDir;
                move(e, {0.0f, e.patrolDir}, cfg_.patrolSpeed, world, dt);
                break;
            }
            case AiState::Chase:
                move(e, dirP, cfg_.chaseSpeed, world, dt);
                break;
            case AiState::Seized:
                break;  // handled above
            case AiState::Attack: {
                if (len > 1e-3f) e.yaw = std::atan2(dirP.x, dirP.y);  // face the player
                // Melee lunge: burst toward the player just before the blow
                // lands, never closer than lungeStopDist.
                const float windup = attackWindup(e, cfg_);
                const bool  lunging = e.type == EnemyType::Melee && !e.strikeDone &&
                                      e.stateTime >= windup - cfg_.lungeTime && len > cfg_.lungeStopDist;
                if (lunging) move(e, dirP, cfg_.lungeSpeed, world, dt);
                else         move(e, glm::vec2(0.0f), 0.0f, world, dt);
                break;
            }
        }

        t->prevCenter = t->center;
        {
            const float moved = glm::length(e.vel) * dt;
            if (moved > 1e-4f) {
                e.stride += moved;
                if (e.stride >= 1.6f) { e.stride = 0.0f; footsteps_.push_back(e.pos); }
            }
        }
        t->center     = e.pos + glm::vec3(0.0f, t->half.y, 0.0f);
        // Debug-readable state colours; the lunge reads as a red tell.
        switch (e.state) {
            case AiState::Idle:   t->color = {0.35f, 0.40f, 0.55f}; break;
            case AiState::Patrol: t->color = {0.30f, 0.50f, 0.50f}; break;
            case AiState::Chase:  t->color = {0.85f, 0.55f, 0.15f}; break;
            case AiState::Attack: t->color = {0.95f, 0.12f, 0.08f}; break;
            case AiState::Seized: break;
        }
        if (e.type == EnemyType::Ranged) t->color = glm::mix(t->color, glm::vec3(0.6f, 0.1f, 0.7f), 0.5f);
    }
}

} // namespace carnage
