#include "game/Hud.hpp"

#include "ui/Text.hpp"

#include <string>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

// Rect centred at `c` (NDC) with half-extents given in NDC-height units, so
// squares stay square on any aspect ratio.
OverlayRect rect(glm::vec2 c, glm::vec2 halfH, float aspect, glm::vec4 color) {
    const glm::vec2 h{halfH.x / aspect, halfH.y};
    return {c - h, c + h, color};
}

} // namespace

// side = +1 draws bottom right (right hand), -1 bottom left (left hand).
void Hud::drawAmmo(const Weapon& w, float side, float aspect, std::vector<OverlayRect>& out) {
    const WeaponStats& s = w.stats();
    const int   mag  = std::min(s.magSize, 30);
    const float pipW = 0.006f, pipH = 0.022f, gap = 0.004f;
    const float x0   = 0.92f * side;
    const bool  empty = w.ammo() <= 0;
    for (int i = 0; i < mag; ++i) {
        const bool full = i < std::min(w.ammo(), mag);
        const glm::vec4 col = full ? glm::vec4(1.0f, 0.85f, 0.3f, 0.9f) : glm::vec4(0.3f, 0.3f, 0.3f, 0.5f);
        out.push_back(rect({x0 - side * static_cast<float>(i) * (pipW * 2.0f + gap) / aspect, -0.90f},
                           {pipW, pipH}, aspect, col));
    }
    const std::string num = std::to_string(w.ammo()) + "/" + std::to_string(s.magSize);
    constexpr float px = 0.0065f;
    const float tw = ui::textWidth(num, px, aspect);
    const float edge = 0.92f * side;
    const float x = side > 0.0f ? edge - tw : edge;
    ui::drawText(out, num, {x, -0.80f}, px, aspect,
                 empty ? glm::vec4(1.0f, 0.25f, 0.2f, 0.95f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.85f));
}

void Hud::build(const Weapon& w, const Weapon* left, const Effects& fx, const glm::mat4& viewProj, float aspect,
                std::vector<OverlayRect>& out) const {
    const WeaponStats& s = w.stats();

    // Muzzle tint across the whole screen.
    if (w.flashing())
        out.push_back({{-1.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 0.8f, 0.4f, 0.10f * w.flashT()}});

    // Projected impact flashes.
    for (const ScreenFlash& f : fx.flashes()) {
        const glm::vec4 clip = viewProj * glm::vec4(f.pos, 1.0f);
        if (clip.w <= 0.05f) continue;
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        if (std::abs(ndc.x) > 1.2f || std::abs(ndc.y) > 1.2f) continue;

        const float t     = std::clamp(f.life / f.maxLife, 0.0f, 1.0f);
        const float size  = f.size * (0.5f + 0.5f * t) / std::max(1.0f, clip.w * 0.15f);
        out.push_back(rect(ndc, {size, size}, aspect, {f.color.r, f.color.g, f.color.b, 0.8f * t}));
    }

    // Crosshair: four ticks that open up with recoil and the shot kick.
    const float open = 0.010f + w.recoil().current().x * 0.9f + w.kick(1.0f) * 0.006f;
    const float len  = 0.013f;
    const float th   = 0.0016f;
    const glm::vec4 white{1.0f, 1.0f, 1.0f, 0.85f};
    out.push_back(rect({0.0f, open + len}, {th, len}, aspect, white));
    out.push_back(rect({0.0f, -open - len}, {th, len}, aspect, white));
    out.push_back(rect({(open + len) * 1.0f / aspect, 0.0f}, {len, th}, aspect, white));
    out.push_back(rect({-(open + len) * 1.0f / aspect, 0.0f}, {len, th}, aspect, white));
    out.push_back(rect({0.0f, 0.0f}, {0.0016f, 0.0016f}, aspect, {1.0f, 1.0f, 1.0f, 0.6f}));

    // Hit marker: an X made of four small diagonal-ish blocks.
    if (hit_ > 0.0f) {
        const glm::vec4 c = kill_ ? glm::vec4(1.0f, 0.15f, 0.1f, hit_)
                                  : glm::vec4(1.0f, 1.0f, 1.0f, hit_);
        const float d = 0.022f + (1.0f - hit_) * 0.01f;
        for (const glm::vec2 dir : {glm::vec2(1, 1), glm::vec2(-1, 1), glm::vec2(1, -1), glm::vec2(-1, -1)}) {
            for (int i = 0; i < 3; ++i) {
                const float k = d + static_cast<float>(i) * 0.006f;
                out.push_back(rect({dir.x * k / aspect, dir.y * k}, {0.0022f, 0.0022f}, aspect, c));
            }
        }
    }

    // Right-hand ammo pips (bottom right) and reload bar.
    if (!s.infiniteAmmo) {
        drawAmmo(w, +1.0f, aspect, out);
        const float rp = w.reloadProgress();
        if (rp >= 0.0f) {
            const float width = 0.10f;
            out.push_back({{-width / aspect * 2.0f, -0.28f}, {width / aspect * 2.0f, -0.265f}, {0.0f, 0.0f, 0.0f, 0.5f}});
            out.push_back({{-width / aspect * 2.0f, -0.28f},
                           {(-width + 2.0f * width * rp) / aspect * 2.0f, -0.265f}, {1.0f, 0.85f, 0.3f, 0.9f}});
        }
    }

    // Left-hand ammo, mirrored at bottom left.
    if (left && !left->stats().infiniteAmmo) drawAmmo(*left, -1.0f, aspect, out);
}

