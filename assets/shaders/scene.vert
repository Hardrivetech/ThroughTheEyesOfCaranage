#version 460 core
// Level geometry and dynamic boxes (renderer's SceneVertex layout).
layout(location = 0) in vec3  aPos;
layout(location = 1) in vec3  aNormal;
layout(location = 2) in vec3  aColor;
layout(location = 3) in float aMat;

uniform mat4 uViewProj;
uniform mat4 uModel;        // identity, except camera space -> world for the viewmodel
uniform int  uObjectNoise;  // 1: grit pattern is fixed to the object (viewmodel)

out vec3     vWorld;
out vec3     vNormal;
out vec3     vColor;
out vec3     vNoise;
flat out int vMat;

void main() {
    vec4 w  = uModel * vec4(aPos, 1.0);
    vWorld  = w.xyz;
    vNormal = mat3(uModel) * aNormal;
    vColor  = aColor;
    vNoise  = uObjectNoise != 0 ? aPos : w.xyz;
    vMat    = int(aMat + 0.5);
    gl_Position = uViewProj * w;
}
