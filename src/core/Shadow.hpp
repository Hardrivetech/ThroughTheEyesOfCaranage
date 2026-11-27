#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace carnage {

// The sun's shadow map: a square orthographic box that follows the player. Pure maths
// (no GL), so the properties that make shadows look right can be tested:
//   * the box is snapped to whole shadow texels, so edges don't crawl or shimmer as the
//     camera moves;
//   * the depth range grows with the box so tall things still cast;
//   * the depth bias is a fixed number of texels in world space, so it stays correct when
//     the box is resized (a fixed value in depth units would not).
inline constexpr int   kShadowMapSize     = 4096;
inline constexpr float kShadowBiasTexels  = 5.89824f; // depth bias, in shadow texels (exactly 0.0006 at the default 50 m box)
inline constexpr float kShadowMinDepth    = 120.0f;  // half-depth is 2.4 * radius, but never less than this (metres)

// Written as radius * 12 / 5 (not radius * 2.4f) so that the default 50 m box gives exactly 120.
inline float shadowDepthHalf(float radius) { return std::max(kShadowMinDepth, radius * 12.0f / 5.0f); }
inline float shadowTexel(float radius, int mapSize) { return 2.0f * radius / static_cast<float>(mapSize); }

// Depth bias as a fraction of the depth range, for a shader that compares in [0,1].
inline float shadowBiasNdc(float radius, int mapSize) {
    return kShadowBiasTexels * shadowTexel(radius, mapSize) / (2.0f * shadowDepthHalf(radius));
}

struct SunShadow {
    glm::mat4 viewProj{1.0f};
    float     texel     = 0.0f;  // metres per shadow texel
    float     bias      = 0.0f;  // shader depth bias (see shadowBiasNdc)
    float     depthHalf = 0.0f;
};

// `sunDir` points towards the sun. The box is centred a little ahead of the camera.
inline SunShadow computeSunShadow(const glm::vec3& eye, const glm::vec3& forward, const glm::vec3& sunDir,
                                  float radius, int mapSize) {
    SunShadow s;
    s.texel     = shadowTexel(radius, mapSize);
    s.depthHalf = shadowDepthHalf(radius);
    s.bias      = shadowBiasNdc(radius, mapSize);

    const glm::vec3 centre    = eye + glm::vec3(forward.x, 0.0f, forward.z) * (radius * 0.4f);
    const glm::mat4 lightView = glm::lookAt(glm::vec3(0.0f), -sunDir, glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 c = glm::vec3(lightView * glm::vec4(centre, 1.0f));
    c.x = std::floor(c.x / s.texel) * s.texel;   // snap to texels: no shimmering
    c.y = std::floor(c.y / s.texel) * s.texel;

    const glm::mat4 proj = glm::ortho(c.x - radius, c.x + radius, c.y - radius, c.y + radius,
                                      -c.z - s.depthHalf, -c.z + s.depthHalf);
    s.viewProj = proj * lightView;
    return s;
}

} // namespace carnage
