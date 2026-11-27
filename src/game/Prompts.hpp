#pragma once
#include "core/InputMap.hpp"
#include "ui/Text.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace carnage {

// Tutorial prompts: short on-screen hints shown when the player enters a trigger
// volume. Pure state + text logic (no rendering, audio or input), so the timing
// and clear rules are unit-testable.

// What ends a prompt early. Every prompt also fades out on its own timer.
enum class PromptClear { Timer, Jump, Slide, Vault, WallRun, Fire, Grab, Throw };

// Things the player did this tick that a prompt may be waiting for.
struct PromptEvents {
    bool jump = false, slide = false, vault = false, wallRun = false;
    bool fire = false, grab = false, thrown = false;

    bool performed(PromptClear c) const {
        switch (c) {
            case PromptClear::Timer:   return false;
            case PromptClear::Jump:    return jump;
            case PromptClear::Slide:   return slide;
            case PromptClear::Vault:   return vault;
            case PromptClear::WallRun: return wallRun;
            case PromptClear::Fire:    return fire;
            case PromptClear::Grab:    return grab;
            case PromptClear::Throw:   return thrown;
        }
        return false;
    }
};

class PromptSystem {
public:
    static constexpr float kFadeIn   = 0.25f;  // s to reach full opacity
    static constexpr float kFadeOut  = 0.5f;   // s from "dismissed" to gone
    static constexpr float kMinShown = 0.75f;  // an action can't dismiss a prompt before this

    // Shows `text`, replacing whatever is up. `seconds` is how long it stays
    // fully visible before fading, unless `clearOn` happens first.
    void show(std::string text, float seconds, PromptClear clearOn = PromptClear::Timer) {
        text_    = std::move(text);
        seconds_ = std::max(seconds, kMinShown);
        clearOn_ = clearOn;
        age_     = 0.0f;
        fade_    = 0.0f;
        fading_  = false;
        active_  = !text_.empty();
    }

    void clear() { *this = PromptSystem{}; }

    void update(float dt, const PromptEvents& ev) {
        if (!active_) return;
        age_ += dt;
        if (!fading_) {
            const bool done = age_ >= seconds_;
            const bool acted = age_ >= kMinShown && ev.performed(clearOn_);
            if (done || acted) fading_ = true;
        } else {
            fade_ += dt;
            if (fade_ >= kFadeOut) clear();
        }
    }

    bool               active() const { return active_; }
    bool               fading() const { return active_ && fading_; }
    const std::string& text() const { return text_; }

    // 0..1 opacity: ramps in, holds, ramps out.
    float alpha() const {
        if (!active_) return 0.0f;
        const float in  = std::clamp(age_ / kFadeIn, 0.0f, 1.0f);
        const float out = fading_ ? 1.0f - std::clamp(fade_ / kFadeOut, 0.0f, 1.0f) : 1.0f;
        return in * out;
    }

private:
    std::string text_;
    PromptClear clearOn_ = PromptClear::Timer;
    float       seconds_ = 0.0f;
    float       age_     = 0.0f;
    float       fade_    = 0.0f;
    bool        fading_  = false;
    bool        active_  = false;
};

inline constexpr std::size_t kMaxPromptChars = 90;  // keeps one line on screen at 16:9

// The HUD font has no braces, so `{name}` placeholders are always expanded
// before drawing. Names are the settings-file keys of the bindable actions
// (forward, sprint, jump, grab, weapon1, ...), plus the fixed mouse buttons.
inline bool promptPlaceholder(std::string_view name, const InputMap& keys, std::string& out) {
    if (name == "lmb") { out = "LEFT CLICK";  return true; }
    if (name == "rmb") { out = "RIGHT CLICK"; return true; }
    for (std::size_t i = 0; i < kActionCount; ++i) {
        if (name == kActionInfo[i].ini) { out = keyName(keys.key(i)); return true; }
    }
    return false;
}

inline std::string expandPromptText(std::string_view text, const InputMap& keys) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '}') { out += '?'; continue; }  // never emit a brace
        if (text[i] != '{') { out += text[i]; continue; }
        const std::size_t end = text.find('}', i);
        std::string key;
        if (end == std::string_view::npos) { out += '?'; continue; }
        if (!promptPlaceholder(text.substr(i + 1, end - i - 1), keys, key)) key = "?";
        out += key;
        i = end;  // consume the whole {placeholder}, known or not
    }
    return out;
}

// Authoring check, run by the level loader. Returns an empty string when the
// text is fine, otherwise a human-readable problem.
inline std::string checkPromptText(std::string_view text) {
    if (text.empty()) return "prompt text is empty";
    const InputMap defaults;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '}') return "stray '}' in prompt text";
        if (text[i] != '{') continue;
        const std::size_t end = text.find('}', i);
        std::string ignored;
        if (end == std::string_view::npos) return "unterminated '{' in prompt text";
        if (!promptPlaceholder(text.substr(i + 1, end - i - 1), defaults, ignored))
            return "unknown placeholder '" + std::string(text.substr(i, end - i + 1)) + "'";
        i = end;
    }
    const std::string shown = expandPromptText(text, defaults);
    for (const char c : shown)
        if (!ui::hasGlyph(c)) return std::string("character '") + c + "' cannot be drawn by the HUD font";
    if (shown.size() > kMaxPromptChars)
        return "prompt is too long (" + std::to_string(shown.size()) + " > " +
               std::to_string(kMaxPromptChars) + " characters once keys are filled in)";
    return {};
}

} // namespace carnage
