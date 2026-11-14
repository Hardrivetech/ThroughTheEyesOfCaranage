#version 460 core
// Loaded meshes (gfx::GpuMesh layout) with per-draw colour and material.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMat;
uniform vec3 uColor;
uniform int  uMaterial;

out vec3     vWorld;
out vec3     vNormal;
out vec3     vColor;
out vec3     vNoise;
flat out int vMat;

void main() {
    vec4 w  = uModel * vec4(aPos, 1.0);
    vWorld  = w.xyz;
    vNormal = uNormalMat * aNormal;
    vColor  = uColor;
    vNoise  = w.xyz;
    vMat    = uMaterial;
    gl_Position = uViewProj * w;
}
