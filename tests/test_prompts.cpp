#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Prompts.hpp"
#include "game/Triggers.hpp"

using namespace carnage;
using Catch::Approx;

namespace {
constexpr float kDt = 1.0f / 60.0f;

// Advances a prompt by `seconds` of simulated time with no player input.
void idle(PromptSystem& p, float seconds, const PromptEvents& ev = {}) {
    for (float t = 0.0f; t < seconds; t += kDt) p.update(kDt, ev);
}
} // namespace

TEST_CASE("A prompt fades in, holds, fades out and goes away", "[prompts]") {
    PromptSystem p;
    CHECK_FALSE(p.active());
    CHECK(p.alpha() == 0.0f);

    p.show("HELLO", 4.0f);
    REQUIRE(p.active());
    CHECK(p.text() == "HELLO");
    CHECK(p.alpha() == 0.0f);  // nothing visible before the first tick

    p.update(PromptSystem::kFadeIn * 0.5f, {});
    CHECK(p.alpha() == Approx(0.5f).margin(0.01f));  // fading in
    idle(p, PromptSystem::kFadeIn);
    CHECK(p.alpha() == 1.0f);

    idle(p, 3.0f);
    CHECK(p.active());
    CHECK_FALSE(p.fading());
    CHECK(p.alpha() == 1.0f);  // holds for its full time

    idle(p, 1.0f);  // past the 4 s mark: now fading
    CHECK(p.fading());
    CHECK(p.alpha() < 1.0f);
    CHECK(p.alpha() > 0.0f);

    idle(p, PromptSystem::kFadeOut + 0.1f);
    CHECK_FALSE(p.active());
    CHECK(p.alpha() == 0.0f);
    CHECK(p.text().empty());
}

TEST_CASE("Alpha never leaves 0..1 and fades monotonically", "[prompts]") {
    PromptSystem p;
    p.show("X", 2.0f);
    float last = 0.0f;
    bool  peaked = false;
    for (int i = 0; i < 600 && p.active(); ++i) {
        p.update(kDt, {});
        const float a = p.alpha();
        REQUIRE(a >= 0.0f);
        REQUIRE(a <= 1.0f);
        if (p.fading()) { peaked = true; REQUIRE(a <= last + 1e-6f); }
        last = a;
    }
    CHECK(peaked);
    CHECK_FALSE(p.active());
}

TEST_CASE("Doing the taught action dismisses the prompt early", "[prompts]") {
    PromptEvents jump;
    jump.jump = true;

    SECTION("after the minimum on-screen time") {
        PromptSystem p;
        p.show("JUMP", 20.0f, PromptClear::Jump);
        idle(p, PromptSystem::kMinShown + 0.05f);
        REQUIRE_FALSE(p.fading());
        p.update(kDt, jump);
        CHECK(p.fading());
        idle(p, PromptSystem::kFadeOut + 0.1f);
        CHECK_FALSE(p.active());
    }
    SECTION("not before it: the player has to be able to read it") {
        PromptSystem p;
        p.show("JUMP", 20.0f, PromptClear::Jump);
        idle(p, PromptSystem::kMinShown * 0.5f);
        p.update(kDt, jump);
        CHECK_FALSE(p.fading());
        idle(p, 1.0f);  // the early action is not remembered...
        CHECK_FALSE(p.fading());
        p.update(kDt, jump);  // ...but doing it again now clears it
        CHECK(p.fading());
    }
    SECTION("a different action does nothing") {
        PromptSystem p;
        p.show("JUMP", 20.0f, PromptClear::Jump);
        PromptEvents other;
        other.slide = other.vault = other.wallRun = other.fire = other.grab = other.thrown = true;
        idle(p, 2.0f, other);
        CHECK(p.active());
        CHECK_FALSE(p.fading());
    }
    SECTION("a timer-only prompt ignores every action") {
        PromptSystem p;
        p.show("INFO", 20.0f, PromptClear::Timer);
        PromptEvents all;
        all.jump = all.slide = all.vault = all.wallRun = all.fire = all.grab = all.thrown = true;
        idle(p, 3.0f, all);
        CHECK_FALSE(p.fading());
    }
}

