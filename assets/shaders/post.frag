#version 460 core
// Resolved HDR -> chromatic aberration, vignette, ACES tonemap, sRGB, grain.
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uHdr;
uniform float uTime;
uniform float uAberration;  // base + trauma-driven
uniform float uExposure;
uniform float uFlash;       // muzzle blind, 0..1
uniform float uSpeedLines;  // 0..1: radial streaks at the screen edge while moving fast
uniform float uCrimson;     // 0..1: red vignette while the camera is shaken (damage, impacts)

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 toSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2  d  = vUV - 0.5;
    float r2 = dot(d, d);
    float ca = uAberration * (0.3 + r2 * 4.0);

    vec3 c;
    c.r = texture(uHdr, vUV - d * ca).r;
    c.g = texture(uHdr, vUV).g;
    c.b = texture(uHdr, vUV + d * ca).b;
    c  *= uExposure;

    // Muzzle blind: warm centre-weighted blow-out plus desaturation.
    c = mix(c, vec3(dot(c, vec3(0.333))), uFlash * 0.6);
    c += uFlash * vec3(1.0, 0.82, 0.55) * 3.0 * (1.2 - length(d));

    float vig = smoothstep(0.30, 0.95, length(d) * 1.4142);
    c *= 1.0 - 0.55 * vig;
    // Trauma bleeds the vignette crimson, strongest at the edges.
    c = mix(c, c * vec3(1.0, 0.30, 0.25) + vec3(0.28, 0.0, 0.015) * vig, uCrimson * vig * 0.65);

    vec3 col = toSrgb(aces(c));

    // Speed lines: streaks around the edge that re-roll about 24 times a second so they crawl.
    if (uSpeedLines > 0.0) {
        const float kStreaks = 96.0;
        float seg    = (atan(d.y, d.x) / 6.2831853 + 0.5) * kStreaks;
        float roll   = floor(uTime * 24.0);
        float streak = smoothstep(0.55, 1.0, hash12(vec2(floor(seg), roll)));
        streak      *= smoothstep(0.0, 0.5, 1.0 - abs(fract(seg) - 0.5) * 2.0);   // thin across its width
        float radial = smoothstep(0.45, 1.0, length(d) * 1.4142);
        col += vec3(0.80, 0.90, 1.0) * streak * radial * uSpeedLines * 0.28;
    }

    float grain = hash12(gl_FragCoord.xy + fract(uTime) * 311.0) - 0.5;
    col += grain * 0.035;

    fragColor = vec4(col, 1.0);
}
