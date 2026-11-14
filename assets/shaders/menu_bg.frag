#version 460 core
// Main-menu backdrop. Uses assets/ui/menu_background.(png|jpg) when present
// (cover-fit, slow drift, vignette); otherwise a procedural red-lit corridor.
in vec2 vUV;
out vec4 fragColor;

uniform sampler2D uTex;
uniform int   uHasTex;
uniform float uTime;
uniform float uAspect;     // screen aspect
uniform float uImgAspect;  // image aspect

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

vec3 corridor(vec2 uv) {
    vec2 p = (uv - 0.5) * vec2(uAspect, 1.0) * 2.0;
    p += vec2(sin(uTime * 0.2) * 0.04, cos(uTime * 0.17) * 0.03);
    float r = max(abs(p.x), abs(p.y) * 1.6) + 1e-3;   // box tunnel distance
    float depth = 1.0 / r + uTime * 0.6;               // travel down the tunnel
    float ringIdx = floor(depth * 1.2);
    float ring = smoothstep(0.0, 0.12, fract(depth * 1.2)) *
                 smoothstep(1.0, 0.88, fract(depth * 1.2));
    float panel = 0.35 + 0.65 * ring;
    float grit  = hash(floor(p * 90.0 / r) + ringIdx);
    vec3 wall = vec3(0.10, 0.10, 0.115) * panel * (0.8 + 0.4 * grit);
    // Ceiling light strips flicker red.
    float strip = step(0.55, fract(depth * 0.6)) * step(0.0, p.y) * step(abs(p.x), 0.25 * (1.0 + 0.0 * r));
    float flick = 0.75 + 0.25 * sin(uTime * 3.0 + ringIdx * 1.7);
    wall += vec3(0.9, 0.1, 0.05) * strip * flick * 0.8;
    wall *= smoothstep(0.0, 0.7, r) * 1.4;              // fade into black at the vanishing point
    wall += vec3(0.5, 0.05, 0.03) * exp(-r * 6.0) * 0.5; // red glow at the end
    return wall;
}

void main() {
    vec3 col;
    if (uHasTex != 0) {
        // Cover fit: scale so the image fills the screen, crop the overflow.
        vec2 uv = vUV - 0.5;
        if (uAspect > uImgAspect) uv.y *= uImgAspect / uAspect;
        else                      uv.x *= uAspect / uImgAspect;
        uv *= 0.94;  // headroom for the drift
        uv += vec2(sin(uTime * 0.11), cos(uTime * 0.09)) * 0.025;
        uv += 0.5;
        col = texture(uTex, vec2(uv.x, 1.0 - uv.y)).rgb;
    } else {
        col = corridor(vUV);
    }
    // Vignette + dim so overlaid text stays readable.
    float v = smoothstep(1.15, 0.25, length((vUV - 0.5) * vec2(uAspect * 0.6, 1.0) * 1.4));
    col *= mix(0.35, 0.85, v);
    fragColor = vec4(col, 1.0);
}
