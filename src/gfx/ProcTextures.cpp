#include "gfx/ProcTextures.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace carnage::gfx {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

// ---- tileable noise ------------------------------------------------------------------------
// Lattice noise whose lattice wraps every `period` cells, so evaluating at u and u + 1 gives the
// same value. Everything is keyed off integer hashes, so results do not depend on the platform.

std::uint32_t mix32(std::uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

float lattice(int x, int y, std::uint32_t seed) {
    const std::uint32_t h = mix32(static_cast<std::uint32_t>(x) * 0x9E3779B1u ^
                                  mix32(static_cast<std::uint32_t>(y) * 0x85EBCA6Bu + seed));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);  // [0, 1)
}

int wrapInt(int a, int n) {
    const int r = a % n;
    return r < 0 ? r + n : r;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

float smoothstep(float a, float b, float x) {
    const float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float fract(float x) { return x - std::floor(x); }

// Value noise on a px x py lattice over the unit tile; (u, v) in [0, 1).
float pnoise(float u, float v, int px, int py, std::uint32_t seed) {
    const float x = u * static_cast<float>(px), y = v * static_cast<float>(py);
    const float fx = std::floor(x), fy = std::floor(y);
    const int   ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    float tx = x - fx, ty = y - fy;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    const int x0 = wrapInt(ix, px), x1 = wrapInt(ix + 1, px);
    const int y0 = wrapInt(iy, py), y1 = wrapInt(iy + 1, py);
    return lerp(lerp(lattice(x0, y0, seed), lattice(x1, y0, seed), tx),
                lerp(lattice(x0, y1, seed), lattice(x1, y1, seed), tx), ty);
}

// Fractal noise: `octaves` layers, each twice the frequency and half the weight, normalised to
// [0, 1]. `base` is the lattice size of the first octave.
float fbm(float u, float v, int base, int octaves, std::uint32_t seed) {
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int   period = base;
    for (int o = 0; o < octaves; ++o) {
        sum += amp * pnoise(u, v, period, period, seed + static_cast<std::uint32_t>(o) * 101u);
        norm += amp;
        amp *= 0.5f;
        period *= 2;
    }
    return sum / norm;
}

// Distance (in tile units) from `x` to the nearest multiple of 1 / count.
float distToGrid(float x, int count) {
    const float f = fract(x * static_cast<float>(count));
    return std::min(f, 1.0f - f) / static_cast<float>(count);
}

// ---- materials -----------------------------------------------------------------------------

struct Texel {
    glm::vec3 albedo{0.5f};  // linear
    float     height = 0.5f; // 0..1, drives the normal map
    float     rough  = 0.8f;
    float     metal  = 0.0f;
    float     ao     = 1.0f;
    float     glow   = 0.0f;
};

struct Generator {
    Texel (*texel)(float u, float v);
    float bump;  // normal-map strength
};

Texel concreteWorn(float u, float v) {
    const float big  = fbm(u, v, 4, 4, 11);
    const float mid  = fbm(u, v, 16, 3, 12);
    const float fine = pnoise(u, v, 128, 128, 13);
    const float tone = lerp(0.55f, 1.1f, big) * (0.85f + 0.3f * mid) * (0.92f + 0.16f * fine);

    // Poured panels: a groove every half tile in both directions (2 x 2 panels).
    const float groove = 1.0f - smoothstep(0.0f, 0.006f, std::min(distToGrid(u, 2), distToGrid(v, 2)));
    // Grime that ran down the face (stretched along v).
    const float streak = smoothstep(0.55f, 0.8f, pnoise(u, v, 24, 3, 14));
    const float pit    = smoothstep(0.72f, 0.85f, pnoise(u, v, 48, 48, 15));

    Texel t;
    t.albedo = glm::vec3(0.50f, 0.49f, 0.47f) * tone * (1.0f - 0.55f * groove) *
               (1.0f - 0.4f * streak) * (1.0f - 0.5f * pit);
    t.height = std::clamp(0.6f + 0.2f * mid + 0.1f * fine - 0.5f * groove - 0.35f * pit, 0.0f, 1.0f);
    t.rough  = std::clamp(0.88f + 0.08f * fine - 0.12f * streak, 0.0f, 1.0f);
    t.metal  = 0.0f;
    t.ao     = 1.0f - 0.6f * groove - 0.4f * pit;
    return t;
}

Texel metalRusted(float u, float v) {
    const float corr = 0.5f + 0.5f * std::sin(kTwoPi * u * 10.0f);  // 10 ridges across the tile
    const float fine = pnoise(u, v, 96, 96, 21);

    // Plates: a seam every half tile along v, with a row of rivets on each side of it.
    const float dSeam = distToGrid(v, 2);
    const float seam  = 1.0f - smoothstep(0.0f, 0.006f, dSeam);
    const float ru    = (fract(u * 16.0f) - 0.5f) / 16.0f;
    const float rivet = 1.0f - smoothstep(0.004f, 0.007f, std::hypot(ru, dSeam - 0.018f));

    // Rust creeps in from the seams and collects in blotches.
    const float edge = 1.0f - smoothstep(0.0f, 0.07f, dSeam);
    const float rust = std::clamp(smoothstep(0.60f, 0.78f, fbm(u, v, 8, 4, 22)) +
                                  edge * 0.6f * smoothstep(0.3f, 0.7f, fbm(u, v, 6, 3, 23)), 0.0f, 1.0f);

    const glm::vec3 steel = glm::vec3(0.42f, 0.44f, 0.47f) * (0.9f + 0.2f * corr);
    const glm::vec3 oxide = glm::vec3(0.36f, 0.15f, 0.06f) * (0.6f + 0.8f * fine);

    Texel t;
    t.albedo = glm::mix(steel, oxide, rust);
    t.height = std::clamp(0.5f + 0.25f * corr - 0.3f * seam + 0.25f * rivet + 0.05f * rust * fine, 0.0f, 1.0f);
    t.metal  = lerp(0.95f, 0.15f, rust);
    t.rough  = std::clamp(lerp(0.38f, 0.85f, rust) + 0.1f * fine, 0.0f, 1.0f);
    t.ao     = 1.0f - 0.5f * seam - 0.2f * rust;
    return t;
}

Texel asphaltWet(float u, float v) {
    const float grain     = pnoise(u, v, 160, 160, 31);
    const float grain2    = pnoise(u, v, 64, 64, 32);
    const float aggregate = smoothstep(0.65f, 0.9f, grain);

    // Cracks follow the contour where a smooth noise field crosses 0.5.
    const float crack  = 1.0f - smoothstep(0.0f, 0.05f, std::abs(fbm(u, v, 6, 3, 33) - 0.5f) * 2.0f);
    // Puddles fill the low spots: mirror-smooth and darker.
    const float puddle = smoothstep(0.52f, 0.62f, fbm(u, v, 3, 3, 34));

    Texel t;
    const float base = 0.04f + 0.03f * grain2 + 0.07f * aggregate;
    t.albedo = glm::vec3(base) * (1.0f - 0.6f * crack) * (1.0f - 0.45f * puddle);
    t.height = lerp(std::clamp(0.6f + 0.15f * grain2 + 0.1f * aggregate - 0.5f * crack, 0.0f, 1.0f), 0.45f, puddle);
    t.rough  = lerp(std::clamp(0.82f - 0.1f * aggregate + 0.13f * crack, 0.0f, 1.0f), 0.05f, puddle);
    t.metal  = 0.0f;
    t.ao     = 1.0f - 0.7f * crack;
    return t;
}

Texel snowPacked(float u, float v) {
    // Wind-scoured: ripples stretched along u, plus slow drifts.
    const float wind  = pnoise(u, v, 4, 24, 41);
    const float drift = fbm(u, v, 3, 4, 42);
    const float ice   = smoothstep(0.62f, 0.75f, fbm(u, v, 5, 3, 43));
    // Sparse glints: a few single texels per tile catch the light.
    const bool  glint = lattice(static_cast<int>(u * 256.0f), static_cast<int>(v * 256.0f), 44) > 0.992f;

    Texel t;
    t.albedo = glm::mix(glm::vec3(0.78f, 0.81f, 0.86f), glm::vec3(0.56f, 0.68f, 0.80f), ice * 0.7f) *
               (0.93f + 0.1f * drift) * (0.92f + 0.1f * wind);
    if (glint) t.albedo += glm::vec3(0.4f);
    t.height = std::clamp(0.5f + 0.3f * (wind - 0.5f) + 0.3f * (drift - 0.5f), 0.0f, 1.0f);
    t.rough  = glint ? 0.15f : lerp(0.65f, 0.2f, ice);
    t.metal  = 0.0f;
    t.ao     = 0.85f + 0.15f * t.height;
    return t;
}

Texel hazardTrim(float u, float v) {
    // Four diagonal stripes across the tile; they line up across the wrap because u + v is
    // multiplied by a whole number.
    const float s      = fract((u + v) * 4.0f);
    const float yellow = smoothstep(0.0f, 0.02f, s) * (1.0f - smoothstep(0.49f, 0.51f, s));

    const float dirt = fbm(u, v, 6, 3, 52);
    // Chipped paint shows bare steel.
    const float chip = smoothstep(0.62f, 0.72f, pnoise(u, v, 60, 60, 51)) * smoothstep(0.45f, 0.65f, fbm(u, v, 10, 4, 53));

    const glm::vec3 paint = glm::mix(glm::vec3(0.025f, 0.025f, 0.028f), glm::vec3(0.85f, 0.62f, 0.04f), yellow) *
                            (0.75f + 0.35f * dirt);
    Texel t;
    t.albedo = glm::mix(paint, glm::vec3(0.45f, 0.45f, 0.47f), chip);
    t.height = std::clamp(0.55f + 0.05f * dirt + 0.05f * yellow - 0.2f * chip, 0.0f, 1.0f);
    t.metal  = chip;
    t.rough  = lerp(0.5f + 0.2f * dirt, 0.4f, chip);
    t.ao     = 1.0f - 0.3f * chip;
    t.glow   = yellow * (1.0f - chip);
    return t;
}

Texel grateIndustrial(float u, float v) {
    // 8 x 8 perforations per tile: rounded-square holes in a galvanised plate.
    const float cu = fract(u * 8.0f) - 0.5f, cv = fract(v * 8.0f) - 0.5f;
    const float qx = std::max(std::abs(cu) - 0.22f, 0.0f), qy = std::max(std::abs(cv) - 0.22f, 0.0f);
    const float sd   = std::hypot(qx, qy) - 0.12f;                 // < 0 inside a hole
    const float hole = 1.0f - smoothstep(-0.015f, 0.015f, sd);
    const float bevel = smoothstep(0.0f, 0.06f, sd);
    const float wear = fbm(u, v, 8, 3, 61);

    Texel t;
    const glm::vec3 galvanised = glm::vec3(0.52f, 0.53f, 0.55f) * (0.8f + 0.3f * wear);
    t.albedo = glm::mix(galvanised, glm::vec3(0.015f), hole);
    t.height = lerp(0.7f + 0.2f * bevel, 0.0f, hole);
    t.metal  = lerp(0.9f, 0.0f, hole);
    t.rough  = lerp(0.4f + 0.25f * wear, 0.9f, hole);
    t.ao     = lerp(1.0f, 0.0f, hole);
    return t;
}

// Order matches the material enum (see texturedLayer).
constexpr Generator kGenerators[kTexturedMaterialCount] = {
    {concreteWorn, 3.0f},   // ConcreteWorn
    {metalRusted, 3.0f},    // MetalRusted
    {asphaltWet, 2.0f},     // AsphaltWet
    {snowPacked, 3.0f},     // SnowPacked
    {hazardTrim, 1.5f},     // HazardTrim
    {grateIndustrial, 4.0f} // GrateIndustrial
};

std::uint8_t toByte(float x) {
    return static_cast<std::uint8_t>(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f);
}

float linearToSrgb(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

} // namespace

TextureSet generateTextureSet(int layer, int size) {
    if (layer < 0 || layer >= kTexturedMaterialCount || size < 8 || size > 2048) return {};
    const Generator& m = kGenerators[layer];

    const std::size_t n = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    std::vector<Texel> texels(n);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            texels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
                m.texel((static_cast<float>(x) + 0.5f) / static_cast<float>(size),
                        (static_cast<float>(y) + 0.5f) / static_cast<float>(size));

    TextureSet set;
    set.size = size;
    set.albedo.resize(n * 4);
    set.normal.resize(n * 4);
    set.mask.resize(n * 4);

    const auto at = [&](int x, int y) -> const Texel& {
        return texels[static_cast<std::size_t>(wrapInt(y, size)) * static_cast<std::size_t>(size) +
                      static_cast<std::size_t>(wrapInt(x, size))];
    };
    // Slope per texel scales with the size, so the same strength gives the same look at any size.
    const float k = m.bump * static_cast<float>(size) / 256.0f;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)) * 4;
            const Texel& t = at(x, y);

            // Central differences with wrap-around, so the normals tile as well.
            const float dx = (at(x + 1, y).height - at(x - 1, y).height) * 0.5f;
            const float dy = (at(x, y + 1).height - at(x, y - 1).height) * 0.5f;
            const glm::vec3 nrm = glm::normalize(glm::vec3(-dx * k, -dy * k, 1.0f));

            set.albedo[i + 0] = toByte(linearToSrgb(t.albedo.r));
            set.albedo[i + 1] = toByte(linearToSrgb(t.albedo.g));
            set.albedo[i + 2] = toByte(linearToSrgb(t.albedo.b));
            set.albedo[i + 3] = 255;

            set.normal[i + 0] = toByte(nrm.x * 0.5f + 0.5f);
            set.normal[i + 1] = toByte(nrm.y * 0.5f + 0.5f);
            set.normal[i + 2] = toByte(nrm.z * 0.5f + 0.5f);
            set.normal[i + 3] = toByte(t.height);

            set.mask[i + 0] = toByte(t.rough);
            set.mask[i + 1] = toByte(t.metal);
            set.mask[i + 2] = toByte(t.ao);
            set.mask[i + 3] = toByte(t.glow);
        }
    }
    return set;
}

