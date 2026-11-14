#include "game/Movement.hpp"

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

constexpr float kVoidY = -30.0f;

// Quake-style: add speed along `dir` only until the projected speed reaches
// `wishSpeed`. Speed already above that is left alone (momentum preserved),
// and the velocity direction turns gradually rather than snapping.
glm::vec2 accelerate(glm::vec2 vel, glm::vec2 dir, float wishSpeed, float accel,
                     float basis, float dt) {
    const float add = wishSpeed - glm::dot(vel, dir);
    if (add <= 0.0f) return vel;
    return vel + dir * std::min(accel * basis * dt, add);
}

glm::vec2 clampSpeed(glm::vec2 h, float maxSpeed) {
    const float s = glm::length(h);
    return s > maxSpeed ? h * (maxSpeed / s) : h;
}

} // namespace

MovementController::MovementController(const World& world, const MovementConfig& cfg)
    : world_(world), cfg_(cfg), eye_(cfg.standEye), prevEye_(cfg.standEye) {}

void MovementController::respawn(glm::vec3 p) {
    spawn_ = pos_ = prevPos_ = p;
    vel_          = glm::vec3(0.0f);
    groundNormal_ = {0.0f, 1.0f, 0.0f};
    state_        = MoveState::Walking;
    grounded_     = true;
    justLanded_   = false;
    coyote_ = jumpBuffer_ = slideCooldown_ = wallCooldown_ = wallTimer_ = 0.0f;
    vaultCooldown_ = vaultT_ = chainTimer_ = 0.0f;
    slideChain_ = 0;
    eye_ = prevEye_ = cfg_.standEye;
    roll_ = prevRoll_ = 0.0f;
}

void MovementController::impulse(glm::vec3 v) {
    vel_ += v;
    if (v.y > 0.5f && grounded_) {
        grounded_ = false;
        state_    = MoveState::Airborne;
        coyote_   = 0.0f;
    }
}

glm::vec3 MovementController::eyePosition(float alpha) const {
    const glm::vec3 a = prevPos_ + glm::vec3(0.0f, prevEye_, 0.0f);
    const glm::vec3 b = pos_     + glm::vec3(0.0f, eye_,     0.0f);
    return glm::mix(a, b, alpha);
}

float MovementController::cameraRoll(float alpha) const {
    return glm::mix(prevRoll_, roll_, alpha);
}

void MovementController::tick(float dt, float yaw, const MoveInput& in) {
    events_  = {};
    prevPos_ = pos_;
    prevEye_ = eye_;
    prevRoll_ = roll_;

    if (noclip_) {
        const glm::vec2 fwd{-std::sin(yaw), -std::cos(yaw)};
        const glm::vec2 right{std::cos(yaw), -std::sin(yaw)};
        const glm::vec2 wish = fwd * in.move.y + right * in.move.x;
        const float     spd  = in.sprint ? 30.0f : 12.0f;
        const float     up   = static_cast<float>(in.jumpHeld) - static_cast<float>(in.crouchHeld);
        vel_   = glm::vec3(wish.x * spd, up * spd * 0.7f, wish.y * spd);
        pos_  += vel_ * dt;
        state_ = MoveState::Airborne;
        grounded_ = false;
        roll_ = 0.0f;
        return;
    }

    coyote_        = grounded_ ? cfg_.coyoteTime : std::max(0.0f, coyote_ - dt);
    jumpBuffer_    = in.jumpPressed ? cfg_.jumpBuffer : std::max(0.0f, jumpBuffer_ - dt);
    slideCooldown_ = std::max(0.0f, slideCooldown_ - dt);
    wallCooldown_  = std::max(0.0f, wallCooldown_ - dt);
    vaultCooldown_ = std::max(0.0f, vaultCooldown_ - dt);
    chainTimer_    = std::max(0.0f, chainTimer_ - dt);
    if (chainTimer_ <= 0.0f) slideChain_ = 0;

    Frame f;
    f.dt    = dt;
    f.in    = in;
    f.right = {std::cos(yaw), -std::sin(yaw)};
    const glm::vec2 fwd{-std::sin(yaw), -std::cos(yaw)};
    const glm::vec2 wish = fwd * in.move.y + f.right * in.move.x;
    const float     wl   = glm::length(wish);
    f.wishMag = std::min(wl, 1.0f);
    f.wishDir = wl > 1e-4f ? wish / wl : glm::vec2(0.0f);
    f.landed  = justLanded_;
    justLanded_ = false;

    glm::vec2 h{vel_.x, vel_.z};

    if (state_ != MoveState::Vaulting) tryVault(f, h);
    if (state_ == MoveState::Vaulting) {
        updateVault(f);
        updateCamera(f);
        return;
    }

    if (grounded_ && jumpBuffer_ > 0.0f) groundJump(h);
    if (grounded_) groundMove(f, h);
    else           airMove(f, h);

    // Horizontal collision against walls, for every state.
    glm::vec2 p{pos_.x, pos_.z};
    world_.resolveWalls(p, pos_.y, cfg_.radius, cfg_.stepHeight, h);
    pos_.x = p.x;
    pos_.z = p.y;
    vel_.x = h.x;
    vel_.z = h.y;

    // Ceiling / overhead collision check.
    const float headTop = pos_.y + eye_ + 0.20f;
    if (const auto ceilY = world_.ceiling(p, pos_.y, headTop + 0.1f)) {
        if (headTop > *ceilY) {
            pos_.y = *ceilY - eye_ - 0.20f;
            if (vel_.y > 0.0f) vel_.y = 0.0f;
        }
    }

    updateCamera(f);

    if (pos_.y < kVoidY) respawn(spawn_);
}

