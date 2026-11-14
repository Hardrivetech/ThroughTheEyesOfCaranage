#include "game/Interact.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

constexpr float kGravity     = 24.0f;
constexpr float kReach       = 2.6f;   // grab distance along the view ray
constexpr float kGrabRadius  = 0.75f;  // max distance from the view ray
constexpr float kEnergyScale = 0.30f;  // damage per joule-ish
constexpr float kMaxDamage   = 160.0f;
constexpr float kHitPhase    = 0.42f;  // swing progress where the strike lands

glm::mat3 matOf(const glm::quat& q) { return glm::mat3_cast(q); }

} // namespace

const KindInfo& info(PickupKind k) {
    //                           radius hitR  mass base swing time throw
    static constexpr KindInfo kTable[] = {
        {"Pipe",   0.06f, 0.50f, 2.0f, 18.0f,  9.0f, 0.34f, 22.0f},
        {"Bat",    0.06f, 0.50f, 1.5f, 15.0f, 10.0f, 0.30f, 22.0f},
        {"Barrel", 0.45f, 0.65f, 9.0f,  8.0f,  6.0f, 0.55f, 14.0f},
        {"Body",   0.40f, 0.65f, 7.0f,  6.0f,  6.0f, 0.50f, 15.0f},
    };
    return kTable[static_cast<int>(k)];
}

void Interact::add(PickupKind kind, const glm::vec3& pos, float yaw) {
    Pickup p;
    p.kind    = kind;
    p.pos     = p.prevPos = pos;
    p.rot     = p.prevRot = glm::angleAxis(yaw, glm::vec3(0, 1, 0));
    pickups_.push_back(p);
}

std::optional<std::size_t> Interact::findGrabTarget(const InteractContext& ctx) const {
    std::optional<std::size_t> best;
    float bestT = kReach;
    for (std::size_t i = 0; i < pickups_.size(); ++i) {
        const Pickup& p = pickups_[i];
        if (!p.alive || p.thrown) continue;
        const glm::vec3 d = p.pos - ctx.eye;
        const float t = glm::dot(d, ctx.forward);
        if (t < 0.0f || t > bestT) continue;
        if (glm::length(d - ctx.forward * t) > kGrabRadius) continue;
        bestT = t;
        best  = i;
    }
    return best;
}

InteractEvents Interact::tick(float dt, const InteractInput& in, const InteractContext& ctx,
                              Combat& combat, const World& world) {
    InteractEvents ev;

    // --- Swing state --------------------------------------------------------
    prevSwingT_ = swingT_;
    cooldown_   = std::max(0.0f, cooldown_ - dt);

    if (held_ && swinging_) {
        const KindInfo& k = info(*held_);
        swingT_ += dt / k.swingTime;

        if (!hitDone_ && swingT_ >= kHitPhase) {
            hitDone_ = true;
            // Swing tip speed peaks mid-strike; player velocity along the view adds.
            const float forwardSpeed = std::max(0.0f, glm::dot(ctx.playerVel, ctx.forward));
            const float v = k.swingSpeed + forwardSpeed;
            const float energy = 0.5f * k.mass * v * v;
            const float dmg = std::min(kMaxDamage, k.baseDamage + kEnergyScale * energy);
            const glm::vec3 center = ctx.eye + ctx.forward * 1.5f;

            const int hits = combat.strike(center, k.hitRadius, dmg, ctx.forward);
            ev.swung  = true;
            ev.hit    = hits > 0;
            ev.impact = std::clamp(v / 18.0f, 0.2f, 1.0f) * (hits > 0 ? 1.0f : 0.35f);

            if (hits == 0 && world.raycast(ctx.eye, ctx.forward, 1.8f)) ev.impact = 0.4f;
            if (*held_ == PickupKind::Barrel && hits > 0) {
                combat.detonate(center, 4.0f, 70.0f, 12.0f);  // hazard: bludgeon sets it off
                held_.reset();
                ev.exploded = true;
                swinging_   = false;
                swingT_ = prevSwingT_ = 0.0f;
            }
        }
        if (swingT_ >= 1.0f) {
            swinging_ = false;
            swingT_ = prevSwingT_ = 0.0f;
            cooldown_ = 0.08f;
        }
    }

    // --- Grab / hurl ---------------------------------------------------------
    if (in.grabPressed && !swinging_) {
        if (held_) {
            const KindInfo& k = info(*held_);
            Pickup p;
            p.kind    = *held_;
            p.pos     = p.prevPos = ctx.eye + ctx.forward * 0.7f;
            p.vel     = ctx.forward * k.throwSpeed + ctx.playerVel * 0.8f;
            p.rot     = p.prevRot = glm::angleAxis(
                std::atan2(-ctx.forward.x, -ctx.forward.z), glm::vec3(0, 1, 0));
            p.spin    = glm::cross(ctx.forward, glm::vec3(0, 1, 0)) * (10.0f / (1.0f + 0.1f * k.mass));
            p.thrown  = true;
            p.life    = *held_ == PickupKind::Body ? 5.0f : 0.0f;
            pickups_.push_back(p);
            held_.reset();
            ev.thrown = true;
            ev.impact = 0.5f;
        } else if (const auto idx = findGrabTarget(ctx)) {
            held_ = pickups_[*idx].kind;
            pickups_[*idx] = pickups_.back();
            pickups_.pop_back();
            ev.grabbed = true;
        } else if (combat.seize(ctx.eye + ctx.forward * 1.4f, 1.1f)) {
            held_      = PickupKind::Body;  // grabbed an enemy
            ev.grabbed = true;
        }
    }

    if (in.swingPressed && held_ && !swinging_ && cooldown_ <= 0.0f) {
        swinging_ = true;
        hitDone_  = false;
        swingT_   = prevSwingT_ = 0.0f;
    }

    // --- Loose pickups -------------------------------------------------------
    for (Pickup& p : pickups_) stepPickup(p, dt, combat, world, ev);
    std::erase_if(pickups_, [](const Pickup& p) { return !p.alive; });

    canGrab_ = !held_ && (findGrabTarget(ctx).has_value() ||
                          combat.canSeize(ctx.eye + ctx.forward * 1.4f, 1.1f));
    return ev;
}