void Hud::vitals(float health01, float armor01, float damage01, float fade, bool dead,
                 float aspect, std::vector<OverlayRect>& out) {
    // Damage vignette: four edge bands of red.
    if (damage01 > 0.0f) {
        const float a = 0.35f * std::clamp(damage01, 0.0f, 1.0f);
        const glm::vec4 red{0.8f, 0.02f, 0.02f, a};
        const glm::vec4 soft{0.8f, 0.02f, 0.02f, a * 0.5f};
        out.push_back({{-1.0f, -1.0f}, {1.0f, -0.80f}, red});
        out.push_back({{-1.0f, 0.80f}, {1.0f, 1.0f}, red});
        out.push_back({{-1.0f, -1.0f}, {-0.85f, 1.0f}, red});
        out.push_back({{0.85f, -1.0f}, {1.0f, 1.0f}, red});
        out.push_back({{-1.0f, -0.80f}, {1.0f, -0.65f}, soft});
        out.push_back({{-1.0f, 0.65f}, {1.0f, 0.80f}, soft});
    }

    if (!dead) {
        const float w = 0.30f / aspect * 2.0f, y0 = -0.96f, h = 0.022f;
        const bool  low = health01 < 0.3f;
        const glm::vec4 hc = low ? glm::vec4(1.0f, 0.15f, 0.1f, 0.9f) : glm::vec4(0.85f, 0.9f, 0.85f, 0.85f);
        out.push_back({{-w * 0.5f, y0}, {w * 0.5f, y0 + h}, {0.0f, 0.0f, 0.0f, 0.5f}});
        out.push_back({{-w * 0.5f, y0}, {-w * 0.5f + w * std::clamp(health01, 0.0f, 1.0f), y0 + h}, hc});
        out.push_back({{-w * 0.5f, y0 + h + 0.006f}, {w * 0.5f, y0 + h + 0.006f + h * 0.5f}, {0.0f, 0.0f, 0.0f, 0.5f}});
        out.push_back({{-w * 0.5f, y0 + h + 0.006f},
                       {-w * 0.5f + w * std::clamp(armor01, 0.0f, 1.0f), y0 + h + 0.006f + h * 0.5f},
                       {0.35f, 0.6f, 1.0f, 0.85f}});
    }

    if (fade > 0.0f) {
        out.push_back({{-1.0f, -1.0f}, {1.0f, 1.0f}, {0.12f * fade, 0.0f, 0.0f, std::clamp(fade, 0.0f, 1.0f)}});
        if (dead && fade > 0.6f) {
            const std::string text = "YOU DIED";
            constexpr float px = 0.016f;
            const float tw = ui::textWidth(text, px, aspect);
            ui::drawText(out, text, {-tw * 0.5f, 0.0f}, px, aspect, {0.9f, 0.1f, 0.08f, (fade - 0.6f) / 0.4f});
        }
    }
}

void Hud::objective(std::string_view text, float aspect, std::vector<OverlayRect>& out) {
    constexpr float px = 0.0060f;
    const float w = ui::textWidth(text, px, aspect);
    ui::drawText(out, text, {-w * 0.5f + 0.0015f / aspect, 0.88f - 0.0015f}, px, aspect, {0, 0, 0, 0.5f});
    ui::drawText(out, text, {-w * 0.5f, 0.88f}, px, aspect, {1.0f, 0.9f, 0.6f, 0.85f});
}

void Hud::tutorial(std::string_view text, float alpha, float aspect, std::vector<OverlayRect>& out) {
    if (alpha <= 0.0f || text.empty()) return;
    constexpr float px = 0.0065f, y = 0.66f, padX = 0.025f, padY = 0.018f;
    const float w = ui::textWidth(text, px, aspect);
    out.push_back({{-w * 0.5f - padX, y - 7.0f * px - padY}, {w * 0.5f + padX, y + padY},
                   {0.0f, 0.0f, 0.0f, 0.45f * alpha}});
    ui::drawText(out, text, {-w * 0.5f + 0.0015f / aspect, y - 0.0015f}, px, aspect, {0, 0, 0, 0.6f * alpha});
    ui::drawText(out, text, {-w * 0.5f, y}, px, aspect, {1.0f, 1.0f, 1.0f, 0.95f * alpha});
}

void Hud::prompt(std::string_view key, std::string_view verb, float aspect,
                 std::vector<OverlayRect>& out) {
    std::string text = "PRESS [";
    text += key;
    text += "] ";
    text += verb;
    constexpr float px = 0.0070f;
    const float w = ui::textWidth(text, px, aspect);
    // Soft shadow first, then the text, so it reads on bright and dark walls.
    ui::drawText(out, text, {-w * 0.5f + 0.0015f / aspect, -0.30f - 0.0015f}, px, aspect, {0, 0, 0, 0.45f});
    ui::drawText(out, text, {-w * 0.5f, -0.30f}, px, aspect, {1.0f, 1.0f, 1.0f, 0.65f});
}

} // namespace carnage