std::vector<std::uint8_t> resampleRGBA(const std::uint8_t* src, int sw, int sh, int dstSize) {
    if (!src || sw <= 0 || sh <= 0 || dstSize < 1 || dstSize > 8192) return {};
    std::vector<std::uint8_t> out(static_cast<std::size_t>(dstSize) * static_cast<std::size_t>(dstSize) * 4);
    const auto px = [&](int x, int y, int c) {
        return static_cast<float>(src[(static_cast<std::size_t>(wrapInt(y, sh)) * static_cast<std::size_t>(sw) +
                                       static_cast<std::size_t>(wrapInt(x, sw))) * 4 + static_cast<std::size_t>(c)]);
    };
    for (int y = 0; y < dstSize; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) * static_cast<float>(sh) / static_cast<float>(dstSize) - 0.5f;
        const int   y0 = static_cast<int>(std::floor(fy));
        const float ty = fy - static_cast<float>(y0);
        for (int x = 0; x < dstSize; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) * static_cast<float>(sw) / static_cast<float>(dstSize) - 0.5f;
            const int   x0 = static_cast<int>(std::floor(fx));
            const float tx = fx - static_cast<float>(x0);
            for (int c = 0; c < 4; ++c) {
                const float v = lerp(lerp(px(x0, y0, c), px(x0 + 1, y0, c), tx),
                                     lerp(px(x0, y0 + 1, c), px(x0 + 1, y0 + 1, c), tx), ty);
                out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(dstSize) + static_cast<std::size_t>(x)) * 4 +
                    static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
            }
        }
    }
    return out;
}

} // namespace carnage::gfx