void Interact::stepPickup(Pickup& p, float dt, Combat& combat, const World& world,
                          InteractEvents& ev) {
    p.prevPos = p.pos;
    p.prevRot = p.rot;

    if (p.life > 0.0f) {
        p.life -= dt;
        if (p.life <= 0.0f) { p.alive = false; return; }
    }

    const KindInfo& k = info(p.kind);
    p.vel.y -= kGravity * dt;

    glm::vec3 next = p.pos + p.vel * dt;
    bool impact = false;
    float impactSpeed = glm::length(p.vel);

    // Walls and ceilings: sweep the segment so fast throws can't tunnel.
    const glm::vec3 seg = next - p.pos;
    const float len = glm::length(seg);
    if (len > 1e-5f) {
        const glm::vec3 dir = seg / len;
        if (const auto h = world.raycast(p.pos, dir, len + k.radius)) {
            if (h->t < len + k.radius) {
                next = p.pos + dir * std::max(0.0f, h->t - k.radius);
                p.vel -= h->normal * (glm::dot(p.vel, h->normal) * 1.35f);  // e = 0.35
                p.spin *= 0.6f;
                impact = true;
            }
        }
    }

    // Floor.
    bool grounded = false;
    if (const auto g = world.ground({next.x, next.z}, next.y + k.radius + 0.2f)) {
        if (next.y - k.radius <= g->height) {
            if (p.vel.y < -1.0f) impact = true;
            next.y = g->height + k.radius;
            p.vel.y = p.vel.y < -1.0f ? -p.vel.y * 0.35f : 0.0f;
            const float fr = std::max(0.0f, 1.0f - 5.0f * dt);
            p.vel.x *= fr;
            p.vel.z *= fr;
            p.spin  *= fr;
            grounded = true;
        }
    }

    // Thrown objects that connect with a target.
    if (p.thrown && impactSpeed > 5.0f) {
        const float energy = 0.5f * k.mass * impactSpeed * impactSpeed;
        const float dmg = std::min(kMaxDamage, k.baseDamage + kEnergyScale * energy);
        const glm::vec3 dir = impactSpeed > 1e-4f ? p.vel / impactSpeed : glm::vec3(0, 0, -1);
        const int hits = combat.strike(next, k.hitRadius, dmg, dir);
        if (hits > 0) {
            ev.hit    = true;
            ev.impact = std::max(ev.impact, std::clamp(impactSpeed / 20.0f, 0.3f, 1.0f));
            p.vel     = -p.vel * 0.25f;
            p.thrown  = false;
            impact    = true;
        }
    }

    if (p.kind == PickupKind::Barrel && p.thrown && impact && impactSpeed > 6.0f) {
        combat.detonate(next, 5.0f, 90.0f, 14.0f);
        ev.exploded = true;
        ev.impact   = 1.0f;
        p.alive     = false;
        return;
    }

    p.pos = next;

    const float spinLen = glm::length(p.spin);
    if (spinLen > 1e-3f)
        p.rot = glm::normalize(glm::angleAxis(spinLen * dt, p.spin / spinLen) * p.rot);

    if (grounded && glm::length(p.vel) < 0.4f) {
        p.thrown = false;
        p.spin   = glm::vec3(0.0f);
    }

    if (p.pos.y < -30.0f) p.alive = false;
}

