#pragma once
#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace carnage {

// Abstract gameplay actions. ESC (pause) and the mouse buttons are fixed.
enum class Action : int {
    Forward, Back, Left, Right,
    Sprint, Crouch, CrouchAlt, Jump,
    Grab, ReloadRight, ReloadLeft, Respawn,
    LeftHandMod,  // hold + weapon slot = equip the left hand
    Weapon1, Weapon2, Weapon3, Weapon4, Weapon5,
    Count
};
constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);

// Key codes are GLFW's (stable across 3.x). They are spelled out here so this
// header, and the menu tests, don't depend on GLFW; main.cpp static_asserts
// the ones that matter.
namespace key {
constexpr int Space = 32, Num0 = 48, A = 65, Escape = 256, Enter = 257, Tab = 258, Backspace = 259;
constexpr int Right = 262, Left = 263, Down = 264, Up = 265;
constexpr int F1 = 290, F12 = 301;
constexpr int LeftShift = 340, LeftControl = 341, LeftAlt = 342;
constexpr int RightShift = 344, RightControl = 345, RightAlt = 346;
constexpr int Last = 348;
} // namespace key

struct ActionInfo {
    const char* ini;    // key in the settings file (after the "bind_" prefix)
    const char* label;  // UI text
    int         def;    // default key
};

inline constexpr std::array<ActionInfo, kActionCount> kActionInfo{{
    {"forward",      "MOVE FORWARD",      'W'},
    {"back",         "MOVE BACK",         'S'},
    {"left",         "MOVE LEFT",         'A'},
    {"right",        "MOVE RIGHT",        'D'},
    {"sprint",       "SPRINT",            key::LeftShift},
    {"crouch",       "CROUCH / SLIDE",    'C'},
    {"crouch_alt",   "CROUCH ALT",        key::LeftControl},
    {"jump",         "JUMP / VAULT",      key::Space},
    {"grab",         "GRAB / THROW",      'E'},
    {"reload_right", "RELOAD RIGHT",      'R'},
    {"reload_left",  "RELOAD LEFT",       'Q'},
    {"respawn",      "RESPAWN",           'T'},
    {"left_mod",     "LEFT HAND MODIFIER", key::LeftAlt},
    {"weapon1",      "WEAPON 1 FISTS",    '1'},
    {"weapon2",      "WEAPON 2 PISTOL",   '2'},
    {"weapon3",      "WEAPON 3 SMG",      '3'},
    {"weapon4",      "WEAPON 4 SHOTGUN",  '4'},
    {"weapon5",      "WEAPON 5 ROCKET",   '5'},
}};

constexpr bool isBindableKey(int k) { return k > 0 && k <= key::Last && k != key::Escape; }

// Action -> physical key. A plain value type: copyable, comparable, no globals.
class InputMap {
public:
    constexpr InputMap() { reset(); }

    constexpr void reset() {
        for (std::size_t i = 0; i < kActionCount; ++i) keys_[i] = kActionInfo[i].def;
    }
    constexpr int key(Action a) const { return keys_[static_cast<std::size_t>(a)]; }
    constexpr int key(std::size_t i) const { return keys_[i]; }
    constexpr int weaponKey(std::size_t slot) const {
        return keys_[static_cast<std::size_t>(Action::Weapon1) + slot];
    }

    // Binds `k` to `a`. If another action already uses `k`, the two swap so
    // no action is ever left unbound or duplicated. Returns false if `k` is
    // not bindable.
    constexpr bool rebind(Action a, int k) {
        if (!isBindableKey(k)) return false;
        const std::size_t ai = static_cast<std::size_t>(a);
        for (std::size_t i = 0; i < kActionCount; ++i)
            if (i != ai && keys_[i] == k) keys_[i] = keys_[ai];
        keys_[ai] = k;
        return true;
    }

    // Used by the settings loader: sets without swapping, caller validates.
    constexpr void setRaw(std::size_t i, int k) { keys_[i] = k; }

    constexpr bool operator==(const InputMap&) const = default;

private:
    std::array<int, kActionCount> keys_{};
};

// Human-readable, upper-case key name for the UI font.
inline std::string keyName(int k) {
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) return std::string(1, static_cast<char>(k));
    if (k >= key::F1 && k <= key::F12) return "F" + std::to_string(k - key::F1 + 1);
    switch (k) {
        case key::Space:        return "SPACE";
        case key::Enter:        return "ENTER";
        case key::Tab:          return "TAB";
        case key::Backspace:    return "BACKSPACE";
        case key::Right:        return "RIGHT";
        case key::Left:         return "LEFT";
        case key::Down:         return "DOWN";
        case key::Up:           return "UP";
        case key::LeftShift:    return "L SHIFT";
        case key::RightShift:   return "R SHIFT";
        case key::LeftControl:  return "L CTRL";
        case key::RightControl: return "R CTRL";
        case key::LeftAlt:      return "L ALT";
        case key::RightAlt:     return "R ALT";
        case 39:  return "'";
        case 44:  return ",";
        case 45:  return "-";
        case 46:  return ".";
        case 47:  return "/";
        case 59:  return ";";
        case 61:  return "=";
        case 91:  return "[";
        case 93:  return "]";
        case 92:  return "\\";
        case 96:  return "`";
        default:  return "KEY " + std::to_string(k);
    }
}

} // namespace carnage