void MovementController::groundJump(glm::vec2& h) {
    // Jump keeps all horizontal momentum; jumping out of a slide adds a bonus.
    // Consecutive slide-jumps stack a growing multiplier (chain window resets it).
    if (state_ == MoveState::Sliding) {
        const float mult = cfg_.slideJumpBoost + cfg_.slideChainBonus * static_cast<float>(slideChain_);
        h = clampSpeed(h * mult, cfg_.maxSpeed);
        slideChain_ = std::min(slideChain_ + 1, cfg_.slideChainMax);
        chainTimer_ = cfg_.slideChainWindow;
    }

    vel_.y      = std::max(vel_.y, 0.0f) + cfg_.jumpSpeed;  // keep upward ramp velocity
    grounded_   = false;
    state_      = MoveState::Airborne;
    jumpBuffer_ = 0.0f;
    coyote_     = 0.0f;
    events_.jumped = true;
}

void MovementController::groundMove(const Frame& f, glm::vec2& h) {
    const float dt    = f.dt;
    const float speed = glm::length(h);
    const MoveInput& in = f.in;

    auto chooseGroundState = [&] {
        return (in.sprint && in.move.y > 0.1f) ? MoveState::Sprinting : MoveState::Walking;
    };

    if (state_ == MoveState::Sliding) {
        if (!in.crouchHeld || speed < cfg_.slideExitSpeed) state_ = chooseGroundState();
    } else {
        const bool wantSlide = (in.crouchPressed || (f.landed && in.crouchHeld)) &&
                               speed >= cfg_.slideEnterSpeed && slideCooldown_ <= 0.0f;
        if (wantSlide) {
            if (!f.landed) h = clampSpeed(h * ((speed + cfg_.slideBoost) / speed), cfg_.maxSpeed);
            state_         = MoveState::Sliding;
            slideCooldown_ = cfg_.slideCooldown;
            events_.slideStarted = true;
        } else {
            state_ = chooseGroundState();
        }
    }

    const glm::vec3 n = groundNormal_;

    if (state_ == MoveState::Sliding) {
        // Gravity along the slope, then Coulomb friction (constant decel).
        // slideSlopeGain over-weights the downhill pull so steep ramps keep
        // accelerating (friction equilibrium sits at tan(theta) = mu / (1 + gain)).
        h += glm::vec2(n.x, n.z) * (cfg_.gravity * n.y * (1.0f + cfg_.slideSlopeGain)) * dt;
        const float sp = glm::length(h);
        if (sp > 1e-4f) {
            const float drop = cfg_.slideFriction * cfg_.gravity * n.y * dt;
            h *= std::max(0.0f, sp - drop) / sp;
        }
        if (f.wishMag > 0.0f)
            h = accelerate(h, f.wishDir, cfg_.slideSteerSpeed, cfg_.slideSteerAccel,
                           cfg_.slideSteerSpeed, dt);
    } else {
        const float sp = glm::length(h);
        if (sp > 1e-4f) {
            float fr = cfg_.groundFriction;
            if (sp > cfg_.sprintSpeed * 1.05f) fr *= cfg_.overspeedFrictionScale;
            const float drop = std::max(sp, cfg_.stopSpeed) * fr * dt;
            h *= std::max(0.0f, sp - drop) / sp;
        }
        const float cap = (state_ == MoveState::Sprinting ? cfg_.sprintSpeed : cfg_.walkSpeed) * f.wishMag;
        if (f.wishMag > 0.0f)
            h = accelerate(h, f.wishDir, cap, cfg_.groundAccel, cap, dt);
    }

    h = clampSpeed(h, cfg_.maxSpeed);

    // Velocity follows the surface, so leaving a ramp edge launches along it.
    vel_.y = n.y < 0.9999f ? -(h.x * n.x + h.y * n.z) / n.y : 0.0f;

    const glm::vec2 p = glm::vec2(pos_.x, pos_.z) + h * dt;
    const auto g = world_.ground(p, pos_.y + cfg_.stepHeight);
    pos_.x = p.x;
    pos_.z = p.y;
    if (g && g->height >= pos_.y - cfg_.snapDown) {
        pos_.y        = g->height;
        groundNormal_ = g->normal;
    } else {
        grounded_ = false;
        state_    = MoveState::Airborne;
        coyote_   = cfg_.coyoteTime;
        pos_.y   += vel_.y * dt;
    }
}

