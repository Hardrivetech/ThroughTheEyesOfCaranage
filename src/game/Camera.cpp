#include "game/Camera.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {
constexpr float     kNearPlane = 0.05f;
constexpr float     kMaxPitch = glm::radians(89.0f);
constexpr glm::vec3 kUp{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 kRight{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 kBack{0.0f, 0.0f, 1.0f};
} // namespace

void Camera::look(glm::vec2 d) {
    yaw_   -= d.x * sensitivity;                       // mouse right -> turn right
    pitch_ -= d.y * sensitivity;                       // mouse up    -> look up
    pitch_  = std::clamp(pitch_, -kMaxPitch, kMaxPitch);
    yaw_    = std::remainder(yaw_, glm::two_pi<float>());
}

void Camera::face(float yawRadians) {
    yaw_   = std::remainder(yawRadians, glm::two_pi<float>());
    pitch_ = 0.0f;
}

glm::quat Camera::orientation(glm::vec2 recoil) const {
    const float pitch = std::clamp(pitch_ + recoil.x, -kMaxPitch, kMaxPitch);
    return glm::angleAxis(yaw_ + recoil.y, kUp) * glm::angleAxis(pitch, kRight);
}

glm::vec3 Camera::forward() const {
    return orientation() * glm::vec3(0.0f, 0.0f, -1.0f);
}

glm::mat4 Camera::view(const glm::vec3& eye, const ShakeOffset& shake, float roll,
                       glm::vec2 recoil) const {
    const glm::quat base = orientation(recoil);
    const glm::quat q    = base * glm::quat(shake.rotation) * glm::angleAxis(roll, kBack);
    const glm::vec3 pos  = eye + base * shake.translation;
    return glm::inverse(glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(q));
}

glm::mat4 perspectiveReverseZ(float fovyRadians, float aspect, float nearPlane, float farPlane) {
    // Swapping the planes in the zero-to-one projection flips the depth range.
    return glm::perspectiveRH_ZO(fovyRadians, aspect, farPlane, nearPlane);
}

glm::mat4 Camera::projection(float aspect) const {
    return perspectiveReverseZ(glm::radians(fovDegrees), aspect, kNearPlane, farPlane);
}

} // namespace carnage