TEST_CASE("Each PromptClear is driven by exactly its own event", "[prompts]") {
    struct Row { PromptClear c; bool PromptEvents::*flag; };
    const Row rows[] = {
        {PromptClear::Jump, &PromptEvents::jump},     {PromptClear::Slide, &PromptEvents::slide},
        {PromptClear::Vault, &PromptEvents::vault},   {PromptClear::WallRun, &PromptEvents::wallRun},
        {PromptClear::Fire, &PromptEvents::fire},     {PromptClear::Grab, &PromptEvents::grab},
        {PromptClear::Throw, &PromptEvents::thrown},
    };
    for (const Row& a : rows) {
        for (const Row& b : rows) {
            PromptEvents ev;
            ev.*(b.flag) = true;
            CHECK(ev.performed(a.c) == (a.c == b.c));
        }
        CHECK_FALSE(PromptEvents{}.performed(a.c));
    }
    PromptEvents all;
    all.jump = all.slide = all.vault = all.wallRun = all.fire = all.grab = all.thrown = true;
    CHECK_FALSE(all.performed(PromptClear::Timer));
}

TEST_CASE("show() replaces the current prompt and clear() removes it", "[prompts]") {
    PromptSystem p;
    p.show("FIRST", 5.0f, PromptClear::Jump);
    idle(p, 1.0f);
    p.show("SECOND", 5.0f);
    CHECK(p.text() == "SECOND");
    CHECK(p.alpha() == 0.0f);  // starts over from invisible
    CHECK_FALSE(p.fading());

    // A replaced prompt forgets the old clear condition.
    PromptEvents jump;
    jump.jump = true;
    idle(p, 2.0f, jump);
    CHECK_FALSE(p.fading());

    p.clear();
    CHECK_FALSE(p.active());
    CHECK(p.alpha() == 0.0f);
    p.update(kDt, {});  // updating an empty system is harmless
    CHECK_FALSE(p.active());

    p.show("", 5.0f);   // empty text never shows
    CHECK_FALSE(p.active());
}

TEST_CASE("Very short durations are held for at least the minimum", "[prompts]") {
    PromptSystem p;
    p.show("QUICK", 0.01f);
    idle(p, PromptSystem::kMinShown * 0.5f);
    CHECK_FALSE(p.fading());
    idle(p, PromptSystem::kMinShown);
    CHECK(p.fading());
}

TEST_CASE("Prompt text expands {action} placeholders to the player's keys", "[prompts][text]") {
    const InputMap defaults;
    CHECK(expandPromptText("PRESS {grab}", defaults) == "PRESS E");
    CHECK(expandPromptText("{sprint} + {jump}", defaults) == "L SHIFT + SPACE");
    CHECK(expandPromptText("{forward}{left}{back}{right}", defaults) == "WASD");
    CHECK(expandPromptText("{weapon1}-{weapon5}", defaults) == "1-5");
    CHECK(expandPromptText("{lmb} / {rmb}", defaults) == "LEFT CLICK / RIGHT CLICK");
    CHECK(expandPromptText("NO PLACEHOLDERS", defaults) == "NO PLACEHOLDERS");
    CHECK(expandPromptText("", defaults).empty());

    SECTION("rebinding is reflected") {
        InputMap keys;
        REQUIRE(keys.rebind(Action::Grab, 'F'));
        REQUIRE(keys.rebind(Action::Jump, key::Enter));
        CHECK(expandPromptText("{grab} {jump}", keys) == "F ENTER");
    }
    SECTION("every bindable action has a placeholder") {
        const InputMap keys;
        for (std::size_t i = 0; i < kActionCount; ++i) {
            INFO(kActionInfo[i].ini);
            const std::string shown = expandPromptText(std::string("{") + kActionInfo[i].ini + "}", keys);
            CHECK(shown == keyName(keys.key(i)));
            CHECK(shown != "?");
        }
    }
    SECTION("unknown placeholders never leak braces to the font") {
        for (const char* text : {"A {nope} B", "A {nope B", "A nope} B", "{{jump}}", "{", "}", "{}"}) {
            INFO(text);
            const std::string shown = expandPromptText(text, defaults);
            CHECK(shown.find('{') == std::string::npos);
            CHECK(shown.find('}') == std::string::npos);
        }
        CHECK(expandPromptText("A {nope} B", defaults) == "A ? B");
    }
}

TEST_CASE("checkPromptText accepts good text and explains bad text", "[prompts][text]") {
    CHECK(checkPromptText("JUMP: {jump}  (SPACE) - 100% !").empty());
    CHECK(checkPromptText("lower case is fine").empty());
    CHECK(checkPromptText("{forward} {left} {back} {right}").empty());

    CHECK_FALSE(checkPromptText("").empty());
    CHECK_FALSE(checkPromptText("{unknown}").empty());
    CHECK_FALSE(checkPromptText("{jump").empty());
    CHECK_FALSE(checkPromptText("jump}").empty());
    CHECK_FALSE(checkPromptText("price: $5").empty());
    CHECK_FALSE(checkPromptText("caf\xc3\xa9").empty());  // non-ASCII has no glyph
    CHECK_FALSE(checkPromptText("tab\there").empty());
    CHECK_FALSE(checkPromptText(std::string(kMaxPromptChars + 1, 'A')).empty());
    CHECK(checkPromptText(std::string(kMaxPromptChars, 'A')).empty());
}

