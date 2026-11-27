#pragma once
#include "core/DrawTypes.hpp"
#include "game/Combat.hpp"
#include "game/World.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <optional>
#include <span>
#include <vector>

namespace carnage {

enum class PickupKind { Pipe, Bat, Barrel, Body };

struct KindInfo {
    const char* name;
    float       radius;       // physics sphere (centre height above rest surface)
    float       hitRadius;    // strike sphere when swung or thrown
    float       mass;         // kg-ish, scales kinetic damage
    float       baseDamage;
    float       swingSpeed;   // tip speed of a full swing, m/s
    float       swingTime;    // seconds for a whole swing
    float       throwSpeed;   // m/s before player velocity is added
};

const KindInfo& info(PickupKind k);

// A loose object in the world. Held objects live in Interact::held_ instead.
struct Pickup {
    PickupKind kind = PickupKind::Pipe;
    glm::vec3  pos{0.0f}, prevPos{0.0f}, vel{0.0f};
    glm::quat  rot{1.0f, 0.0f, 0.0f, 0.0f}, prevRot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3  spin{0.0f};      // angular velocity (rad/s, world axes)
    bool       thrown = false;  // dangerous while true
    float      life   = 0.0f;   // despawn timer for bodies (<= 0: permanent)
    bool       alive  = true;
};

struct InteractInput {
    bool grabPressed  = false;  // pick up, or hurl if already holding
    bool swingPressed = false;  // bludgeon with the held object
};

struct InteractContext {
    glm::vec3 eye{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 playerVel{0.0f};
};

struct InteractEvents {
    bool  grabbed   = false;
    bool  thrown    = false;
    bool  swung     = false;
    bool  hit       = false;
    bool  exploded  = false;
    float impact    = 0.0f;  // 0..1 strength for camera/trauma feedback
};

// Lightweight grab / hurl / bludgeon system. Damage comes from kinetic energy:
//   E = 0.5 * m * v^2,  v = swingSpeed * curve + max(0, playerVel . forward)
//   damage = baseDamage + kEnergy * E
class Interact {
public:
    void add(PickupKind kind, const glm::vec3& pos, float yaw = 0.0f);

    InteractEvents tick(float dt, const InteractInput& in, const InteractContext& ctx,
                        Combat& combat, const World& world);

    std::span<const Pickup> pickups() const { return pickups_; }

    bool holding() const { return held_.has_value(); }
    bool canGrab() const { return canGrab_; }
    const char* heldName() const { return held_ ? info(*held_).name : ""; }

    void collect(float alpha, std::vector<DrawBox>& out) const;
    // Camera-space boxes for the held object (left hand), incl. swing animation.
    void buildHeld(float alpha, std::vector<DrawBox>& out) const;

private:
    void stepPickup(Pickup& p, float dt, Combat& combat, const World& world,
                    InteractEvents& ev);
    std::optional<std::size_t> findGrabTarget(const InteractContext& ctx) const;
    void appendShape(const Pickup& p, const glm::vec3& pos, const glm::mat3& R,
                     std::vector<DrawBox>& out) const;

    std::vector<Pickup>       pickups_;
    std::optional<PickupKind> held_;
    bool  canGrab_   = false;
    bool  swinging_  = false;
    bool  hitDone_   = false;
    float swingT_    = 0.0f;   // 0..1
    float prevSwingT_ = 0.0f;
    float cooldown_  = 0.0f;
};

} // namespace carnage
