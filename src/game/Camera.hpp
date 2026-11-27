#pragma once
#include "game/Trauma.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace carnage {

// First-person camera orientation. Yaw/pitch are stored as scalars (no
// accumulated roll, no gimbal flip) and composed into a quaternion on demand.
// Position comes from the movement controller.
class Camera {
public:
    float sensitivity = 0.0022f;  // radians per mouse count
    float fovDegrees  = 90.0f;
    float farPlane    = 300.0f;   // far clip distance (a level's Environment sets it)

    // Render rate: apply raw mouse delta (pixels) immediately for low latency.
    void look(glm::vec2 mouseDelta);

    float yaw() const { return yaw_; }

    // Faces `yawRadians` and levels the pitch (spawn / respawn). Same convention
    // as the movement controller: 0 faces -Z, +pi/2 faces -X.
    void face(float yawRadians);

    // Aim orientation. `recoil` = (pitch, yaw) offset from weapon recoil.
    glm::quat orientation(glm::vec2 recoil = glm::vec2(0.0f)) const;
    glm::vec3 forward() const;

    // `roll` leans the camera (radians, positive = head tilts left).
    glm::mat4 view(const glm::vec3& eye, const ShakeOffset& shake, float roll,
                   glm::vec2 recoil = glm::vec2(0.0f)) const;
    glm::mat4 projection(float aspect) const;

private:
    float yaw_   = 0.0f;
    float pitch_ = 0.0f;
};

} // namespace carnage
