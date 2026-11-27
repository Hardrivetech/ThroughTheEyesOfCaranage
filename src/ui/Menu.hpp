#pragma once
#include "core/DrawTypes.hpp"
#include "core/InputMap.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace carnage::ui {

struct Settings {
    // Gameplay
    float sensitivity = 1.0f;   // multiplier on the base mouse sensitivity
    bool  invertY     = false;
    float shake       = 1.0f;   // camera shake / feedback multiplier
    float flash       = 1.0f;   // muzzle-flash blind multiplier
    // Video
    float fov         = 90.0f;  // base vertical FOV, degrees
    float fovKick     = 1.0f;   // speed FOV-kick multiplier
    bool  vsync       = false;
    // Audio
    float volume      = 0.8f;   // master 0..1
    float music       = 1.0f;   // menu music 0..1
    float sfx         = 1.0f;   // effects + ambience 0..1
    // Controls
    InputMap keys;
};

bool loadSettings(Settings& s, const char* path);
void saveSettings(const Settings& s, const char* path);

enum class Screen { Main, Pause, Options, Playing };
enum class Tab { Gameplay, Video, Audio, Controls };

// What the game must do in response to a menu update.
enum class MenuAction {
    None,
    Play,   // start a new game, or resume: capture the cursor and run the simulation
    Pause,  // opened the pause menu: release the cursor
    Quit,
    MainMenu,  // pause -> main menu: the game must unload the level
    Continue,        // main menu: load the saved game (the menu is closed; call showMain() if the load fails)
    SelectLevel,     // main menu: go to the Kodiak hub (the menu is closed; call showMain() if that fails)
    LoadCheckpoint,  // pause: reload the last checkpoint and resume
    SaveCheckpoint,  // pause: write the save; the pause menu stays open
    RestartLevel,    // pause: restart the current level and resume
};

struct MenuInput {
    glm::vec2 cursor{0.0f};  // NDC
    bool      clicked = false;  // left button went down this tick
    bool      held    = false;  // left button is down
    bool      esc     = false;  // escape went down this tick
    int       key     = -1;     // first key pressed this tick (rebind capture), or -1
    // Keyboard navigation of the main and pause menus (not the settings screen, where keys rebind).
    bool      up      = false;  // Up arrow or W
    bool      down    = false;  // Down arrow or S
    bool      confirm = false;  // Enter or Space
};

// What the game knows that changes the menus. The game sets it (Menu::setContext); the menu never
// touches the disk.
struct MenuContext {
    bool        canContinue = false;     // a save exists
    std::string continueDetail;          // shown on the Continue button, e.g. the saved level
    bool        canSelectLevel = false;  // the hub is unlocked (the first mission is complete)
};

// One row of a menu screen.
struct MenuItem {
    std::string label;
    std::string detail;            // small text at the right of the button (may be empty)
    MenuAction  action = MenuAction::None;
    bool        opensSettings = false;
    bool        enabled = true;    // a disabled row is drawn dim and cannot be chosen
    bool        danger  = false;

    bool operator==(const MenuItem&) const = default;
};

// The rows of the main menu and of the pause menu, top to bottom.
std::vector<MenuItem> mainMenuItems(const MenuContext& ctx);
std::vector<MenuItem> pauseMenuItems();

// Where row `index` is centred on screen (NDC y) and how big a row is. Shared by drawing,
// mouse hit-testing and the tests, so they cannot disagree.
float itemCenterY(Screen screen, std::size_t index);
inline constexpr float kItemHalfHeight = 0.065f;
inline float itemHalfWidth(float aspect) { return 0.74f / aspect; }

// Immediate-mode style menu: update() both handles input and rebuilds the
// cached overlay rects, so draw() is a plain append.
class Menu {
public:
    MenuAction update(const MenuInput& in, float aspect);
    void       draw(std::vector<OverlayRect>& out) const;

    // One-shot UI sound requests raised by the last update().
    bool soundClick() const { return click_; }
    bool soundHover() const { return hoverChanged_; }

