#include "gfx/Humanoid.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace carnage::gfx {

namespace {

// Rotates a local bounding box into world orientation around a pivot point.
DrawBox makeOrientedBox(glm::vec3 center, glm::vec3 half, glm::quat rot,
                        glm::vec3 color, Material mat = Material::Cloth) {
    return DrawBox{center, half, glm::mat3_cast(rot), color, false, mat};
}

} // namespace

void buildPlayerBodyBoxes(const HumanoidPose& pose, std::vector<DrawBox>& out) {
    const glm::quat qYaw   = glm::angleAxis(pose.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::vec3 pos    = pose.position;
    const float     t      = pose.time;
    const float     speed  = pose.speed;

    // Tactical palette
    const glm::vec3 skinCol{0.82f, 0.64f, 0.52f};
    const glm::vec3 shirtCol{0.16f, 0.20f, 0.16f};
    const glm::vec3 pantsCol{0.12f, 0.14f, 0.18f};
    const glm::vec3 bootCol{0.08f, 0.08f, 0.10f};
    const glm::vec3 armorCol{0.22f, 0.24f, 0.26f};

    // Stance gait parameters
    float walkCycle = std::sin(t * (speed > 8.0f ? 14.0f : 9.0f)) * std::clamp(speed / 8.0f, 0.0f, 1.0f);
    if (pose.state == MoveState::Airborne) walkCycle = 0.2f;

    float legPitchL =  walkCycle * 0.55f;
    float legPitchR = -walkCycle * 0.55f;
    float armPitchL = -walkCycle * 0.45f;
    float armPitchR =  walkCycle * 0.45f;
    float bodyTilt  = 0.0f;

    if (pose.state == MoveState::Sliding) {
        bodyTilt  = glm::radians(-35.0f);
        legPitchL = glm::radians(75.0f);   // lead leg extended forward
        legPitchR = glm::radians(-40.0f);  // rear leg tucked under
    } else if (pose.state == MoveState::WallRunning) {
        bodyTilt  = glm::radians(10.0f);
        legPitchL = std::sin(t * 16.0f) * 0.6f;
        legPitchR = -legPitchL;
    }

    const glm::quat qBody = qYaw * glm::angleAxis(bodyTilt, glm::vec3(1.0f, 0.0f, 0.0f));

    // 1. Pelvis (Hip root)
    const glm::vec3 pelvisPos = pos + glm::vec3(0.0f, 0.90f, 0.0f);
    out.push_back(makeOrientedBox(pelvisPos, {0.16f, 0.09f, 0.11f}, qBody, pantsCol, Material::Cloth));

    // 2. Torso & Tactical Armor Vest
    const glm::vec3 torsoPos = pelvisPos + qBody * glm::vec3(0.0f, 0.26f, 0.0f);
    out.push_back(makeOrientedBox(torsoPos, {0.19f, 0.18f, 0.13f}, qBody, shirtCol, Material::Cloth));
    out.push_back(makeOrientedBox(torsoPos + qBody * glm::vec3(0.0f, 0.02f, 0.02f),
                                  {0.20f, 0.15f, 0.12f}, qBody, armorCol, Material::Steel));

    // 3. Head & Neck (skipped in first-person to prevent view clipping)
    if (!pose.hideHead) {
        const glm::vec3 neckPos = torsoPos + qBody * glm::vec3(0.0f, 0.22f, 0.0f);
        out.push_back(makeOrientedBox(neckPos, {0.06f, 0.04f, 0.06f}, qBody, skinCol, Material::Skin));
        const glm::vec3 headPos = neckPos + qBody * glm::vec3(0.0f, 0.14f, 0.0f);
        out.push_back(makeOrientedBox(headPos, {0.11f, 0.13f, 0.11f}, qBody, skinCol, Material::Skin));
        // Tactical visor / goggles
        out.push_back(makeOrientedBox(headPos + qBody * glm::vec3(0.0f, 0.02f, -0.10f),
                                      {0.09f, 0.035f, 0.03f}, qBody, armorCol, Material::Steel));
    }

    // 4. Left & Right Legs (Thighs, Shins, Boots)
    for (int side : {-1, 1}) {
        const float s = static_cast<float>(side);
        const float pitch = side < 0 ? legPitchL : legPitchR;
        const glm::quat qLeg = qBody * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

        const glm::vec3 hipOffset = qBody * glm::vec3(s * 0.11f, -0.05f, 0.0f);
        const glm::vec3 thighPos  = pelvisPos + hipOffset + qLeg * glm::vec3(0.0f, -0.22f, 0.0f);
        out.push_back(makeOrientedBox(thighPos, {0.08f, 0.20f, 0.08f}, qLeg, pantsCol, Material::Cloth));

        const glm::vec3 kneeOffset = qLeg * glm::vec3(0.0f, -0.42f, 0.0f);
        const glm::vec3 shinPos    = pelvisPos + hipOffset + kneeOffset;
        out.push_back(makeOrientedBox(shinPos, {0.07f, 0.20f, 0.07f}, qLeg, pantsCol, Material::Cloth));

        const glm::vec3 bootPos = shinPos + qLeg * glm::vec3(0.0f, -0.22f, -0.03f);
        out.push_back(makeOrientedBox(bootPos, {0.075f, 0.06f, 0.11f}, qLeg, bootCol, Material::Leather));
    }

    // 5. Left & Right Arms (Shoulders, Forearms, Hands)
    for (int side : {-1, 1}) {
        const float s = static_cast<float>(side);
        const float pitch = side < 0 ? armPitchL : armPitchR;
        const glm::quat qArm = qBody * glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

        const glm::vec3 shoulderLocal = glm::vec3(s * 0.23f, 0.15f, 0.0f);
        const glm::vec3 upperArmPos   = torsoPos + qBody * shoulderLocal + qArm * glm::vec3(0.0f, -0.15f, 0.0f);
        out.push_back(makeOrientedBox(upperArmPos, {0.065f, 0.14f, 0.065f}, qArm, shirtCol, Material::Cloth));

        const glm::vec3 forearmPos = upperArmPos + qArm * glm::vec3(0.0f, -0.26f, 0.0f);
        out.push_back(makeOrientedBox(forearmPos, {0.055f, 0.13f, 0.055f}, qArm, skinCol, Material::Skin));

        const glm::vec3 handPos = forearmPos + qArm * glm::vec3(0.0f, -0.15f, 0.0f);
        out.push_back(makeOrientedBox(handPos, {0.045f, 0.045f, 0.05f}, qArm, bootCol, Material::Leather));
    }
}

void buildViewmodelArmsAndFists(std::string_view weaponName, float punchProgress,
                                int punchSide, float kick, float reloadProgress,
                                std::vector<DrawBox>& out, bool leftHandFree) {
    const glm::vec3 skinCol{0.82f, 0.64f, 0.52f};
    const glm::vec3 gloveCol{0.10f, 0.10f, 0.12f};
    const glm::vec3 tapeCol{0.22f, 0.20f, 0.18f};

    if (weaponName == "Fists") {
        // --- MELEE FISTS MODE ---
        // Animate two tactical fists ready in a boxer guard position.
        const float p = punchProgress;
        const float strike = p > 0.0f ? std::sin(p * glm::pi<float>()) : 0.0f;

        for (int side = 0; side < 2; ++side) {
            const float s = side == 0 ? -1.0f : 1.0f;
            if (side == 0 && !leftHandFree) continue;
            const bool active = (side == punchSide) && (p > 0.0f);

            glm::vec3 pivot{s * 0.18f, -0.22f, -0.35f};
            float     extend = 0.0f;
            float     pitch  = 0.0f;
            float     yaw    = 0.0f;

            if (active) {
                extend = strike * 0.42f;                     // thrust forward 42cm
                pitch  = strike * (side == 0 ? 0.15f : 0.25f); // jab vs hook tilt
                yaw    = strike * (side == 0 ? 0.10f : -0.30f);
            } else {
                // Idle breathing guard motion
                extend = std::sin(kick * 2.0f) * 0.01f;
            }

            pivot.z -= extend;
            pivot.x += s * extend * 0.1f;
            pivot.y += extend * 0.05f;

            const glm::quat qRot = glm::angleAxis(yaw, glm::vec3(0, 1, 0)) *
                                   glm::angleAxis(pitch, glm::vec3(1, 0, 0));
            const glm::mat3 R = glm::mat3_cast(qRot);

            auto addPart = [&](glm::vec3 loc, glm::vec3 half, glm::vec3 col, Material mat) {
                out.push_back({pivot + R * loc, half, R, col, false, mat});
            };

            // Forearm (tape & skin)
            addPart({0.0f, -0.04f, 0.12f}, {0.045f, 0.045f, 0.12f}, tapeCol, Material::Cloth);
            addPart({0.0f, -0.02f, 0.02f}, {0.048f, 0.048f, 0.04f}, gloveCol, Material::Leather);

            // Fist Main Block (Palm & Knuckles)
            addPart({0.0f, 0.0f, -0.05f}, {0.048f, 0.045f, 0.045f}, skinCol, Material::Skin);
            // Knuckle reinforcement plate
            addPart({0.0f, 0.015f, -0.065f}, {0.046f, 0.02f, 0.025f}, gloveCol, Material::Leather);

            // Folded Fingers (Index, Middle, Ring, Pinky)
            for (int f = 0; f < 4; ++f) {
                const float fx = -0.033f + static_cast<float>(f) * 0.022f;
                addPart({fx, -0.018f, -0.09f}, {0.010f, 0.014f, 0.016f}, skinCol, Material::Skin);
            }
            // Wrapped Thumb
            addPart({s * 0.042f, -0.01f, -0.055f}, {0.014f, 0.016f, 0.022f}, gloveCol, Material::Leather);
        }
        return;
    }

    // --- FIREARM ARMS & GRIPS ---
    // Render left and right forearms holding firearms (Pistol, SMG, Shotgun, Rocket Launcher).
    const float dip = reloadProgress >= 0.0f ? std::sin(reloadProgress * glm::pi<float>()) : 0.0f;
    const glm::mat3 R = glm::mat3_cast(glm::angleAxis(dip * 0.35f, glm::vec3(1, 0, 0)));

    auto addArmBox = [&](glm::vec3 center, glm::vec3 half, glm::vec3 col, Material mat) {
        out.push_back({center, half, R, col, false, mat});
    };

    // Right Arm (Trigger Hand)
    addArmBox({0.19f, -0.28f - dip * 0.15f, -0.32f + kick * 0.04f},
              {0.042f, 0.042f, 0.14f}, tapeCol, Material::Cloth);
    addArmBox({0.18f, -0.22f - dip * 0.15f, -0.45f + kick * 0.05f},
              {0.040f, 0.040f, 0.04f}, gloveCol, Material::Leather);

    // Left Support Arm (Support Hand under handguard or frame)
    if (!leftHandFree) return;
    addArmBox({-0.19f, -0.29f - dip * 0.10f, -0.34f},
              {0.042f, 0.042f, 0.14f}, tapeCol, Material::Cloth);
    addArmBox({-0.12f, -0.24f - dip * 0.10f, -0.52f},
              {0.040f, 0.040f, 0.04f}, gloveCol, Material::Leather);
}

} // namespace carnage::gfx
