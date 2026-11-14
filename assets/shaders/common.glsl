// Shared helpers: procedural noise, sky model, PBR BRDF terms.
// Included after the #version line of each stage; must not contain one.

const float PI = 3.14159265359;

// Keep in sync with core/Material.hpp.
const int M_CONCRETE = 0;
const int M_TILE     = 1;
const int M_PAINTED  = 2;
const int M_STEEL    = 3;
const int M_BRASS    = 4;
const int M_FLESH    = 5;
const int M_PLASTIC  = 6;
const int M_EMISSIVE = 7;
const int M_SKIN     = 8;
const int M_CLOTH    = 9;
const int M_LEATHER  = 10;

// ---- noise ------------------------------------------------------------------

float hash13(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 31.32);
    return fract((p.x + p.y) * p.z);
}

float vnoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = hash13(i);
    float n100 = hash13(i + vec3(1.0, 0.0, 0.0));
    float n010 = hash13(i + vec3(0.0, 1.0, 0.0));
    float n110 = hash13(i + vec3(1.0, 1.0, 0.0));
    float n001 = hash13(i + vec3(0.0, 0.0, 1.0));
    float n101 = hash13(i + vec3(1.0, 0.0, 1.0));
    float n011 = hash13(i + vec3(0.0, 1.0, 1.0));
    float n111 = hash13(i + vec3(1.0, 1.0, 1.0));
    return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
               mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}

// 4 octaves, normalised to roughly [0, 1] with mean ~0.5.
float fbm(vec3 p) {
    float a = 0.5;
    float s = 0.0;
    for (int i = 0; i < 4; ++i) {
        s += a * vnoise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return s / 0.9375;
}

// Gradient of vnoise(p * freq) with respect to p (forward differences).
vec3 noiseGrad(vec3 p, float freq) {
    float e  = 0.5 / freq;
    float h0 = vnoise(p * freq);
    return vec3(vnoise((p + vec3(e, 0.0, 0.0)) * freq) - h0,
                vnoise((p + vec3(0.0, e, 0.0)) * freq) - h0,
                vnoise((p + vec3(0.0, 0.0, e)) * freq) - h0) / e;
}

// ---- sky --------------------------------------------------------------------

// Dusty, low-sun overcast. Used for the backdrop, ambient light, reflections
// and distance haze so they all agree.
vec3 skyColor(vec3 d, vec3 sunDir) {
    vec3 zenith  = vec3(0.10, 0.14, 0.22);
    vec3 horizon = vec3(0.62, 0.42, 0.34);
    vec3 ground  = vec3(0.07, 0.065, 0.06);

    vec3 col = mix(horizon, zenith, pow(clamp(d.y, 0.0, 1.0), 0.45));
    col = mix(col, ground, smoothstep(0.0, 0.25, -d.y));

    float sd = max(dot(d, sunDir), 0.0);
    col += vec3(1.0, 0.60, 0.30) * pow(sd, 8.0) * 0.5;
    col += vec3(1.0, 0.75, 0.50) * pow(sd, 256.0) * 20.0;
    return col;
}

// ---- PBR (Cook-Torrance: GGX / Smith-Schlick / Schlick) ----------------------

float distGGX(float NdotH, float rough) {
    float a  = rough * rough;
    float a2 = a * a;
    float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geoSmith(float NdotV, float NdotL, float rough) {
    float r = rough + 1.0;
    float k = r * r / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
