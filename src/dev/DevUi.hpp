#pragma once
// Dev overlay drawing: console panel, dev menu, perf readout. Emits OverlayRects
// only; no GL, no game state beyond DevState/FrameProfile.
#include "core/DrawTypes.hpp"
#include "dev/Console.hpp"
#include "dev/DevState.hpp"
#include "ui/Text.hpp"

#include <glm/glm.hpp>

#include <array>
#include <string>
#include <vector>

namespace carnage::dev {

inline void drawConsole(const Console& c, float aspect, bool caretOn, std::vector<OverlayRect>& out) {
    constexpr float px = 0.0065f, lh = 0.062f, top = 1.0f, bottom = 0.30f;
    out.push_back({{-1.0f, bottom}, {1.0f, top}, {0.02f, 0.02f, 0.03f, 0.80f}});
    out.push_back({{-1.0f, bottom - 0.004f}, {1.0f, bottom}, {0.85f, 0.12f, 0.10f, 0.95f}});

    const float inputTop = bottom + 0.065f;
    std::string line = "> " + c.input() + (caretOn ? "_" : " ");
    ui::drawText(out, line, {-0.97f, inputTop}, px, aspect, {1.0f, 0.9f, 0.5f, 1.0f});

    const auto& log  = c.log();
    const int   rows = static_cast<int>((top - 0.03f - inputTop) / lh);
    const int   n    = std::min<int>(rows, static_cast<int>(log.size()));
    for (int i = 0; i < n; ++i) {
        const LogLine& l = log[log.size() - 1 - static_cast<std::size_t>(i)];
        const std::string_view t = std::string_view(l.text).substr(0, 110);
        const glm::vec4 col = l.error ? glm::vec4(1.0f, 0.35f, 0.3f, 1.0f)
                                      : glm::vec4(0.8f, 0.85f, 0.8f, 1.0f);
        ui::drawText(out, t, {-0.97f, inputTop + static_cast<float>(i + 1) * lh}, px, aspect, col);
    }
}

// ---- dev menu ------------------------------------------------------------
// Each button runs a console command, so the menu and console share one path.
struct MenuItem {
    const char* label;
    const char* cmd;
    const bool* on;  // toggle state, or null for a plain button
};

inline std::array<MenuItem, 16> menuItems(const DevState& d) {
    return {{
        {"GOD MODE", "god", &d.god},
        {"INFINITE AMMO", "infammo", &d.infAmmo},
        {"NOCLIP", "noclip", &d.noclip},
        {"FREEZE AI", "freeze", &d.freezeAI},
        {"PERF STATS", "stats", &d.showStats},
        {"HEAL", "heal", nullptr},
        {"REFILL AMMO", "ammo", nullptr},
        {"KILL ALL", "kill_all", nullptr},
        {"SPAWN MELEE", "spawn melee", nullptr},
        {"SPAWN RANGED", "spawn ranged", nullptr},
        {"CLEAR DECALS", "clear_decals", nullptr},
        {"SLOW MO 0.25", "timescale 0.25", nullptr},
        {"NORMAL SPEED", "timescale 1", nullptr},
        {"OPEN DOOR 0", "open_door 0", nullptr},
        {"OPEN DOOR 1", "open_door 1", nullptr},
        {"RESPAWN CHECKPOINT", "respawn", nullptr},
    }};
}

constexpr float kMenuX0 = -0.98f, kMenuX1 = -0.50f, kMenuTop = 0.84f, kMenuPitch = 0.072f,
                kMenuRowH = 0.062f;

inline OverlayRect menuRowRect(std::size_t i) {
    const float y1 = kMenuTop - static_cast<float>(i) * kMenuPitch;
    return {{kMenuX0, y1 - kMenuRowH}, {kMenuX1, y1}, {}};
}

inline bool inRect(const OverlayRect& r, glm::vec2 p) {
    return p.x >= r.min.x && p.x <= r.max.x && p.y >= r.min.y && p.y <= r.max.y;
}

// Command of the row under `cursor`, or nullptr.
inline const char* menuHit(const DevState& d, glm::vec2 cursor) {
    const auto items = menuItems(d);
    for (std::size_t i = 0; i < items.size(); ++i)
        if (inRect(menuRowRect(i), cursor)) return items[i].cmd;
    return nullptr;
}

inline void drawMenu(const DevState& d, glm::vec2 cursor, float aspect, std::vector<OverlayRect>& out) {
    constexpr float px = 0.0065f;
    const auto      items = menuItems(d);
    const float     bottom = kMenuTop - static_cast<float>(items.size()) * kMenuPitch;
    out.push_back({{kMenuX0 - 0.015f, bottom - 0.01f}, {kMenuX1 + 0.015f, kMenuTop + 0.13f},
                   {0.02f, 0.02f, 0.03f, 0.82f}});
    out.push_back({{kMenuX0 - 0.015f, kMenuTop + 0.125f}, {kMenuX1 + 0.015f, kMenuTop + 0.13f},
                   {0.85f, 0.12f, 0.10f, 0.95f}});
    ui::drawText(out, "DEV MENU", {kMenuX0 + 0.01f, kMenuTop + 0.095f}, px * 1.3f, aspect,
                 {1.0f, 0.9f, 0.5f, 1.0f});
    ui::drawText(out, "F3 CLOSE  ` CONSOLE", {kMenuX0 + 0.01f, kMenuTop + 0.04f}, px * 0.85f, aspect,
                 {0.6f, 0.6f, 0.6f, 1.0f});

    for (std::size_t i = 0; i < items.size(); ++i) {
        const OverlayRect r = menuRowRect(i);
        const bool hot = inRect(r, cursor);
        out.push_back({r.min, r.max, hot ? glm::vec4(0.35f, 0.12f, 0.10f, 0.95f)
                                          : glm::vec4(0.12f, 0.12f, 0.14f, 0.90f)});
        ui::drawText(out, items[i].label, {r.min.x + 0.012f, r.max.y - 0.0085f}, px, aspect,
                     {0.92f, 0.92f, 0.92f, 1.0f});
        if (items[i].on) {
            const bool on = *items[i].on;
            out.push_back({{r.max.x - 0.045f, r.min.y + 0.016f}, {r.max.x - 0.012f, r.max.y - 0.016f},
                           on ? glm::vec4(0.2f, 0.9f, 0.3f, 1.0f) : glm::vec4(0.3f, 0.3f, 0.3f, 1.0f)});
        }
    }
}

// ---- perf readout and active-cheat badges --------------------------------
struct StatsInfo {
    int ticksPerSec = 0, fps = 0;
    int particles = 0, decals = 0, enemiesAlive = 0;
    int enemiesAwake = 0, enemiesAsleep = 0;       // of the living ones: thinking vs standing by
    int lightsSubmitted = 0, lightsTotal = 0;      // room lights drawn this frame vs in the level
    int worldQueriesPerSec = 0;                    // World ray/ground/wall queries answered
    float worldBlocksPerQuery = 0.0f;              // blocks examined per query (the broadphase at work)
    int maxBoxes = 8192;
};

inline void drawStats(const DevState& d, const FrameProfile& p, const StatsInfo& s, float aspect,
                      std::vector<OverlayRect>& out) {
    constexpr float px = 0.0055f, lh = 0.05f;
    const auto num = [](float v) {
        const int t = static_cast<int>(v * 100.0f + 0.5f);
        return std::to_string(t / 100) + "." + (t % 100 < 10 ? "0" : "") + std::to_string(t % 100);
    };
    std::vector<std::string> lines;
    if (d.showStats) {
        lines = {
            "FPS " + std::to_string(s.fps) + "  TICKS " + std::to_string(s.ticksPerSec) + "/S",
            "FRAME " + num(p.frameMs) + " MS",
            "CPU RENDER " + num(p.cpuMs) + " MS",
            "  COLLECT " + num(p.collectMs) + " MS",
            "  SCENE   " + num(p.sceneMs) + " MS",
            "  UI      " + num(p.overlayMs) + " MS",
            "BOXES " + std::to_string(p.boxes) + "/" + std::to_string(s.maxBoxes),
            "UI RECTS " + std::to_string(p.overlayRects),
            "PARTICLES " + std::to_string(s.particles),
            "DECALS " + std::to_string(s.decals),
            "ENEMIES " + std::to_string(s.enemiesAlive) + "  AWAKE " + std::to_string(s.enemiesAwake) +
                "  ASLEEP " + std::to_string(s.enemiesAsleep),
            "LIGHTS " + std::to_string(s.lightsSubmitted) + "/" + std::to_string(s.lightsTotal),
            "WORLD " + std::to_string(s.worldQueriesPerSec) + " Q/S  " + num(s.worldBlocksPerQuery) + " BLOCKS/Q",
        };
    }
    std::string badges;
    if (d.god)      badges += "GOD ";
    if (d.noclip)   badges += "NOCLIP ";
    if (d.infAmmo)  badges += "INFAMMO ";
    if (d.freezeAI) badges += "AI OFF ";
    if (d.timeScale != 1.0f) badges += "TIME " + num(d.timeScale) + " ";
    if (!badges.empty()) lines.push_back(badges);

    float y = 0.96f;
    for (const std::string& l : lines) {
        const float w = ui::textWidth(l, px, aspect);
        out.push_back({{0.98f - w - 0.01f, y - 7.0f * px - 0.008f}, {0.99f, y + 0.006f},
                       {0.0f, 0.0f, 0.0f, 0.55f}});
        ui::drawText(out, l, {0.98f - w, y}, px, aspect, {0.4f, 1.0f, 0.5f, 1.0f});
        y -= lh;
    }
}

} // namespace carnage::dev
