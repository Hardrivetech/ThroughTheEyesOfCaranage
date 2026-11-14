#version 460 core
#include "common.glsl"

in vec3     vWorld;
in vec3     vNormal;
in vec3     vColor;
in vec3     vNoise;
flat in int vMat;

out vec4 fragColor;

const int MAX_LIGHTS = 8;

uniform vec3  uEye;
uniform vec3  uSunDir;     // unit vector pointing towards the sun
uniform vec3  uSunColor;   // HDR radiance
uniform float uHaze;       // distance haze density, 0 disables

uniform mat4            uLightViewProj;
uniform sampler2DShadow uShadowMap;
uniform float           uShadowTexel;  // world metres per shadow texel

uniform int  uLightCount;
uniform vec4 uLightPosR[MAX_LIGHTS];   // xyz position, w radius
uniform vec3 uLightCol[MAX_LIGHTS];

struct Surface {
    vec3  albedo;
    vec3  n;
    float rough;
    float metal;
    float ao;
    vec3  emissive;
};

// Perturbs n by a height-field gradient g (tangent-plane part only).
vec3 bump(vec3 n, vec3 g) {
    return normalize(n - (g - dot(g, n) * n));
}

Surface concrete(vec3 p, vec3 n, vec3 base) {
    Surface s;
    float big  = fbm(p * 0.7);
    float mid  = fbm(p * 3.0);
    float fine = vnoise(p * 18.0);

    float tone = mix(0.55, 1.15, big) * (0.85 + 0.3 * mid) * (0.9 + 0.2 * fine);

    // Board-formed seams on walls, expansion joints on floors and roofs.
    float seam;
    if (abs(n.y) < 0.5) {
        float b = p.y / 0.6;
        seam = smoothstep(0.0, 0.05, fract(b)) * smoothstep(1.0, 0.95, fract(b));
        tone *= 0.9 + 0.2 * hash13(vec3(floor(b), 3.0, 7.0));
    } else {
        vec2 g = abs(fract(p.xz / 4.0) - 0.5) * 4.0;
        seam = smoothstep(0.0, 0.06, 2.0 - g.x) * smoothstep(0.0, 0.06, 2.0 - g.y);
    }

    float stain = smoothstep(0.55, 0.85, fbm(vec3(p.x * 0.8 + p.z * 0.8, p.y * 0.15, p.z * 0.1 + 5.0)));
    float pits  = smoothstep(0.70, 0.85, vnoise(p * 9.0 + 3.0));

    s.albedo = base * tone * mix(0.55, 1.0, seam) * (1.0 - 0.5 * stain) * (1.0 - 0.45 * pits);
    s.rough  = clamp(0.88 - 0.12 * stain, 0.0, 1.0);
    s.metal  = 0.0;
    s.ao     = mix(0.65, 1.0, seam) * (1.0 - 0.5 * pits);
    s.n      = bump(n, noiseGrad(p, 6.0) * 0.10 + noiseGrad(p, 25.0) * 0.04);
    s.emissive = vec3(0.0);
    return s;
}

Surface tile(vec3 p, vec3 n, vec3 base) {
    Surface s;
    // Project onto the dominant plane so walls get tiles too.
    vec2 uv = abs(n.y) > 0.5 ? p.xz : (abs(n.x) > abs(n.z) ? p.zy : p.xy);
    vec2 cell = uv / 2.0;
    vec2 f    = abs(fract(cell) - 0.5) * 2.0;           // 0 at centre, 1 at seam
    float seam = smoothstep(0.965, 0.99, max(f.x, f.y));
    vec2  id   = floor(cell);
    float tint = hash13(vec3(id, 1.0));

    float grime = fbm(p * 1.3);
    float wet   = smoothstep(0.52, 0.66, fbm(vec3(p.x, 0.0, p.z) * 0.18 + 11.0));
    wet *= step(0.5, abs(n.y));

    vec3 tileCol = base * (0.75 + 0.35 * tint) * mix(0.55, 1.0, grime);
    s.albedo = mix(tileCol, vec3(0.04, 0.04, 0.045), seam);
    s.albedo *= mix(1.0, 0.45, wet);
    s.rough  = mix(mix(0.5, 0.8, grime), 0.07, wet);
    s.metal  = 0.0;
    s.ao     = mix(1.0, 0.55, seam);
    vec3 g   = noiseGrad(p, 30.0) * 0.015 * (1.0 - wet);
    s.n      = bump(n, g);
    s.emissive = vec3(0.0);
    return s;
}