bool MovementController::updateWallRun(const Frame& f, glm::vec2& h) {
    const glm::vec2 pos2{pos_.x, pos_.z};
    const float reach = cfg_.radius + cfg_.wallReach;
    const float speed = glm::length(h);
    const MoveInput& in = f.in;

    if (state_ == MoveState::WallRunning) {
        const auto w   = world_.probeWall(pos2, pos_.y, reach, cfg_.wallMinHeight);
        const auto gnd = world_.ground(pos2, pos_.y + cfg_.stepHeight);
        const bool nearGround = gnd && (pos_.y - gnd->height) < 0.15f;

        if (w && jumpBuffer_ > 0.0f) {
            // Wall jump: keep the run's momentum, kick away from the wall.
            glm::vec2 t{-w->normal.y, w->normal.x};
            if (glm::dot(h, t) < 0.0f) t = -t;
            h = clampSpeed(t * speed + w->normal * cfg_.wallJumpPush, cfg_.maxSpeed);
            vel_.y         = cfg_.wallJumpUp;
            state_         = MoveState::Airborne;
            wallCooldown_  = cfg_.wallRunCooldown;
            jumpBuffer_    = 0.0f;
            events_.wallJumped = true;
            return false;
        }

        const bool keep = w && in.move.y > 0.1f && speed >= cfg_.wallRunMinSpeed * 0.7f &&
                          wallTimer_ < cfg_.wallRunMaxTime && !nearGround;
        if (!keep) {
            state_        = MoveState::Airborne;
            wallCooldown_ = 0.25f;
            return false;
        }
        wallNormal_ = w->normal;
        return true;
    }

    if (wallCooldown_ > 0.0f || in.move.y <= 0.1f || speed < cfg_.wallRunMinSpeed) return false;

    const auto w = world_.probeWall(pos2, pos_.y, reach, cfg_.wallMinHeight);
    if (!w) return false;
    const auto gnd = world_.ground(pos2, pos_.y + cfg_.stepHeight);
    if (gnd && (pos_.y - gnd->height) < 0.5f) return false;            // too close to the ground
    if (glm::dot(h / speed, -w->normal) > cfg_.maxAttachDot) return false;  // nearly head-on

    state_      = MoveState::WallRunning;
    wallNormal_ = w->normal;
    wallTimer_  = 0.0f;
    vel_.y      = std::clamp(vel_.y, -2.0f, 2.5f);
    events_.wallRunStarted = true;
    return true;
}

void MovementController::airMove(const Frame& f, glm::vec2& h) {
    const float dt = f.dt;

    // Coyote jump: walked off a ledge a moment ago.
    if (jumpBuffer_ > 0.0f && coyote_ > 0.0f && state_ != MoveState::WallRunning) {
        vel_.y      = cfg_.jumpSpeed;
        jumpBuffer_ = 0.0f;
        coyote_     = 0.0f;
        events_.jumped = true;
    }

    if (updateWallRun(f, h)) {
        glm::vec2 t{-wallNormal_.y, wallNormal_.x};
        if (glm::dot(h, t) < 0.0f) t = -t;
        float sp = glm::length(h);  // carry full speed along the wall
        if (sp < cfg_.wallRunSpeed) sp = std::min(cfg_.wallRunSpeed, sp + cfg_.wallRunAccel * dt);
        const glm::vec2 target = t * sp - wallNormal_ * cfg_.wallStick;
        const float     b      = std::clamp(wallTimer_ / cfg_.wallRunBlendTime, 0.0f, 1.0f);
        h = glm::mix(h, target, b * b * (3.0f - 2.0f * b));

        vel_.y -= cfg_.gravity * cfg_.wallRunGravityScale * dt;
        vel_.y  = std::max(vel_.y, -cfg_.wallRunMaxFall);
        wallTimer_ += dt;
    } else {
        state_ = MoveState::Airborne;
        if (f.wishMag > 0.0f)
            h = accelerate(h, f.wishDir, cfg_.airSpeedCap, cfg_.airAccel, cfg_.walkSpeed, dt);
        vel_.y -= cfg_.gravity * (vel_.y < 0.0f ? cfg_.fallGravityScale : 1.0f) * dt;
    }

    h = clampSpeed(h, cfg_.maxSpeed);

    const float prevFeet = pos_.y;
    const glm::vec2 p = glm::vec2(pos_.x, pos_.z) + h * dt;
    pos_.x  = p.x;
    pos_.z  = p.y;
    pos_.y += vel_.y * dt;

    const auto g = world_.ground(p, prevFeet + cfg_.stepHeight);
    if (g && pos_.y <= g->height) {
        events_.landImpact = std::max(0.0f, -vel_.y);
        pos_.y        = g->height;
        vel_.y        = 0.0f;
        groundNormal_ = g->normal;
        grounded_     = true;
        justLanded_   = true;
        state_        = MoveState::Walking;
    }
}

