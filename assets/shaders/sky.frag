#version 460 core
#include "common.glsl"

in vec2 vUV;
out vec4 fragColor;

uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform vec3 uSunDir;

void main() {
    vec4 w = uInvViewProj * vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w - uEye);
    fragColor = vec4(skyColor(d, uSunDir), 1.0);
}