Surface paintedMetal(vec3 p, vec3 n, vec3 paint) {
    Surface s;
    float wearLow  = fbm(p * 2.2);
    float wearHigh = vnoise(p * 14.0);
    // Wear collects towards the bottom and along random scratches.
    float wear = smoothstep(0.55, 0.78, wearLow * 0.8 + wearHigh * 0.35 + (1.0 - clamp(p.y / 8.0, 0.0, 1.0)) * 0.15);
    float rustMask = smoothstep(0.35, 0.8, fbm(p * 5.0 + 9.0));
    // Vertical drip streaks.
    float drip = smoothstep(0.6, 0.9, fbm(vec3(p.x * 6.0 + p.z * 6.0, p.y * 0.4, 2.0)));

    vec3 rust  = vec3(0.34, 0.14, 0.06) * (0.6 + 0.8 * vnoise(p * 20.0));
    vec3 steel = vec3(0.55, 0.55, 0.58);
    vec3 under = mix(steel, rust, rustMask);

    vec3 paintCol = paint * (0.8 + 0.3 * fbm(p * 1.1)) * (1.0 - 0.4 * drip);
    s.albedo = mix(paintCol, under, wear);
    s.metal  = mix(0.0, mix(1.0, 0.35, rustMask), wear);
    s.rough  = mix(0.45, mix(0.35, 0.8, rustMask), wear);
    s.ao     = 1.0 - 0.35 * drip;
    s.n      = bump(n, noiseGrad(p, 12.0) * 0.03 + noiseGrad(p, 40.0) * 0.02);
    s.emissive = vec3(0.0);
    return s;
}

Surface surfaceFor(int mat, vec3 p, vec3 n, vec3 col) {
    Surface s;
    if (mat == M_CONCRETE) return concrete(p, n, col);
    if (mat == M_TILE)     return tile(p, n, col);
    if (mat == M_PAINTED)  return paintedMetal(p, n, col);

    s.n = n;
    s.ao = 1.0;
    s.emissive = vec3(0.0);
    if (mat == M_STEEL) {
        float brushed = vnoise(vec3(p.x * 3.0, p.y * 90.0, p.z * 3.0));
        s.albedo = col * (0.85 + 0.2 * brushed);
        s.metal  = 1.0;
        s.rough  = 0.30 + 0.2 * brushed;
    } else if (mat == M_BRASS) {
        s.albedo = col * vec3(1.0, 0.8, 0.45);
        s.metal  = 1.0;
        s.rough  = 0.28;
    } else if (mat == M_FLESH) {
        s.albedo = col * (0.85 + 0.25 * fbm(p * 14.0));
        s.metal  = 0.0;
        s.rough  = 0.55;
    } else if (mat == M_SKIN) {
        float pore = vnoise(p * 45.0);
        s.albedo = col * (0.90 + 0.15 * pore);
        s.metal  = 0.0;
        s.rough  = 0.42 + 0.15 * pore;
        s.n      = bump(n, noiseGrad(p, 35.0) * 0.015);
    } else if (mat == M_CLOTH) {
        float weave = vnoise(vec3(p.x * 60.0, p.y * 60.0, p.z * 60.0));
        s.albedo = col * (0.85 + 0.20 * weave);
        s.metal  = 0.0;
        s.rough  = 0.75 + 0.15 * weave;
    } else if (mat == M_LEATHER) {
        float grain = fbm(p * 12.0);
        s.albedo = col * (0.75 + 0.35 * grain);
        s.metal  = 0.0;
        s.rough  = 0.38 + 0.20 * grain;
        s.n      = bump(n, noiseGrad(p, 20.0) * 0.025);
    } else {  // plastic
        s.albedo = col;
        s.metal  = 0.0;
        s.rough  = 0.45;
    }
    return s;
}

