#version 460 core
// Resolved HDR -> chromatic aberration, vignette, ACES tonemap, sRGB, grain.
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uHdr;
uniform float uTime;
uniform float uAberration;  // base + trauma-driven
uniform float uExposure;
uniform float uFlash;       // muzzle blind, 0..1

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

    c *= 1.0 - 0.55 * smoothstep(0.30, 0.95, length(d) * 1.4142);

    vec3 col = toSrgb(aces(c));

    float grain = hash12(gl_FragCoord.xy + fract(uTime) * 311.0) - 0.5;
    col += grain * 0.035;

    fragColor = vec4(col, 1.0);
}
