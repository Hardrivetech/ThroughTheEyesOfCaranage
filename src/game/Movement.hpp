#pragma once
#include "game/World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace carnage {

enum class MoveState { Walking, Sprinting, Sliding, Airborne, WallRunning, Vaulting };

constexpr const char* name(MoveState s) {
    switch (s) {
        case MoveState::Walking:     return "walking";
        case MoveState::Sprinting:   return "sprinting";
        case MoveState::Sliding:     return "sliding";
        case MoveState::Airborne:    return "airborne";
        case MoveState::WallRunning: return "wallrunning";
        case MoveState::Vaulting:    return "vaulting";
    }
    return "?";
}

struct MovementConfig {
    // Body
    float radius     = 0.4f;
    float stepHeight = 0.4f;   // max ledge walked up automatically
    float snapDown   = 0.35f;  // max drop that keeps you glued to the ground
    float standEye   = 1.7f;
    float slideEye   = 0.8f;

    // Gravity
    float gravity          = 24.0f;
    float fallGravityScale = 1.3f;

    // Ground speeds and curves
    float walkSpeed              = 6.0f;
    float sprintSpeed            = 10.5f;
    float maxSpeed               = 30.0f;  // hard horizontal cap
    float groundAccel            = 7.0f;   // 1/s, scaled by wish speed
    float groundFriction         = 6.0f;   // exponential, 1/s
    float overspeedFrictionScale = 0.35f;  // bleed rate above sprint speed
    float stopSpeed              = 2.0f;   // friction floor so stops aren't asymptotic

    // Air
    float airAccel    = 1.2f;
    float airSpeedCap = 2.0f;

    // Jump
    float jumpSpeed  = 8.2f;
    float coyoteTime = 0.10f;
    float jumpBuffer = 0.12f;

    // Slide
    float slideEnterSpeed = 7.5f;
    float slideExitSpeed  = 3.0f;
    float slideBoost      = 2.0f;   // one-off m/s when sliding out of a run
    float slideFriction   = 0.18f;  // Coulomb coefficient (decel = mu * g * cos)
    float slideSteerSpeed = 3.0f;
    float slideSteerAccel = 3.0f;
    float slideCooldown   = 0.4f;
    float slideJumpBoost  = 1.1f;
    // Extra gravity-along-slope multiplier while sliding downhill, so steep
    // ramps keep accelerating instead of settling at a friction equilibrium.
    float slideSlopeGain    = 0.6f;
    // Each consecutive slide-jump adds this much to the jump multiplier.
    float slideChainBonus   = 0.05f;
    int   slideChainMax     = 4;
    float slideChainWindow  = 1.5f;  // s to land/slide again before the chain resets

    // Vault
    float vaultMaxHeight = 1.3f;   // tallest ledge (above feet) that can be vaulted
    float vaultMinSpeed  = 3.0f;
    float vaultDuration  = 0.32f;
    float vaultExitSpeed = 6.0f;   // minimum speed handed back after the vault
    float vaultCooldown  = 0.25f;
    float vaultReach     = 0.5f;   // how far past the body radius the hands land
    float vaultEyeDip    = 0.25f;

    // Wall run
    float wallRunMinSpeed     = 5.0f;
    float wallRunSpeed        = 11.0f;
    float wallRunAccel        = 8.0f;
    float wallRunGravityScale = 0.15f;
    float wallRunMaxFall      = 4.0f;
    float wallRunMaxTime      = 2.5f;
    float wallReach           = 0.35f;
    float wallMinHeight       = 2.5f;   // wall top above feet needed to run on it
    float wallStick           = 1.0f;
    float wallJumpPush        = 7.0f;
    float wallJumpUp          = 8.5f;
    float wallRunCooldown     = 0.30f;
    float wallRunTilt         = glm::radians(14.0f);
    float maxAttachDot        = 0.85f;  // reject near head-on approaches
    float wallRunBlendTime    = 0.15f;  // s to blend from airborne velocity onto the wall
};

