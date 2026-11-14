#pragma once
#include "core/DrawTypes.hpp"
#include "core/Material.hpp"
#include "game/Movement.hpp"

#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace carnage::gfx {

struct HumanoidPose {
    glm::vec3 position{0.0f};
    float     yaw   = 0.0f;
    float     pitch = 0.0f;
    float     time  = 0.0f;
    float     speed = 0.0f;
    MoveState state = MoveState::Walking;
    bool      hideHead = false;
};

// Builds procedurally animated anatomical body components (Torso, Shoulders, Arms,
// Pelvis, Legs, Boots, Head) for the player character. Placed in the world for
// self-shadowing and first-person leg visibility.
void buildPlayerBodyBoxes(const HumanoidPose& pose, std::vector<DrawBox>& out);

// Builds realistic procedural viewmodel arms and fists in camera space.
// Animates fist punches (alternating jab/hook) or two-handed tactical firearm grips.
void buildViewmodelArmsAndFists(std::string_view weaponName, float punchProgress,
                                int punchSide, float kick, float reloadProgress,
                                std::vector<DrawBox>& out, bool leftHandFree = true);

} // namespace carnage::gfx