// ---------------------------------------------------------------------------
// Trigger state: prompt triggers fire once and carry their data
// ---------------------------------------------------------------------------

TEST_CASE("Prompt triggers fire once when entered and carry their data", "[prompts][triggers]") {
    TriggerSystem ts;
    TriggerVolume v;
    v.min       = {0.0f, 0.0f, 0.0f};
    v.max       = {2.0f, 2.0f, 2.0f};
    v.action    = TriggerAction::Prompt;
    v.text      = "VAULT: {sprint}";
    v.time      = 8.0f;
    v.clearOn   = PromptClear::Vault;
    ts.add(v);
    const auto none = [](int) { return 0; };

    CHECK(ts.update({9.0f, 0.0f, 9.0f}, none).empty());  // outside

    const auto first = ts.update({1.0f, 1.0f, 1.0f}, none);
    REQUIRE(first.size() == 1);
    CHECK(first[0].action == TriggerAction::Prompt);
    CHECK(first[0].text == "VAULT: {sprint}");
    CHECK(first[0].time == 8.0f);
    CHECK(first[0].clearOn == PromptClear::Vault);

    CHECK(ts.update({1.0f, 1.0f, 1.0f}, none).empty());  // staying inside does not re-show it
    CHECK(ts.update({9.0f, 0.0f, 9.0f}, none).empty());
    CHECK(ts.update({1.0f, 1.0f, 1.0f}, none).empty());  // leaving and returning does not either

    ts.reset();  // a restart re-arms it
    CHECK(ts.update({1.0f, 1.0f, 1.0f}, none).size() == 1);
}

TEST_CASE("Entering a volume drives the on-screen prompt end to end", "[prompts][triggers]") {
    TriggerSystem ts;
    TriggerVolume v;
    v.min     = {0.0f, 0.0f, 0.0f};
    v.max     = {2.0f, 2.0f, 2.0f};
    v.action  = TriggerAction::Prompt;
    v.text    = "PRESS {grab}";
    v.time    = 6.0f;
    v.clearOn = PromptClear::Grab;
    ts.add(v);

    const InputMap keys;
    PromptSystem prompts;
    const auto none = [](int) { return 0; };
    const auto step = [&](glm::vec3 where, const PromptEvents& ev) {
        for (const TriggerEvent& e : ts.update(where, none))
            if (e.action == TriggerAction::Prompt) prompts.show(expandPromptText(e.text, keys), e.time, e.clearOn);
        prompts.update(kDt, ev);
    };

    step({9.0f, 0.0f, 9.0f}, {});
    CHECK_FALSE(prompts.active());

    step({1.0f, 1.0f, 1.0f}, {});  // walk in: the hint appears, with the key filled in
    REQUIRE(prompts.active());
    CHECK(prompts.text() == "PRESS E");

    for (int i = 0; i < 60; ++i) step({9.0f, 0.0f, 9.0f}, {});  // walking away does not hide it
    CHECK(prompts.active());
    CHECK_FALSE(prompts.fading());

    PromptEvents grab;
    grab.grab = true;
    step({9.0f, 0.0f, 9.0f}, grab);  // doing the thing dismisses it
    CHECK(prompts.fading());
    for (int i = 0; i < 60; ++i) step({9.0f, 0.0f, 9.0f}, {});
    CHECK_FALSE(prompts.active());

    step({1.0f, 1.0f, 1.0f}, {});  // and it does not come back
    CHECK_FALSE(prompts.active());
}

TEST_CASE("Two prompts in one tick: the later one wins the screen", "[prompts][triggers]") {
    TriggerSystem ts;
    for (const char* text : {"FIRST", "SECOND"}) {
        TriggerVolume v;
        v.max    = {1.0f, 1.0f, 1.0f};
        v.action = TriggerAction::Prompt;
        v.text   = text;
        ts.add(v);
    }
    const auto events = ts.update({0.5f, 0.5f, 0.5f}, [](int) { return 0; });
    REQUIRE(events.size() == 2);
    PromptSystem prompts;
    for (const TriggerEvent& e : events) prompts.show(e.text, e.time, e.clearOn);
    CHECK(prompts.text() == "SECOND");
}
