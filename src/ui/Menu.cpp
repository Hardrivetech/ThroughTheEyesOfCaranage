#include "ui/Menu.hpp"
#include "ui/Text.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>

namespace carnage::ui {

namespace {

constexpr const char* kSettingsPath = "carnage_settings.ini";

constexpr float kSensMin = 0.2f, kSensMax = 3.0f;
constexpr float kFovMin  = 70.0f, kFovMax = 110.0f;
constexpr float kShakeMax = 1.5f;
constexpr float kFlashMax = 1.5f;
constexpr float kKickMax  = 1.5f;

int rowId(float cy) { return static_cast<int>(std::lround(cy * 100.0f)); }

constexpr glm::vec4 kWhite {0.92f, 0.92f, 0.94f, 1.0f};
constexpr glm::vec4 kGrey  {0.62f, 0.62f, 0.66f, 1.0f};
constexpr glm::vec4 kDim   {0.40f, 0.40f, 0.44f, 1.0f};
constexpr glm::vec4 kRed   {0.85f, 0.12f, 0.08f, 1.0f};
constexpr glm::vec4 kHot   {0.60f, 0.09f, 0.06f, 0.96f};

// Options layout (NDC). Rows span the panel; sliders live on the right.
constexpr float kPanelX    = 0.86f;
constexpr float kRowX      = 0.80f;
constexpr float kRowHalfH  = 0.052f;
constexpr float kRowStride = 0.125f;
constexpr float kRowTop    = 0.50f;
constexpr float kTrackX0   = 0.10f, kTrackX1 = 0.58f;
constexpr float kValueX    = 0.78f;
constexpr float kTabY      = 0.68f;
constexpr float kDescY     = -0.55f;
constexpr float kFooterY   = -0.80f;
constexpr int   kBindRowsPerColumn = 9;

} // namespace

bool loadSettings(Settings& s, const char* path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string key;
    float       value = 0.0f;
    while (f >> key >> value) {
        if (key == "sensitivity")  s.sensitivity = value;
        else if (key == "invert_y") s.invertY = value != 0.0f;
        else if (key == "shake")   s.shake = value;
        else if (key == "flash")   s.flash = value;
        else if (key == "fov")     s.fov = value;
        else if (key == "fov_kick") s.fovKick = value;
        else if (key == "vsync")   s.vsync = value != 0.0f;
        else if (key == "volume")  s.volume = value;
        else if (key == "music")   s.music = value;
        else if (key == "sfx")     s.sfx = value;
        else if (key.starts_with("bind_")) {
            const std::string_view id = std::string_view(key).substr(5);
            for (std::size_t i = 0; i < kActionCount; ++i)
                if (id == kActionInfo[i].ini && isBindableKey(static_cast<int>(value)))
                    s.keys.setRaw(i, static_cast<int>(value));
        }
    }
    // A hand-edited file could bind two actions to one key: fall back to defaults.
    for (std::size_t i = 0; i < kActionCount; ++i)
        for (std::size_t j = i + 1; j < kActionCount; ++j)
            if (s.keys.key(i) == s.keys.key(j)) { s.keys.reset(); i = j = kActionCount; }
    s.sensitivity = std::clamp(s.sensitivity, kSensMin, kSensMax);
    s.shake       = std::clamp(s.shake, 0.0f, kShakeMax);
    s.flash       = std::clamp(s.flash, 0.0f, kFlashMax);
    s.fov         = std::clamp(s.fov, kFovMin, kFovMax);
    s.fovKick     = std::clamp(s.fovKick, 0.0f, kKickMax);
    s.volume      = std::clamp(s.volume, 0.0f, 1.0f);
    s.music       = std::clamp(s.music, 0.0f, 1.0f);
    s.sfx         = std::clamp(s.sfx, 0.0f, 1.0f);
    return true;
}

void saveSettings(const Settings& s, const char* path) {
    std::ofstream f(path);
    if (!f) return;
    f << "sensitivity " << s.sensitivity << "\n"
      << "invert_y " << (s.invertY ? 1 : 0) << "\n"
      << "shake " << s.shake << "\n"
      << "flash " << s.flash << "\n"
      << "fov " << s.fov << "\n"
      << "fov_kick " << s.fovKick << "\n"
      << "vsync " << (s.vsync ? 1 : 0) << "\n"
      << "volume " << s.volume << "\n"
      << "music " << s.music << "\n"
      << "sfx " << s.sfx << "\n";
    for (std::size_t i = 0; i < kActionCount; ++i)
        f << "bind_" << kActionInfo[i].ini << " " << s.keys.key(i) << "\n";
}

// --- helpers ---------------------------------------------------------------

bool Menu::hover(glm::vec2 c, glm::vec2 halfNdc) const {
    return std::abs(in_.cursor.x - c.x) <= halfNdc.x && std::abs(in_.cursor.y - c.y) <= halfNdc.y;
}

void Menu::textCentered(std::string_view t, float cx, float cy, float px, glm::vec4 col) {
    const float w = textWidth(t, px, aspect_);
    drawText(rects_, t, {cx - w * 0.5f, cy + 3.5f * px}, px, aspect_, col);
}

void Menu::textLeft(std::string_view t, float x, float cy, float px, glm::vec4 col) {
    drawText(rects_, t, {x, cy + 3.5f * px}, px, aspect_, col);
}

void Menu::textRight(std::string_view t, float x, float cy, float px, glm::vec4 col) {
    drawText(rects_, t, {x - textWidth(t, px, aspect_), cy + 3.5f * px}, px, aspect_, col);
}

bool Menu::button(std::string_view label, float cy, bool danger, float cx, float halfX) {
    const glm::vec2 half{halfX > 0.0f ? halfX : 0.50f / aspect_, 0.065f};
    const bool      over = hover({cx, cy}, half);
    if (over) hotId_ = rowId(cy) + static_cast<int>(cx * 1000.0f);
    if (over && in_.clicked) click_ = true;

    const glm::vec4 base = danger ? glm::vec4(0.16f, 0.06f, 0.06f, 0.92f)
                                  : glm::vec4(0.09f, 0.09f, 0.11f, 0.92f);
    rect({cx - half.x, cy - half.y}, {cx + half.x, cy + half.y}, over ? kHot : base);
    // Accent bar on the left edge.
    rect({cx - half.x, cy - half.y}, {cx - half.x + 0.006f / aspect_ * 2.0f, cy + half.y},
         over ? kWhite : kRed);
    textCentered(label, cx, cy, 0.0105f, kWhite);
    return over && in_.clicked;
}

bool Menu::tabButton(Tab t, std::string_view label, float cx) {
    constexpr float kHalfX = 0.17f, kHalfY = 0.05f;
    const bool active = tab_ == t;
    const bool over   = hover({cx, kTabY}, {kHalfX, kHalfY});
    if (over) hotId_ = 1000 + static_cast<int>(t);
    if (over && in_.clicked) click_ = true;

    rect({cx - kHalfX, kTabY - kHalfY}, {cx + kHalfX, kTabY + kHalfY},
         active ? glm::vec4(0.14f, 0.06f, 0.05f, 0.95f)
                : (over ? glm::vec4(0.12f, 0.12f, 0.15f, 0.9f) : glm::vec4(0.06f, 0.06f, 0.08f, 0.75f)));
    // Underline: bright red when selected, faint on hover.
    if (active || over)
        rect({cx - kHalfX, kTabY - kHalfY}, {cx + kHalfX, kTabY - kHalfY + 0.008f},
             active ? kRed : kDim);
    textCentered(label, cx, kTabY, 0.0095f, active ? kWhite : (over ? kWhite : kGrey));
    return over && in_.clicked;
}

// Draws the row background and returns whether the cursor is over it.
bool Menu::row(float cy, std::string_view label, std::string_view desc) {
    const bool over = hover({0.0f, cy}, {kRowX, kRowHalfH});
    if (over) { hotId_ = rowId(cy); hoverDesc_ = desc; }
    rect({-kRowX, cy - kRowHalfH}, {kRowX, cy + kRowHalfH},
         over ? glm::vec4(0.17f, 0.06f, 0.05f, 0.78f) : glm::vec4(0.08f, 0.08f, 0.10f, 0.55f));
    rect({-kRowX, cy - kRowHalfH}, {-kRowX + 0.005f / aspect_ * 2.0f, cy + kRowHalfH},
         over ? kRed : glm::vec4(0.30f, 0.06f, 0.05f, 0.7f));
    textLeft(label, -kRowX + 0.04f, cy, 0.0095f, over ? kWhite : glm::vec4(0.80f, 0.80f, 0.83f, 1.0f));
    return over;
}

// Returns true when the value changed.
bool Menu::slider(int id, float cy, std::string_view label, std::string_view desc, float& norm,
                  const std::string& value) {
    const bool rowOver = row(cy, label, desc);

    const bool over = in_.cursor.x >= kTrackX0 - 0.03f && in_.cursor.x <= kTrackX1 + 0.03f &&
                      std::abs(in_.cursor.y - cy) < kRowHalfH;
    if (over && in_.clicked) { dragging_ = id; click_ = true; }

    bool changed = false;
    if (dragging_ == id) {
        if (in_.held) {
            const float n = std::clamp((in_.cursor.x - kTrackX0) / (kTrackX1 - kTrackX0), 0.0f, 1.0f);
            changed = n != norm;
            norm    = n;
        } else {
            dragging_ = -1;
        }
    }
    const bool active = over || dragging_ == id;
    (void)rowOver;

    const float hx = kTrackX0 + (kTrackX1 - kTrackX0) * norm;
    rect({kTrackX0, cy - 0.006f}, {kTrackX1, cy + 0.006f}, {0.20f, 0.20f, 0.24f, 1.0f});
    rect({kTrackX0, cy - 0.006f}, {hx, cy + 0.006f}, active ? glm::vec4(1.0f, 0.22f, 0.12f, 1.0f) : kRed);
    // Diamond-ish handle: a tall bar with a bright centre.
    const float hw = 0.009f / aspect_;
    rect({hx - hw, cy - 0.032f}, {hx + hw, cy + 0.032f}, active ? kWhite : kGrey);
    textRight(value, kValueX, cy, 0.0095f, active ? kWhite : kGrey);
    return changed;
}

bool Menu::toggle(float cy, std::string_view label, std::string_view desc, bool on) {
    const bool over = row(cy, label, desc);
    if (over && in_.clicked) click_ = true;

    // Pill switch aligned with the slider track's right end.
    const glm::vec2 c{kTrackX1 - 0.04f, cy};
    const glm::vec2 half{0.06f, 0.030f};
    rect(c - half, c + half, on ? glm::vec4(0.70f, 0.10f, 0.06f, 1.0f) : glm::vec4(0.18f, 0.18f, 0.22f, 1.0f));
    const float kw = 0.020f / aspect_ * 1.6f;
    const float kx = on ? c.x + half.x - kw - 0.006f : c.x - half.x + 0.006f;
    rect({kx, c.y - half.y + 0.006f}, {kx + kw, c.y + half.y - 0.006f}, kWhite);
    textRight(on ? "ON" : "OFF", kValueX, cy, 0.0095f, on ? kWhite : kGrey);
    return over && in_.clicked;
}

void Menu::bindingRow(float cy, std::string_view keys, std::string_view action) {
    constexpr float kHalfH = 0.034f;
    rect({-kRowX, cy - kHalfH}, {kRowX, cy + kHalfH}, {0.08f, 0.08f, 0.10f, 0.45f});
    textLeft(keys, -kRowX + 0.04f, cy, 0.0085f, kRed);
    textLeft(action, -0.12f, cy, 0.0085f, kWhite);
}

// --- screens ---------------------------------------------------------------

MenuAction Menu::mainScreen() {
    textCentered("THROUGH THE EYES OF", 0.0f, 0.66f, 0.0095f, kGrey);
    textCentered("CARNAGE", 0.0f, 0.46f, 0.030f, kRed);

    if (button("PLAY", 0.10f)) { screen_ = Screen::Playing; return MenuAction::Play; }
    if (button("OPTIONS", -0.10f)) { back_ = Screen::Main; screen_ = Screen::Options; tab_ = Tab::Gameplay; }
    if (button("QUIT GAME", -0.30f, true)) return MenuAction::Quit;

    textCentered("WASD MOVE    SPACE JUMP/VAULT    SHIFT SPRINT    C SLIDE", 0.0f, -0.76f, 0.0065f, kGrey);
    textCentered("LMB ATTACK    RMB LEFT HAND    E GRAB/THROW    1-5 WEAPONS    ALT+2-5 DUAL WIELD",
                 0.0f, -0.84f, 0.0065f, kGrey);
    return MenuAction::None;
}

MenuAction Menu::pauseScreen() {
    textCentered("PAUSED", 0.0f, 0.55f, 0.026f, kWhite);

    if (button("RESUME", 0.22f)) { screen_ = Screen::Playing; return MenuAction::Play; }
    if (button("OPTIONS", 0.02f)) { back_ = Screen::Pause; screen_ = Screen::Options; tab_ = Tab::Gameplay; }
    if (button("MAIN MENU", -0.18f)) { screen_ = Screen::Main; return MenuAction::MainMenu; }
    if (button("QUIT GAME", -0.38f, true)) return MenuAction::Quit;
    return MenuAction::None;
}

void Menu::leaveOptions() {
    if (dirty_) { saveSettings(settings_, kSettingsPath); dirty_ = false; }
    dragging_ = -1;
    rebinding_ = -1;
    screen_   = back_;
}

void Menu::tabGameplay() {
    float n = (settings_.sensitivity - kSensMin) / (kSensMax - kSensMin);
    if (slider(0, kRowTop, "MOUSE SENSITIVITY", "How fast the camera turns with the mouse", n,
               std::format("{:.2f}", settings_.sensitivity))) {
        settings_.sensitivity = kSensMin + n * (kSensMax - kSensMin);
        dirty_ = true;
    }
    if (toggle(kRowTop - kRowStride, "INVERT Y AXIS", "Push the mouse up to look down", settings_.invertY)) {
        settings_.invertY = !settings_.invertY;
        dirty_ = true;
    }
    n = settings_.shake / kShakeMax;
    if (slider(1, kRowTop - 2 * kRowStride, "SCREEN SHAKE", "Strength of trauma shake and camera kick", n,
               std::format("{:.0f}%", settings_.shake * 100.0f))) {
        settings_.shake = n * kShakeMax;
        dirty_ = true;
    }
    n = settings_.flash / kFlashMax;
    if (slider(2, kRowTop - 3 * kRowStride, "MUZZLE FLASH", "Brightness of the blinding flash when firing", n,
               std::format("{:.0f}%", settings_.flash * 100.0f))) {
        settings_.flash = n * kFlashMax;
        dirty_ = true;
    }
}

void Menu::tabVideo() {
    float n = (settings_.fov - kFovMin) / (kFovMax - kFovMin);
    if (slider(3, kRowTop, "FIELD OF VIEW", "Base vertical field of view in degrees", n,
               std::format("{:.0f}", settings_.fov))) {
        settings_.fov = std::round(kFovMin + n * (kFovMax - kFovMin));
        dirty_ = true;
    }
    n = settings_.fovKick / kKickMax;
    if (slider(4, kRowTop - kRowStride, "SPEED FOV KICK", "How much the view widens as you gain speed", n,
               std::format("{:.0f}%", settings_.fovKick * 100.0f))) {
        settings_.fovKick = n * kKickMax;
        dirty_ = true;
    }
    if (toggle(kRowTop - 2 * kRowStride, "VSYNC", "Sync to the display refresh rate to stop tearing",
               settings_.vsync)) {
        settings_.vsync = !settings_.vsync;
        dirty_ = true;
    }
}

void Menu::tabAudio() {
    float n = settings_.volume;
    if (slider(5, kRowTop, "MASTER VOLUME", "Overall loudness of the game", n,
               std::format("{:.0f}%", settings_.volume * 100.0f))) {
        settings_.volume = n;
        dirty_ = true;
    }
    n = settings_.music;
    if (slider(6, kRowTop - kRowStride, "MUSIC VOLUME", "Loudness of the main menu music", n,
               std::format("{:.0f}%", settings_.music * 100.0f))) {
        settings_.music = n;
        dirty_ = true;
    }
    n = settings_.sfx;
    if (slider(7, kRowTop - 2 * kRowStride, "EFFECTS VOLUME", "Weapons footsteps impacts and ambience", n,
               std::format("{:.0f}%", settings_.sfx * 100.0f))) {
        settings_.sfx = n;
        dirty_ = true;
    }
}

void Menu::rebindRow(int index, float cy) {
    constexpr float kHalfH = 0.044f;
    const bool  right = index >= kBindRowsPerColumn;
    const float x0 = right ? 0.03f : -kRowX, x1 = right ? kRowX : -0.03f;
    const bool  over = hover({(x0 + x1) * 0.5f, cy}, {(x1 - x0) * 0.5f, kHalfH});
    const bool  active = rebinding_ == index;
    if (over) hotId_ = 2000 + index;
    if (over && in_.clicked) { click_ = true; rebinding_ = active ? -1 : index; }

    rect({x0, cy - kHalfH}, {x1, cy + kHalfH},
         active ? glm::vec4(0.35f, 0.07f, 0.05f, 0.9f)
                : (over ? glm::vec4(0.17f, 0.06f, 0.05f, 0.78f) : glm::vec4(0.08f, 0.08f, 0.10f, 0.50f)));
    textLeft(kActionInfo[static_cast<std::size_t>(index)].label, x0 + 0.03f, cy, 0.0072f,
             over || active ? kWhite : kGrey);
    const std::string name = active ? std::string("PRESS A KEY")
                                    : keyName(settings_.keys.key(static_cast<std::size_t>(index)));
    textRight(name, x1 - 0.03f, cy, 0.0082f, kRed);
}

void Menu::tabControls() {
    hoverDesc_ = rebinding_ >= 0 ? "PRESS THE NEW KEY, OR ESC TO CANCEL"
                                 : "CLICK AN ACTION, THEN PRESS A KEY. A KEY IN USE IS SWAPPED";
    if (rebinding_ >= 0) {
        if (in_.key == key::Escape) rebinding_ = -1;
        else if (in_.key >= 0 && settings_.keys.rebind(static_cast<Action>(rebinding_), in_.key)) {
            rebinding_ = -1;
            dirty_     = true;
            click_     = true;
        }
    }
    for (int i = 0; i < static_cast<int>(kActionCount); ++i) {
        const int col = i / kBindRowsPerColumn, r = i % kBindRowsPerColumn;
        (void)col;
        rebindRow(i, kRowTop + 0.04f - 0.105f * static_cast<float>(r));
    }
    textCentered("MOUSE: LMB RIGHT HAND    RMB LEFT HAND    ESC PAUSE (FIXED)", 0.0f, -0.56f + 0.06f, 0.0065f, kDim);
}

void Menu::optionsScreen() {
    // Header.
    textLeft("OPTIONS", -kPanelX, 0.88f, 0.026f, kWhite);
    rect({-kPanelX, 0.795f}, {kPanelX, 0.80f}, {0.55f, 0.08f, 0.05f, 0.9f});

    // Tabs.
    const struct { Tab t; const char* label; float x; } tabs[] = {
        {Tab::Gameplay, "GAMEPLAY", -0.54f}, {Tab::Video, "VIDEO", -0.18f},
        {Tab::Audio, "AUDIO", 0.18f},        {Tab::Controls, "CONTROLS", 0.54f}};
    for (const auto& t : tabs)
        if (tabButton(t.t, t.label, t.x)) { tab_ = t.t; dragging_ = -1; rebinding_ = -1; }

    // Panel.
    rect({-kPanelX, -0.62f}, {kPanelX, 0.60f}, {0.03f, 0.03f, 0.04f, 0.72f});
    rect({-kPanelX, 0.595f}, {kPanelX, 0.60f}, {0.25f, 0.25f, 0.30f, 0.8f});

    switch (tab_) {
        case Tab::Gameplay: tabGameplay(); break;
        case Tab::Video:    tabVideo();    break;
        case Tab::Audio:    tabAudio();    break;
        case Tab::Controls: tabControls(); break;
    }

    // Hovered setting's description.
    if (!hoverDesc_.empty()) {
        rect({-kRowX, kDescY - 0.045f}, {kRowX, kDescY + 0.045f}, {0.0f, 0.0f, 0.0f, 0.35f});
        textLeft(hoverDesc_, -kRowX + 0.04f, kDescY, 0.0085f, kGrey);
    }

    // Footer.
    if (button("BACK", kFooterY, false, -0.34f, 0.30f)) leaveOptions();
    if (button("RESET DEFAULTS", kFooterY, true, 0.34f, 0.30f)) {
        settings_ = Settings{};
        dirty_    = true;
    }
}

MenuAction Menu::update(const MenuInput& in, float aspect) {
    rects_.clear();
    click_ = hoverChanged_ = false;
    hotId_ = -1;
    hoverDesc_ = {};

    if (screen_ == Screen::Playing) {
        if (in.esc) {
            screen_ = Screen::Pause;
            return MenuAction::Pause;
        }
        return MenuAction::None;
    }

    in_     = in;
    aspect_ = aspect;
    const bool wasCapturing = rebinding_ >= 0;

    const float dim = screen_ == Screen::Options ? (back_ == Screen::Main ? 0.55f : 0.82f)
                                                 : (atMainMenu() ? 0.20f : 0.74f);
    rect({-1.0f, -1.0f}, {1.0f, 1.0f}, {0.015f, 0.015f, 0.02f, dim});

    MenuAction act = MenuAction::None;
    switch (screen_) {
        case Screen::Main:    act = mainScreen(); break;
        case Screen::Pause:   act = pauseScreen(); break;
        case Screen::Options: optionsScreen(); break;
        case Screen::Playing: break;
    }
    hoverChanged_ = hotId_ != -1 && hotId_ != prevHotId_;
    prevHotId_    = hotId_;

    // Escape: close options, or resume from pause.
    if (in.esc && act == MenuAction::None && !wasCapturing) {
        if (screen_ == Screen::Options) leaveOptions();
        else if (screen_ == Screen::Pause) { screen_ = Screen::Playing; act = MenuAction::Play; }
    }
    return act;
}

void Menu::draw(std::vector<OverlayRect>& out) const {
    out.insert(out.end(), rects_.begin(), rects_.end());
}

} // namespace carnage::ui
