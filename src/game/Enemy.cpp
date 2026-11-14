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

        if (!t->alive()) { e.wasAlive = false; e.vel = glm::vec2(0.0f); continue; }
        if (!e.wasAlive) {  // respawned: back to the post
            e.wasAlive = true;
            e.pos      = e.origin;
            enterState(e, AiState::Idle);
        }

        const Perception p = perceive(e, world, playerFeet, playerEyeHeight);
        if (stepState(e, cfg_, p, dt)) {
            EnemyAttack a;
            a.from   = e.pos;
            a.ranged = e.type == EnemyType::Ranged;
            if (!a.ranged) {
                a.damage = cfg_.meleeDamage;
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
                if (reaches && miss <= cfg_.playerRadius) {
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
        }
        if (e.type == EnemyType::Ranged) t->color = glm::mix(t->color, glm::vec3(0.6f, 0.1f, 0.7f), 0.5f);
    }
}

} // namespace carnage