    bool active() const { return screen_ != Screen::Playing; }
    Screen screen() const { return screen_; }

    void                  setContext(MenuContext ctx) { ctx_ = std::move(ctx); }
    const MenuContext&    context() const { return ctx_; }
    // The rows of the current screen (empty on the settings screen and while playing).
    std::vector<MenuItem> items() const;
    // The row that Enter would choose, or -1 when there are no rows.
    int                   focus() const { return items().empty() ? -1 : static_cast<int>(focus_); }
    // A short message under the pause menu ("CHECKPOINT SAVED"), shown for a couple of seconds.
    void                  notify(std::string text) { notice_ = std::move(text); noticeTicks_ = kNoticeTicks; }
    const std::string&    notice() const { return noticeTicks_ > 0 ? notice_ : kNoNotice; }
    // Level end: drop back to the main menu (the caller resets the game).
    void showMain() { screen_ = Screen::Main; }
    bool atMainMenu() const { return screen_ == Screen::Main || (screen_ == Screen::Options && back_ == Screen::Main); }

    Settings&       settings() { return settings_; }
    const Settings& settings() const { return settings_; }
    Tab             tab() const { return tab_; }
    bool            capturing() const { return rebinding_ >= 0; }

private:
    static constexpr int         kNoticeTicks = 150;
    inline static const std::string kNoNotice{};

    MenuAction itemScreen();
    MenuAction activate(const MenuItem& item);
    void       drawItem(const MenuItem& item, float cy, bool focused);
    void       brackets(glm::vec2 lo, glm::vec2 hi, glm::vec4 color);
    void       optionsScreen();
    void       tabGameplay();
    void       tabVideo();
    void       tabAudio();
    void       tabControls();

    bool button(std::string_view label, float cy, bool danger = false, float cx = 0.0f, float halfX = -1.0f);
    bool tabButton(Tab t, std::string_view label, float cx);

    // Options rows. `cy` is the row centre; `desc` shows in the footer on hover.
    bool row(float cy, std::string_view label, std::string_view desc);
    bool slider(int id, float cy, std::string_view label, std::string_view desc, float& norm,
                const std::string& value);
    bool toggle(float cy, std::string_view label, std::string_view desc, bool on);
    void bindingRow(float cy, std::string_view keys, std::string_view action);
    void rebindRow(int index, float cy);

    void textCentered(std::string_view text, float cx, float cy, float px, glm::vec4 color);
    void textLeft(std::string_view text, float x, float cy, float px, glm::vec4 color);
    void textRight(std::string_view text, float x, float cy, float px, glm::vec4 color);
    void rect(glm::vec2 a, glm::vec2 b, glm::vec4 c) { rects_.push_back({a, b, c}); }
    bool hover(glm::vec2 center, glm::vec2 halfNdc) const;
    void leaveOptions();

    Screen    screen_ = Screen::Main;
    Screen    back_   = Screen::Main;
    Tab       tab_    = Tab::Gameplay;
    Settings  settings_;
    MenuContext ctx_;
    std::size_t focus_ = 0;                    // row Enter would choose
    Screen      focusScreen_ = Screen::Playing; // the screen focus_ belongs to
    glm::vec2   lastCursor_{9.0f, 9.0f};        // for noticing that the mouse moved
    std::string notice_;
    int         noticeTicks_ = 0;
    bool      dirty_    = false;
    int       dragging_ = -1;
    int       rebinding_ = -1;  // Action index awaiting a key press, or -1
    int       scroll_    = 0;   // first visible binding row
    bool      click_ = false, hoverChanged_ = false;
    int       hotId_ = -1, prevHotId_ = -1;  // hovered widget, for hover ticks
    std::string_view hoverDesc_;
    MenuInput in_{};
    float     aspect_ = 16.0f / 9.0f;

    std::vector<OverlayRect> rects_;
};

} // namespace carnage::ui