struct MoveInput {
    glm::vec2 move{0.0f};        // x = strafe right, y = forward, [-1,1]
    bool sprint        = false;
    bool crouchHeld    = false;
    bool crouchPressed = false;
    bool jumpPressed   = false;
    bool jumpHeld      = false;  // used by dev no-clip (ascend)
};

// Things that happened during the last tick, for camera/audio feedback.
struct MoveEvents {
    float landImpact = 0.0f;  // downward speed on touchdown (m/s)
    bool  jumped         = false;
    bool  wallJumped     = false;
    bool  slideStarted   = false;
    bool  wallRunStarted = false;
    bool  vaultStarted   = false;
    bool  vaultFinished  = false;
};

class MovementController {
public:
    explicit MovementController(const World& world, const MovementConfig& cfg = {});

    void respawn(glm::vec3 feetPosition);
    void tick(float dt, float yaw, const MoveInput& in);

    // Instant velocity change (explosions, knockback). Lifts off the ground
    // when the impulse has a meaningful upward component.
    void impulse(glm::vec3 v);

    // Dev: free-fly with no collision. Velocity is zeroed on exit.
    void setNoclip(bool on) { if (noclip_ && !on) vel_ = glm::vec3(0.0f); noclip_ = on; }
    bool noclip() const { return noclip_; }

    MoveState        state() const { return state_; }
    const MoveEvents& events() const { return events_; }
    glm::vec3        position() const { return pos_; }
    glm::vec3        velocity() const { return vel_; }
    float            horizontalSpeed() const { return glm::length(glm::vec2(vel_.x, vel_.z)); }
    bool             grounded() const { return grounded_; }

    glm::vec3 eyePosition(float alpha) const;
    float     cameraRoll(float alpha) const;

private:
    struct Frame {
        float     dt = 0.0f;
        MoveInput in;
        glm::vec2 right{1.0f, 0.0f};
        glm::vec2 wishDir{0.0f};
        float     wishMag = 0.0f;
        bool      landed  = false;
    };

    void groundJump(glm::vec2& h);
    void groundMove(const Frame& f, glm::vec2& h);
    void airMove(const Frame& f, glm::vec2& h);
    bool updateWallRun(const Frame& f, glm::vec2& h);
    void updateCamera(const Frame& f);
    bool tryVault(const Frame& f, const glm::vec2& h);
    void updateVault(const Frame& f);

    const World&   world_;
    MovementConfig cfg_;

    glm::vec3 spawn_{0.0f, 0.0f, 6.0f};
    glm::vec3 pos_{0.0f, 0.0f, 6.0f};
    glm::vec3 prevPos_{0.0f, 0.0f, 6.0f};
    glm::vec3 vel_{0.0f};
    glm::vec3 groundNormal_{0.0f, 1.0f, 0.0f};
    glm::vec2 wallNormal_{0.0f};

    MoveState  state_      = MoveState::Walking;
    MoveEvents events_{};
    bool       grounded_   = true;
    bool       justLanded_ = false;
    bool       noclip_     = false;

    float coyote_        = 0.0f;
    float jumpBuffer_    = 0.0f;
    float slideCooldown_ = 0.0f;
    float wallCooldown_  = 0.0f;
    float wallTimer_     = 0.0f;
    float vaultCooldown_ = 0.0f;
    float vaultT_        = 0.0f;
    float vaultDur_      = 0.3f;
    glm::vec3 vaultStart_{0.0f}, vaultEnd_{0.0f};
    glm::vec2 vaultDir_{0.0f};
    float vaultSpeed_    = 0.0f;
    int   slideChain_    = 0;
    float chainTimer_    = 0.0f;

    float eye_ = 1.7f, prevEye_ = 1.7f;
    float roll_ = 0.0f, prevRoll_ = 0.0f;
};

} // namespace carnage
