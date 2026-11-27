#include "game/Hud.hpp"

#include "ui/Text.hpp"

#include <string>

#include <algorithm>
#include <cmath>

namespace carnage {

namespace {

// Palette: crimson #E63946, amber #F4A261, steel #1D2228.
constexpr glm::vec4 kCrimson{0.902f, 0.224f, 0.275f, 1.0f};
constexpr glm::vec4 kAmber{0.957f, 0.635f, 0.380f, 1.0f};
constexpr glm::vec4 kSteel{0.114f, 0.133f, 0.157f, 1.0f};

glm::vec4 withAlpha(glm::vec4 c, float a) { c.a = a; return c; }

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

namespace {

// Sharp L-shaped brackets at the four corners of [lo, hi] (NDC).
void bracketCorners(glm::vec2 lo, glm::vec2 hi, float aspect, glm::vec4 c, std::vector<OverlayRect>& out) {
    const float t = 0.0035f, lx = 0.014f / aspect * 2.0f, ly = 0.020f, tx = t / aspect * 2.0f;
    out.push_back({{lo.x, hi.y - t}, {lo.x + lx, hi.y}, c});  out.push_back({{lo.x, hi.y - ly}, {lo.x + tx, hi.y}, c});
    out.push_back({{hi.x - lx, hi.y - t}, {hi.x, hi.y}, c});  out.push_back({{hi.x - tx, hi.y - ly}, {hi.x, hi.y}, c});
    out.push_back({{lo.x, lo.y}, {lo.x + lx, lo.y + t}, c});  out.push_back({{lo.x, lo.y}, {lo.x + tx, lo.y + ly}, c});
    out.push_back({{hi.x - lx, lo.y}, {hi.x, lo.y + t}, c});  out.push_back({{hi.x - tx, lo.y}, {hi.x, lo.y + ly}, c});
}

void centredText(std::string_view text, float y, float px, float aspect, glm::vec4 c, std::vector<OverlayRect>& out) {
    const float w = ui::textWidth(text, px, aspect);
    ui::drawText(out, text, {-w * 0.5f + 0.0015f / aspect, y - 0.0015f}, px, aspect, {0, 0, 0, 0.55f * c.a});
    ui::drawText(out, text, {-w * 0.5f, y}, px, aspect, c);
}

} // namespace

void Hud::momentum(const MomentumGauge& g, float aspect, std::vector<OverlayRect>& out) {
    const float w = 0.40f / aspect * 2.0f, y0 = -0.845f, h = 0.016f;
    const float x0 = -w * 0.5f;
    const auto at = [&](float m) { return x0 + w * std::clamp(m, 0.0f, 1.0f); };

    out.push_back({{x0, y0}, {x0 + w, y0 + h}, withAlpha(kSteel, 0.85f)});

    glm::vec4 fill = withAlpha(kAmber, 0.55f);                       // building up
    if (g.tier == MomentumTier::Sprint)    fill = withAlpha(kAmber, 0.95f);
    if (g.tier == MomentumTier::Overdrive) fill = withAlpha(kCrimson, 0.98f);
    if (g.fill > 0.0f) out.push_back({{x0, y0}, {at(g.fill), y0 + h}, fill});

    // Ticks: sprint (amber, tall), wall-run (white, short), speed-line threshold (crimson, tall).
    const float tw = 0.0016f / aspect * 2.0f;
    out.push_back({{at(g.sprintMark) - tw, y0 - 0.006f}, {at(g.sprintMark) + tw, y0 + h + 0.006f}, kAmber});
    out.push_back({{at(g.wallRunMark) - tw, y0 + h}, {at(g.wallRunMark) + tw, y0 + h + 0.006f}, {0.92f, 0.92f, 0.94f, 0.9f}});
    out.push_back({{at(g.overdriveMark) - tw, y0 - 0.006f}, {at(g.overdriveMark) + tw, y0 + h + 0.006f}, kCrimson});

    bracketCorners({x0 - 0.006f / aspect * 2.0f, y0 - 0.008f}, {x0 + w + 0.006f / aspect * 2.0f, y0 + h + 0.008f}, aspect,
                   withAlpha(kAmber, 0.8f), out);

    const char* label = "";
    switch (g.tier) {
        case MomentumTier::Sprint:    label = "SPRINT"; break;
        case MomentumTier::Overdrive: label = "OVERDRIVE"; break;
        default: break;
    }
    if (*label) {
        constexpr float px = 0.0045f;
        const float tw2 = ui::textWidth(label, px, aspect);
        ui::drawText(out, label, {x0 + w - tw2, y0 + h + 0.050f}, px, aspect,
                     g.tier == MomentumTier::Overdrive ? kCrimson : kAmber);
    }
}

void Hud::seize(const SeizeHud& s, std::string_view key, float aspect, std::vector<OverlayRect>& out) {
    if (s.state == SeizeState::None) return;
    const std::string keyText = "[" + std::string(key) + "] ";

    if (s.state == SeizeState::CanSeize) {
        const std::string text = keyText + "SEIZE";
        constexpr float px = 0.0075f;
        const float w = ui::textWidth(text, px, aspect);
        bracketCorners({-w * 0.5f - 0.03f / aspect, -0.335f}, {w * 0.5f + 0.03f / aspect, -0.27f}, aspect, withAlpha(kAmber, 0.9f), out);
        centredText(text, -0.30f, px, aspect, withAlpha(kAmber, 0.98f), out);
        return;
    }

    // Holding a human shield: integrity bar, then the throw prompt.
    const float w = 0.22f / aspect * 2.0f, y = -0.335f, h = 0.014f;
    const glm::vec4 bar = s.shieldWeak ? kCrimson : kAmber;
    centredText("HUMAN SHIELD", y + 0.062f, 0.0050f, aspect, withAlpha(bar, 0.95f), out);
    out.push_back({{-w * 0.5f, y}, {w * 0.5f, y + h}, withAlpha(kSteel, 0.9f)});
    if (s.shield01 > 0.0f) out.push_back({{-w * 0.5f, y}, {-w * 0.5f + w * s.shield01, y + h}, withAlpha(bar, 0.95f)});
    bracketCorners({-w * 0.5f - 0.005f / aspect * 2.0f, y - 0.007f}, {w * 0.5f + 0.005f / aspect * 2.0f, y + h + 0.007f}, aspect,
                   withAlpha(bar, 0.85f), out);

    const std::string text = keyText + "THROW";
    centredText(text, y - 0.040f, 0.0072f, aspect, {1.0f, 1.0f, 1.0f, 0.95f}, out);

    // The ready pip fills over the first moments of the hold, then shows READY (or BREAKING).
    const float pw = 0.07f / aspect * 2.0f, py = y - 0.125f, ph = 0.012f;
    const char* state = !s.throwReady ? "NO SHIELD" : (s.grip01 < 1.0f ? "GRIP" : (s.shieldWeak ? "BREAKING" : "READY"));
    out.push_back({{-pw * 0.5f, py}, {pw * 0.5f, py + ph}, withAlpha(kSteel, 0.9f)});
    out.push_back({{-pw * 0.5f, py}, {-pw * 0.5f + pw * s.grip01, py + ph}, withAlpha(s.shieldWeak ? kCrimson : kAmber, 0.95f)});
    centredText(state, py - 0.012f, 0.0050f, aspect, withAlpha(s.shieldWeak ? kCrimson : kAmber, 0.9f), out);
}

void Hud::damageArcs(std::span<const DamageArc> arcs, float aspect, std::vector<OverlayRect>& out) {
    constexpr float kRadius = 0.20f, kSpread = 0.34f;
    constexpr int   kDots = 9;
    for (const DamageArc& a : arcs) {
        const float alpha = DamageIndicators::alpha(a);
        if (alpha <= 0.0f) continue;
        for (int i = 0; i < kDots; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(kDots - 1) * 2.0f - 1.0f;   // -1..1 across the arc
            const float ang = a.angle + t * kSpread;
            const float edge = 1.0f - 0.55f * std::abs(t);                                          // brighter in the middle
            for (const float r : {kRadius, kRadius + 0.013f}) {
                out.push_back(rect({std::sin(ang) * r / aspect, std::cos(ang) * r}, {0.0042f, 0.0042f}, aspect,
                                   withAlpha(kCrimson, std::min(1.0f, alpha * edge))));
            }
        }
        // A bright pointer just outside the arc's middle.
        const float pr = kRadius + 0.035f;
        out.push_back(rect({std::sin(a.angle) * pr / aspect, std::cos(a.angle) * pr}, {0.0065f, 0.0065f}, aspect,
                           withAlpha({1.0f, 0.55f, 0.55f, 1.0f}, alpha)));
    }
}

void Hud::crosshairWarning(const CrosshairWarning& w, float aspect, std::vector<OverlayRect>& out) {
    if (!w.active) return;
    const float r = 0.052f + 0.012f * w.pulse;   // the brackets breathe with the heartbeat
    bracketCorners({-r / aspect, -r}, {r / aspect, r}, aspect, withAlpha(kCrimson, 0.45f + 0.5f * w.pulse), out);
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