bool MovementController::tryVault(const Frame& f, const glm::vec2& h) {
    const MoveInput& in = f.in;
    if (vaultCooldown_ > 0.0f || state_ == MoveState::Sliding || state_ == MoveState::WallRunning) return false;
    if (in.move.y <= 0.1f) return false;
    if (!(jumpBuffer_ > 0.0f || (in.sprint && grounded_))) return false;

    const float speed = glm::length(h);
    if (speed < cfg_.vaultMinSpeed) return false;

    const glm::vec2 dir  = f.wishMag > 0.0f ? f.wishDir : h / speed;
    const glm::vec2 p{pos_.x, pos_.z};
    const float maxTop = pos_.y + cfg_.vaultMaxHeight;

    const auto obstacle = world_.ground(p + dir * (cfg_.radius + 0.3f), maxTop);
    if (!obstacle) return false;
    const float rise = obstacle->height - pos_.y;
    if (rise <= cfg_.stepHeight + 0.05f || rise > cfg_.vaultMaxHeight) return false;

    const glm::vec2 end2 = p + dir * (cfg_.radius + cfg_.vaultReach);
    const auto land = world_.ground(end2, maxTop);
    if (!land || land->normal.y < 0.95f || std::abs(land->height - obstacle->height) > 0.1f) return false;
    if (world_.ceiling(end2, land->height, land->height + 1.9f)) return false;

    state_       = MoveState::Vaulting;
    grounded_    = false;
    vaultT_      = 0.0f;
    vaultDur_    = cfg_.vaultDuration;
    vaultStart_  = pos_;
    vaultEnd_    = {end2.x, land->height, end2.y};
    vaultDir_    = dir;
    vaultSpeed_  = std::max(speed, cfg_.vaultExitSpeed);
    jumpBuffer_  = 0.0f;
    events_.vaultStarted = true;
    return true;
}

void MovementController::updateVault(const Frame& f) {
    vaultT_ += f.dt;
    const float u    = std::clamp(vaultT_ / vaultDur_, 0.0f, 1.0f);
    const float ease = u * u * (3.0f - 2.0f * u);
    const float rise = 1.0f - (1.0f - u) * (1.0f - u);

    pos_.x = glm::mix(vaultStart_.x, vaultEnd_.x, ease);
    pos_.z = glm::mix(vaultStart_.z, vaultEnd_.z, ease);
    pos_.y = glm::mix(vaultStart_.y, vaultEnd_.y, rise) + 0.12f * std::sin(u * glm::pi<float>());
    vel_   = {vaultDir_.x * vaultSpeed_, 0.0f, vaultDir_.y * vaultSpeed_};

    if (u >= 1.0f) {
        pos_          = vaultEnd_;
        groundNormal_ = {0.0f, 1.0f, 0.0f};
        grounded_     = true;
        state_        = f.in.sprint ? MoveState::Sprinting : MoveState::Walking;
        vaultCooldown_ = cfg_.vaultCooldown;
        events_.vaultFinished = true;
    }
}

void MovementController::updateCamera(const Frame& f) {
    const float dt = f.dt;

    float targetEye = state_ == MoveState::Sliding ? cfg_.slideEye : cfg_.standEye;
    if (state_ == MoveState::Vaulting)
        targetEye -= cfg_.vaultEyeDip * std::sin(glm::pi<float>() * std::clamp(vaultT_ / vaultDur_, 0.0f, 1.0f));
    eye_ += (targetEye - eye_) * (1.0f - std::exp(-14.0f * dt));

    // Lean away from the wall: wall on the right -> positive roll (head left).
    float targetRoll = 0.0f;
    if (state_ == MoveState::WallRunning)
        targetRoll = (glm::dot(wallNormal_, f.right) < 0.0f ? 1.0f : -1.0f) * cfg_.wallRunTilt;
    roll_ += (targetRoll - roll_) * (1.0f - std::exp(-10.0f * dt));
}

} // namespace carnage