float sunShadow(vec3 pos, vec3 n) {
    vec3 offsetPos = pos + n * (uShadowTexel * 2.0);
    vec4 lp = uLightViewProj * vec4(offsetPos, 1.0);
    vec3 sc = lp.xyz / lp.w * 0.5 + 0.5;
    if (sc.z > 1.0 || sc.x < 0.0 || sc.x > 1.0 || sc.y < 0.0 || sc.y > 1.0) return 1.0;

    float texel = 1.0 / float(textureSize(uShadowMap, 0).x);
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(uShadowMap, vec3(sc.xy + vec2(x, y) * texel, sc.z - 0.0006));
    return lit / 9.0;
}

vec3 shade(Surface s, vec3 pos, vec3 V) {
    vec3 n  = s.n;
    float NdotV = max(dot(n, V), 1e-3);
    vec3 F0 = mix(vec3(0.04), s.albedo, s.metal);
    vec3 diffuseCol = s.albedo * (1.0 - s.metal);
    float rough = clamp(s.rough, 0.045, 1.0);

    // Direct: sun with shadows.
    vec3 Lo = vec3(0.0);
    {
        vec3  L = uSunDir;
        float NdotL = max(dot(n, L), 0.0);
        if (NdotL > 0.0) {
            vec3  H = normalize(V + L);
            float NdotH = max(dot(n, H), 0.0);
            float D = distGGX(NdotH, rough);
            float G = geoSmith(NdotV, NdotL, rough);
            vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
            vec3  spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
            vec3  kd = (1.0 - F) * (1.0 - s.metal);
            float sh = sunShadow(pos, normalize(vNormal));
            Lo += (kd * diffuseCol / PI + spec) * uSunColor * NdotL * sh;
        }
    }

    // Direct: dynamic point lights (muzzle flashes, explosions, sparks).
    for (int i = 0; i < uLightCount; ++i) {
        vec3  toL = uLightPosR[i].xyz - pos;
        float d2  = dot(toL, toL);
        float r   = uLightPosR[i].w;
        if (d2 >= r * r) continue;
        float d   = sqrt(d2);
        vec3  L   = toL / d;
        float NdotL = max(dot(n, L), 0.0);
        if (NdotL <= 0.0) continue;

        float w   = clamp(1.0 - pow(d / r, 4.0), 0.0, 1.0);
        float att = w * w / (d2 + 1.0);

        vec3  H = normalize(V + L);
        float D = distGGX(max(dot(n, H), 0.0), rough);
        float G = geoSmith(NdotV, NdotL, rough);
        vec3  F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3  spec = D * G * F / (4.0 * NdotV * NdotL + 1e-4);
        vec3  kd = (1.0 - F) * (1.0 - s.metal);
        Lo += (kd * diffuseCol / PI + spec) * uLightCol[i] * NdotL * att;
    }

    // Ambient from the procedural sky: diffuse from the normal, specular
    // from the reflection vector blurred towards the normal by roughness.
    vec3  R = reflect(-V, n);
    vec3  skyDiffuse = skyColor(n, uSunDir) * 0.5;
    vec3  skySpec    = skyColor(normalize(mix(R, n, rough * rough)), uSunDir);
    vec3  Fa = F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
    vec3  ambient = (1.0 - Fa) * (1.0 - s.metal) * diffuseCol * skyDiffuse
                  + Fa * skySpec * mix(0.15, 1.0, 1.0 - rough);

    return (Lo + ambient * s.ao) + s.emissive;
}

void main() {
    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 toEye = uEye - vWorld;
    float dist = length(toEye);
    vec3 V = toEye / max(dist, 1e-4);

    vec3 color;
    if (vMat == M_EMISSIVE) {
        color = vColor * 5.0;
    } else {
        Surface s = surfaceFor(vMat, vNoise, N, vColor);
        color = shade(s, vWorld, V);
    }

    if (uHaze > 0.0) {
        float f = 1.0 - exp(-dist * uHaze);
        color = mix(color, skyColor(-V, uSunDir) * 0.8, f);
    }
    fragColor = vec4(color, 1.0);
}