void Interact::appendShape(const Pickup& p, const glm::vec3& pos, const glm::mat3& R,
                           std::vector<DrawBox>& out) const {
    auto add = [&](glm::vec3 local, glm::vec3 half, glm::vec3 col, Material m) {
        out.push_back({pos + R * local, half, R, col, false, m});
    };
    switch (p.kind) {
        case PickupKind::Pipe:
            add({0, 0, 0}, {0.035f, 0.035f, 0.50f}, {0.34f, 0.35f, 0.37f}, Material::Steel);
            add({0, 0, -0.50f}, {0.045f, 0.045f, 0.03f}, {0.28f, 0.29f, 0.30f}, Material::Steel);
            break;
        case PickupKind::Bat:
            add({0, 0, 0.1f}, {0.030f, 0.030f, 0.35f}, {0.45f, 0.30f, 0.15f}, Material::Leather);
            add({0, 0, -0.25f}, {0.048f, 0.048f, 0.20f}, {0.45f, 0.30f, 0.15f}, Material::Leather);
            break;
        case PickupKind::Barrel:
            add({0, 0, 0}, {0.30f, 0.45f, 0.30f}, {0.70f, 0.12f, 0.08f}, Material::PaintedMetal);
            add({0, 0.1f, 0}, {0.305f, 0.07f, 0.305f}, {0.90f, 0.75f, 0.10f}, Material::PaintedMetal);
            break;
        case PickupKind::Body:
            add({0, 0, 0}, {0.28f, 0.40f, 0.16f}, {0.55f, 0.20f, 0.12f}, Material::Cloth);
            add({0, 0.52f, 0}, {0.13f, 0.13f, 0.13f}, {0.82f, 0.64f, 0.52f}, Material::Skin);
            break;
    }
}

void Interact::collect(float alpha, std::vector<DrawBox>& out) const {
    for (const Pickup& p : pickups_) {
        if (!p.alive) continue;
        const glm::vec3 pos = glm::mix(p.prevPos, p.pos, alpha);
        const glm::quat rot = glm::slerp(p.prevRot, p.rot, alpha);
        appendShape(p, pos, matOf(rot), out);
    }
}

void Interact::buildHeld(float alpha, std::vector<DrawBox>& out) const {
    if (!held_) return;

    const float u = glm::mix(prevSwingT_, swingT_, alpha);
    // Wind-up back, fast sweep across, then recover.
    float sweep = 0.0f;
    if (!swinging_)    sweep = 0.0f;
    else if (u < 0.30f) sweep = -0.8f * (u / 0.30f);
    else if (u < 0.55f) sweep = -0.8f + 2.0f * ((u - 0.30f) / 0.25f);
    else                sweep = 1.2f * (1.0f - (u - 0.55f) / 0.45f);

    const float thrust = swinging_ && u > 0.3f && u < 0.6f ? 0.18f : 0.0f;
    const glm::vec3 pivot{-0.26f - sweep * 0.12f, -0.26f + std::abs(sweep) * 0.03f,
                          -0.42f - thrust};
    const glm::quat q = glm::angleAxis(sweep * 0.9f, glm::vec3(0, 1, 0)) *
                        glm::angleAxis(0.35f + sweep * 0.25f, glm::vec3(1, 0, 0));
    const glm::mat3 R = matOf(q);

    Pickup proto;
    proto.kind = *held_;
    // Shift the object forward so the grip sits at the hand.
    const float fwdOffset = *held_ == PickupKind::Barrel || *held_ == PickupKind::Body ? 0.45f : 0.30f;
    appendShape(proto, pivot + R * glm::vec3(0.0f, 0.0f, -fwdOffset), R, out);

    // Gloved hand and forearm.
    const glm::vec3 tape{0.22f, 0.20f, 0.18f}, glove{0.10f, 0.10f, 0.12f};
    out.push_back({pivot + R * glm::vec3(0.0f, -0.02f, 0.14f), {0.042f, 0.042f, 0.14f}, R, tape,
                   false, Material::Cloth});
    out.push_back({pivot, {0.050f, 0.050f, 0.050f}, R, glove, false, Material::Leather});
}

} // namespace carnage
