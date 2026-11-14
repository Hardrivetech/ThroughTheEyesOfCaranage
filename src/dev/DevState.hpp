#pragma once
#include <algorithm>

namespace carnage::dev {

// Live-playtest toggles. Kept out of gameplay classes: main.cpp consults these
// at a handful of hook points. Not persisted; they survive a level reset on
// purpose so a tester can keep god mode while iterating.
struct DevState {
    bool  god         = false;
    bool  infAmmo     = false;
    bool  noclip      = false;
    bool  freezeAI    = false;
    bool  showStats   = false;
    float timeScale   = 1.0f;
    bool  menuOpen    = false;
    bool  consoleOpen = false;

    void setTimeScale(float s) { timeScale = std::clamp(s, 0.05f, 2.0f); }
};

// Frame-time profiler: exponential moving averages in milliseconds.
struct FrameProfile {
    float cpuMs = 0.0f;       // whole render lambda, CPU side
    float collectMs = 0.0f;   // building boxes / lights
    float sceneMs = 0.0f;     // renderScene (CPU submit)
    float overlayMs = 0.0f;   // UI build + drawOverlay
    float frameMs = 0.0f;     // wall time between frames
    int   boxes = 0, overlayRects = 0;

    static void ema(float& v, float s) { v = v == 0.0f ? s : v + (s - v) * 0.05f; }
};

} // namespace carnage::dev
