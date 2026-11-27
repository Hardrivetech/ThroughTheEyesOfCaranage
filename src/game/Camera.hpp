#pragma once
#include "game/Trauma.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace carnage {

// Perspective projection for a reversed depth buffer, clip space z in 0..1 (the renderer sets
// glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE) and tests with GL_GREATER). Depth is 1 at the near
// plane and 0 at the far plane. A float depth buffer has most of its precision near 0, and
// reversing the range puts that precision where the standard mapping runs out: at 1 km with a
// 0.05 m near plane, the old projection could not tell surfaces under about 1 m apart, this one
// separates a few centimetres. x, y and w match glm::perspective exactly, so nothing moves on
// screen.
glm::mat4 perspectiveReverseZ(float fovyRadians, float aspect, float nearPlane, float farPlane);

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
