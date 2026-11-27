#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "game/Briefing.hpp"
#include "game/Level.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace carnage;
using Catch::Approx;

namespace {

constexpr float kDt = 1.0f / 60.0f;

const std::string kShippedBrief = std::string(CARNAGE_ASSET_DIR) + "/briefings/level01_outro.brf";
const std::string kShippedLevel = std::string(CARNAGE_ASSET_DIR) + "/levels/level01_escape.lvl";

BriefingData makeData(std::initializer_list<std::string> texts, float hold = 1.0f) {
    BriefingData d;
    d.name = "test";
    for (const std::string& t : texts) {
        BriefingBeat b;
        b.text = t;
        b.hold = hold;
        d.beats.push_back(b);
    }
    return d;
}

struct Tally {
    int                      started = 0;
    int                      ended   = 0;
    std::vector<std::size_t> startedAt;   // beat index at each beatStarted
    std::vector<BriefingSfx> sfx;
};

// One tick; folds the events into `t`.
void tick(BriefingPlayer& p, Tally& t, const BriefingInput& in = {}, float dt = kDt) {
    const BriefingEvents e = p.update(dt, in);
    if (e.beatStarted) { ++t.started; t.startedAt.push_back(p.beatIndex()); t.sfx.push_back(e.sfx); }
    if (e.ended) ++t.ended;
}

void run(BriefingPlayer& p, Tally& t, float seconds, const BriefingInput& in = {}) {
    for (float s = 0.0f; s < seconds; s += kDt) tick(p, t, in);
}

BriefingInput advance() { BriefingInput i; i.advance = true; return i; }
BriefingInput skip()    { BriefingInput i; i.skip = true; return i; }

bool mentions(const rec::Issue& i, const std::string& needle) {
    return i.message.find(needle) != std::string::npos;
}

} // namespace

// ---------------------------------------------------------------------------
// Shipped content
// ---------------------------------------------------------------------------

TEST_CASE("The shipped briefing loads cleanly and is wired to Level 01", "[briefing]") {
    const BriefingLoad b = loadBriefingFile(kShippedBrief);
    REQUIRE(b.opened);
    for (const rec::Issue& i : b.issues) FAIL("line " << i.line << ": " << i.message);
    CHECK(b.clean());
    CHECK(b.data.name == "level01_outro");
    CHECK_FALSE(b.data.title.empty());
    CHECK(b.data.beats.size() >= 2);
    CHECK(checkBriefingImages(b.data, CARNAGE_ASSET_DIR).empty());  // every referenced image exists

    const LevelLoad level = loadLevelFile(kShippedLevel);
    REQUIRE(level.clean());
    CHECK(level.level.briefing == b.data.name);
    CHECK(completionTransition(level.level).briefing == b.data.name);
    CHECK(briefingPath(CARNAGE_ASSET_DIR, level.level.briefing) == kShippedBrief);
}

TEST_CASE("A shipped briefing plays start to finish and can be skipped at any tick", "[briefing]") {
    const BriefingLoad b = loadBriefingFile(kShippedBrief);
    REQUIRE(b.clean());

    SECTION("left alone it finishes") {
        BriefingPlayer p;
        Tally t;
        p.start(b.data);
        for (int i = 0; i < 60 * 120 && p.active(); ++i) tick(p, t);
        CHECK_FALSE(p.active());
        CHECK(p.end() == BriefingEnd::Finished);
        CHECK(t.ended == 1);
        CHECK(t.started == static_cast<int>(b.data.beats.size()));
    }
    SECTION("ESC at every possible moment skips cleanly") {
        for (int skipAt = 0; skipAt < 60 * 14; skipAt += 7) {
            BriefingPlayer p;
            Tally t;
            p.start(b.data);
            for (int i = 0; i < skipAt && p.active(); ++i) tick(p, t);
            if (!p.active()) continue;
            tick(p, t, skip());
            for (int i = 0; i < 60 * 5 && p.active(); ++i) tick(p, t);
            REQUIRE_FALSE(p.active());
            CHECK(t.ended == 1);
            CHECK((p.end() == BriefingEnd::Skipped || p.end() == BriefingEnd::Finished));
        }
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

TEST_CASE("Briefing files parse into beats", "[briefing][parse]") {
    const BriefingLoad r = parseBriefing(
        "# a comment\n"
        "briefing name=op_kodiak title=\"OPERATION KODIAK\" music=0\n"
        "beat text=\"FIRST LINE\\nSECOND LINE\" image=briefings/a.png hold=6 sfx=explosion\n"
        "beat text=\"Plain, with # hash and: punctuation!\"   # trailing comment\n"
        "beat text=X sfx=click\n"
        "beat text=\"Y\" sfx=impact image=b/c.JPG\n");
    REQUIRE(r.clean());
    const BriefingData& d = r.data;
    CHECK(d.name == "op_kodiak");
    CHECK(d.title == "OPERATION KODIAK");
    CHECK_FALSE(d.music);
    REQUIRE(d.beats.size() == 4);

    CHECK(d.beats[0].text == "FIRST LINE\nSECOND LINE");  // \n became a real break
    CHECK(d.beats[0].image == "briefings/a.png");
    CHECK(d.beats[0].hold == 6.0f);
    CHECK(d.beats[0].sfx == BriefingSfx::Explosion);
    CHECK(d.beats[0].line == 3);

    CHECK(d.beats[1].text == "Plain, with # hash and: punctuation!");
    CHECK(d.beats[1].image.empty());
    CHECK(d.beats[1].hold == 4.0f);                 // default
    CHECK(d.beats[1].sfx == BriefingSfx::None);     // default
    CHECK(d.beats[2].sfx == BriefingSfx::Click);
    CHECK(d.beats[3].sfx == BriefingSfx::Impact);
    CHECK(d.beats[3].image == "b/c.JPG");
}

TEST_CASE("The briefing header is optional and defaults sensibly", "[briefing][parse]") {
    const BriefingLoad r = parseBriefing("beat text=HELLO\n");
    CHECK(r.clean());
    CHECK(r.data.name == "unnamed");
    CHECK(r.data.title.empty());
    CHECK(r.data.music);
    CHECK(r.data.beats.size() == 1);
}

TEST_CASE("Each kind of malformed briefing line is skipped with a useful message", "[briefing][malformed]") {
    struct Bad { const char* line; const char* expect; };
    const Bad cases[] = {
        {"wibble x=1",                                         "unknown record type"},
        {"beat",                                               "missing 'text'"},
        {"beat text=\"\"",                                     "'text' is empty"},
        {"beat text=\"   \"",                                  "'text' is empty"},
        {"beat text=\"\\n\\n\"",                               "'text' is empty"},
        {"beat text=\"COST $5\"",                              "cannot be drawn"},
        {"beat text=\"caf\xc3\xa9\"",                          "cannot be drawn"},
        {"beat text=\"unterminated",                           "unterminated quote"},
        {"beat text=\"A\"B",                                   "after the closing quote"},
        {"beat text=A texxt=B",                                "unknown key 'texxt'"},
        {"beat text=A text=B",                                 "duplicate key"},
        {"beat text=A hold=0",                                 "'hold' must be between"},
        {"beat text=A hold=61",                                "'hold' must be between"},
        {"beat text=A hold=soon",                              "not a number"},
        {"beat text=A sfx=bang",                               "unknown sfx"},
        {"beat text=\"A\\nB\\nC\\nD\\nE\\nF\\nG\\nH\\nI\"",      "too many line breaks"},
        {"beat text=A image=../secret.png",                    "'image' must be a plain relative path"},
        {"beat text=A image=/etc/pic.png",                     "'image' must be a plain relative path"},
        {"beat text=A image=a\\b.png",                         "'image' must be a plain relative path"},
        {"beat text=A image=pic.gif",                          "'image' must be a plain relative path"},
        {"beat text=A image=pic",                              "'image' must be a plain relative path"},
        {"beat text=A image=dir.png/file",                     "'image' must be a plain relative path"},
        {"beat text=A image=c:/pic.png",                       "'image' must be a plain relative path"},
        {"briefing name=Not_An_Id",                            "'name' must be an id"},
        {"briefing music=maybe",                               "must be 0 or 1"},
        {"briefing colour=red",                                "unknown key 'colour'"},
    };
    for (const Bad& c : cases) {
        INFO("line: " << c.line);
        const BriefingLoad r = parseBriefing(std::string("beat text=OK\n") + c.line + "\n");
        // Line 1 is a good beat; the bad line is line 2.
        const auto hit = std::find_if(r.issues.begin(), r.issues.end(), [](const rec::Issue& i) { return i.line == 2; });
        REQUIRE(hit != r.issues.end());
        CHECK(mentions(*hit, c.expect));
        CHECK(r.data.beats.size() == 1);   // only the good beat survived
        CHECK(r.data.name == "unnamed");   // a bad record never half-applies
    }
}

TEST_CASE("Briefing size limits", "[briefing][malformed]") {
    SECTION("beat text length") {
        const std::string ok(kMaxBeatChars, 'A');
        CHECK(parseBriefing("beat text=" + ok + "\n").clean());
        const BriefingLoad r = parseBriefing("beat text=" + std::string(kMaxBeatChars + 1, 'A') + "\n");
        REQUIRE_FALSE(r.issues.empty());
        CHECK(mentions(r.issues[0], "too long"));
    }
    SECTION("title length") {
        CHECK(parseBriefing("briefing title=" + std::string(kMaxTitleChars, 'A') + "\nbeat text=X\n").clean());
        const BriefingLoad r = parseBriefing("briefing title=" + std::string(kMaxTitleChars + 1, 'A') + "\nbeat text=X\n");
        REQUIRE_FALSE(r.issues.empty());
        CHECK(mentions(r.issues[0], "'title' is too long"));
    }
    SECTION("beat count") {
        std::string text;
        for (std::size_t i = 0; i < kMaxBriefingBeats + 3; ++i) text += "beat text=X\n";
        const BriefingLoad r = parseBriefing(text);
        CHECK(r.data.beats.size() == kMaxBriefingBeats);
        CHECK(r.issues.size() == 3);
        CHECK(mentions(r.issues[0], "too many beats"));
    }
}

TEST_CASE("Briefing-level problems are reported without failing the load", "[briefing][malformed]") {
    SECTION("no beats") {
        const BriefingLoad r = parseBriefing("briefing name=empty\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(r.issues[0].line == 0);
        CHECK(mentions(r.issues[0], "no beats"));
    }
    SECTION("an empty file and a comment-only file") {
        CHECK(parseBriefing("").issues.size() == 1);
        CHECK(parseBriefing("# nothing\n# here\n").issues.size() == 1);
    }
    SECTION("a duplicate header is ignored; the first wins") {
        const BriefingLoad r = parseBriefing("briefing name=one\nbriefing name=two\nbeat text=X\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "duplicate"));
        CHECK(r.data.name == "one");
    }
    SECTION("good beats around a bad one still load") {
        const BriefingLoad r = parseBriefing("beat text=A\nbeat sfx=bang text=B\nbeat text=C\n");
        REQUIRE(r.issues.size() == 1);
        CHECK(r.issues[0].line == 2);
        REQUIRE(r.data.beats.size() == 2);
        CHECK(r.data.beats[1].text == "C");
    }
}

TEST_CASE("Hostile briefing input never crashes the parser", "[briefing][malformed]") {
    const std::string junk[] = {
        std::string("\0\0\xff\xfe=\n=\n==\n", 12),
        std::string(100000, 'x'),
        "beat " + std::string(20000, '='),
        "beat text=\"" + std::string(100000, 'A') + "\"\n",
        "beat\nbriefing\n\n\n#\n",
        std::string(1000, '\n'),
    };
    for (const std::string& text : junk) {
        const BriefingLoad r = parseBriefing(text);
        CHECK(r.opened);
        CHECK_FALSE(r.clean());
        CHECK(r.data.beats.size() <= kMaxBriefingBeats);
    }
}

TEST_CASE("Loading a briefing from disk", "[briefing][io]") {
    SECTION("a missing file is reported, not thrown") {
        const BriefingLoad r = loadBriefingFile("no/such/briefing.brf");
        CHECK_FALSE(r.opened);
        CHECK_FALSE(r.clean());
        REQUIRE(r.issues.size() == 1);
        CHECK(mentions(r.issues[0], "cannot open"));
        CHECK(r.data.beats.empty());
    }
    SECTION("a written file round-trips") {
        const std::string path = "carnage_test_briefing.brf";
        { std::ofstream f(path); f << "briefing name=disk\nbeat text=\"HI THERE\" hold=2\n"; }
        const BriefingLoad r = loadBriefingFile(path);
        std::remove(path.c_str());
        CHECK(r.clean());
        CHECK(r.data.name == "disk");
        REQUIRE(r.data.beats.size() == 1);
        CHECK(r.data.beats[0].hold == 2.0f);
    }
}

TEST_CASE("Image paths and missing-image reporting", "[briefing][images]") {
    CHECK(validImagePath("briefings/a.png"));
    CHECK(validImagePath("a.JPG"));
    CHECK(validImagePath("deep/er/path-with_chars.1.jpeg"));
    CHECK(validImagePath("pic.bmp"));
    CHECK_FALSE(validImagePath(""));
    CHECK_FALSE(validImagePath("../a.png"));
    CHECK_FALSE(validImagePath("a/../b.png"));
    CHECK_FALSE(validImagePath("/abs.png"));
    CHECK_FALSE(validImagePath("a b.png"));
    CHECK_FALSE(validImagePath("a.tga"));
    CHECK_FALSE(validImagePath("png"));
    CHECK_FALSE(validImagePath("a.png/"));
    CHECK_FALSE(validImagePath(std::string(200, 'a') + ".png"));

    namespace fs = std::filesystem;
    const fs::path root = "carnage_test_brief_assets";
    fs::create_directories(root / "briefings");
    { std::ofstream(root / "briefings" / "here.png") << "x"; }

    BriefingData d = makeData({"A", "B", "C"});
    d.beats[0].image = "briefings/here.png";
    d.beats[0].line  = 3;
    d.beats[1].image = "briefings/gone.png";
    d.beats[1].line  = 4;
    // beat 2 has no image
    const auto issues = checkBriefingImages(d, root.string());
    fs::remove_all(root);
    REQUIRE(issues.size() == 1);
    CHECK(issues[0].line == 4);
    CHECK(mentions(issues[0], "gone.png"));
    CHECK(mentions(issues[0], "plain background"));
}

// ---------------------------------------------------------------------------
// Word wrap
// ---------------------------------------------------------------------------

TEST_CASE("wrapText wraps on words, honours breaks and splits long words", "[briefing][wrap]") {
    using V = std::vector<std::string>;
    CHECK(wrapText("HELLO WORLD", 20) == V{"HELLO WORLD"});
    CHECK(wrapText("HELLO WORLD", 11) == V{"HELLO WORLD"});   // exactly fits
    CHECK(wrapText("HELLO WORLD", 10) == V{"HELLO", "WORLD"});
    CHECK(wrapText("A B C D E", 3) == V{"A B", "C D", "E"});
    CHECK(wrapText("ONE\nTWO", 20) == V{"ONE", "TWO"});
    CHECK(wrapText("ONE\n\nTWO", 20) == V{"ONE", "", "TWO"});  // a blank paragraph is a blank line
    CHECK(wrapText("  PADDED   WORDS  ", 20) == V{"PADDED WORDS"});
    CHECK(wrapText("ABCDEFGHIJ", 4) == V{"ABCD", "EFGH", "IJ"});  // a long word is split
    CHECK(wrapText("HI ABCDEFGHIJ", 4) == V{"HI", "ABCD", "EFGH", "IJ"});
    CHECK(wrapText("", 10) == V{""});
    CHECK(wrapText("X", 0) == V{"X"});                         // zero width is treated as 1
    CHECK(wrapText("AB", 0) == V{"A", "B"});

    // No line ever exceeds the width, and no characters are lost.
    const std::string text = "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG AND KEEPS RUNNING ALL THE WAY HOME";
    for (std::size_t width = 1; width < 90; width += 3) {
        const auto lines = wrapText(text, width);
        std::string joined;
        for (const std::string& l : lines) {
            CHECK(l.size() <= width);
            joined += l;
        }
        std::string expect;
        for (const char c : text) if (c != ' ') expect += c;
        std::string got;
        for (const char c : joined) if (c != ' ') got += c;
        CHECK(got == expect);
    }
}

// ---------------------------------------------------------------------------
// Playback state machine
// ---------------------------------------------------------------------------

TEST_CASE("Beats play in order, once each, then the briefing ends", "[briefing][player]") {
    BriefingData d = makeData({"ONE", "TWO", "THREE"}, 1.0f);
    d.beats[0].sfx = BriefingSfx::Impact;
    d.beats[2].sfx = BriefingSfx::Click;

    BriefingPlayer p;
    Tally t;
    CHECK_FALSE(p.active());
    p.start(d);
    REQUIRE(p.active());
    CHECK(p.end() == BriefingEnd::None);
    CHECK(p.beatIndex() == 0);

    for (int i = 0; i < 60 * 20 && p.active(); ++i) tick(p, t);

    CHECK_FALSE(p.active());
    CHECK(p.end() == BriefingEnd::Finished);
    CHECK(t.ended == 1);
    REQUIRE(t.started == 3);
    CHECK(t.startedAt == std::vector<std::size_t>{0, 1, 2});
    CHECK(t.sfx == std::vector<BriefingSfx>{BriefingSfx::Impact, BriefingSfx::None, BriefingSfx::Click});

    // Further updates do nothing, and never report a second ending.
    for (int i = 0; i < 120; ++i) tick(p, t);
    CHECK(t.ended == 1);
    CHECK(t.started == 3);
    CHECK(p.end() == BriefingEnd::Finished);
}

TEST_CASE("Text appears at the typewriter speed and the beat then holds", "[briefing][player]") {
    BriefingPlayer p;
    Tally t;
    p.start(makeData({std::string(90, 'A')}, 2.0f));

    run(p, t, 1.0f);
    CHECK(static_cast<float>(p.revealedChars()) == Approx(BriefingPlayer::kCharsPerSec).margin(2.0f));
    CHECK_FALSE(p.fullyRevealed());

    run(p, t, 1.1f);  // 90 chars take 2 s
    CHECK(p.fullyRevealed());
    CHECK(p.revealedChars() == 90);
    CHECK(p.beatIndex() == 0);
    CHECK(p.active());
    CHECK_FALSE(p.leaving());

    run(p, t, 1.8f);  // still inside the 2 s hold
    CHECK(p.active());
    CHECK_FALSE(p.leaving());
    run(p, t, 0.3f);  // hold over (2 s after the text finished): the only beat is done, so the exit begins
    CHECK(p.leaving());
}

TEST_CASE("Hold is counted from when the text has finished appearing", "[briefing][player]") {
    BriefingPlayer p;
    Tally t;
    p.start(makeData({"ABCDEFGHI", "NEXT"}, 2.0f));  // 9 chars = 0.2 s to appear
    run(p, t, 2.0f);
    CHECK(p.beatIndex() == 0);   // 0.2 + 2.0 s not yet reached
    run(p, t, 0.4f);
    CHECK(p.beatIndex() == 1);
}

TEST_CASE("SPACE / ENTER: the first press completes the text, the next moves on", "[briefing][player]") {
    BriefingPlayer p;
    Tally t;
    p.start(makeData({std::string(200, 'A'), "SECOND"}, 30.0f));
    run(p, t, BriefingPlayer::kAdvanceGrace + 0.05f);
    REQUIRE_FALSE(p.fullyRevealed());

    tick(p, t, advance());
    CHECK(p.fullyRevealed());
    CHECK(p.beatIndex() == 0);   // completing the text did not also skip the beat

    tick(p, t, advance());
    CHECK(p.beatIndex() == 1);   // now it moved on
    CHECK(p.revealedChars() == 0);
    CHECK(p.fade() < 0.1f);      // the new beat fades in from black

    tick(p, t, advance());       // its text is only 6 chars; one press completes it...
    run(p, t, 0.3f);
    CHECK(p.fullyRevealed());
    tick(p, t, advance());       // ...and the next press ends the briefing
    CHECK(p.leaving());
    run(p, t, 1.0f);
    CHECK(p.end() == BriefingEnd::Finished);
    CHECK(t.ended == 1);
}

TEST_CASE("SPACE / ENTER are ignored in the first moments, ESC never is", "[briefing][player]") {
    SECTION("advance during the grace period does nothing") {
        BriefingPlayer p;
        Tally t;
        p.start(makeData({std::string(400, 'A'), "B"}, 30.0f));
        tick(p, t);
        tick(p, t, advance());
        CHECK_FALSE(p.fullyRevealed());
        CHECK(p.beatIndex() == 0);
        run(p, t, BriefingPlayer::kAdvanceGrace);
        tick(p, t, advance());
        CHECK(p.fullyRevealed());
    }
    SECTION("ESC works on the very first tick") {
        BriefingPlayer p;
        Tally t;
        p.start(makeData({"ONE", "TWO"}));
        tick(p, t, skip());
        CHECK(p.leaving());
        run(p, t, 1.0f);
        CHECK(p.end() == BriefingEnd::Skipped);
        CHECK(t.ended == 1);
    }
}

TEST_CASE("Skipping from any state ends the briefing exactly once", "[briefing][player]") {
    const auto setup = [](BriefingPlayer& p) { p.start(makeData({std::string(300, 'A'), "TWO", "THREE"}, 3.0f)); };

    SECTION("while the text is appearing") {
        BriefingPlayer p; Tally t; setup(p);
        run(p, t, 1.0f);
        tick(p, t, skip());
        run(p, t, 1.0f);
        CHECK(p.end() == BriefingEnd::Skipped);
        CHECK(t.ended == 1);
        CHECK(t.started == 1);  // later beats never started
    }
    SECTION("while a beat is holding") {
        BriefingPlayer p; Tally t; setup(p);
        run(p, t, 8.0f);
        tick(p, t, skip());
        run(p, t, 1.0f);
        CHECK(p.end() == BriefingEnd::Skipped);
        CHECK(t.ended == 1);
    }
    SECTION("on the last beat") {
        BriefingPlayer p; Tally t; setup(p);
        for (int i = 0; i < 2; ++i) { run(p, t, 0.4f); tick(p, t, advance()); tick(p, t, advance()); }
        REQUIRE(p.beatIndex() == 2);
        tick(p, t, skip());
        run(p, t, 1.0f);
        CHECK(p.end() == BriefingEnd::Skipped);
        CHECK(t.ended == 1);
    }
    SECTION("repeated ESC and SPACE while fading out change nothing") {
        BriefingPlayer p; Tally t; setup(p);
        tick(p, t, skip());
        for (int i = 0; i < 10; ++i) { tick(p, t, skip()); tick(p, t, advance()); }
        run(p, t, 1.0f);
        CHECK(p.end() == BriefingEnd::Skipped);
        CHECK(t.ended == 1);
    }
    SECTION("skipping after a natural finish does not rewrite how it ended") {
        BriefingPlayer p; Tally t;
        p.start(makeData({"A"}, 1.0f));
        for (int i = 0; i < 60 * 5; ++i) tick(p, t);
        REQUIRE(p.end() == BriefingEnd::Finished);
        tick(p, t, skip());
        CHECK(p.end() == BriefingEnd::Finished);
        CHECK(t.ended == 1);
    }
}

TEST_CASE("Edge cases: no beats, restart, reset, odd time steps", "[briefing][player]") {
    SECTION("a briefing with no beats ends on its own") {
        BriefingPlayer p; Tally t;
        p.start(BriefingData{});
        for (int i = 0; i < 60 * 3 && p.active(); ++i) tick(p, t);
        CHECK_FALSE(p.active());
        CHECK(p.end() == BriefingEnd::Finished);
        CHECK(t.started == 0);
        CHECK(t.ended == 1);
        CHECK(p.beat() == nullptr);
    }
    SECTION("start() can be used again after a briefing ended") {
        BriefingPlayer p; Tally t;
        p.start(makeData({"A"}));
        p.update(kDt, skip());
        for (int i = 0; i < 60 && p.active(); ++i) p.update(kDt, {});
        REQUIRE(p.end() == BriefingEnd::Skipped);
        p.start(makeData({"B", "C"}));
        CHECK(p.active());
        CHECK(p.end() == BriefingEnd::None);
        CHECK(p.beatIndex() == 0);
        CHECK(p.revealedChars() == 0);
    }
    SECTION("reset() mid-play leaves nothing behind") {
        BriefingPlayer p; Tally t;
        p.start(makeData({"A", "B"}));
        run(p, t, 0.5f);
        p.reset();
        CHECK_FALSE(p.active());
        CHECK(p.end() == BriefingEnd::None);
        CHECK(p.beat() == nullptr);
        CHECK(p.fade() == 0.0f);
        const BriefingEvents e = p.update(kDt, skip());
        CHECK_FALSE(e.beatStarted);
        CHECK_FALSE(e.ended);
    }
    SECTION("updating a player that never started is harmless") {
        BriefingPlayer p;
        const BriefingEvents e = p.update(1.0f, skip());
        CHECK_FALSE(e.ended);
        CHECK_FALSE(p.active());
    }
    SECTION("a zero or negative time step does not advance anything") {
        BriefingPlayer p; Tally t;
        p.start(makeData({"ABCDEFGH"}));
        tick(p, t, {}, 0.0f);
        tick(p, t, {}, -5.0f);
        CHECK(p.revealedChars() == 0);
        CHECK(p.active());
    }
    SECTION("a huge time step (a stall) still terminates in a few updates") {
        BriefingPlayer p; Tally t;
        p.start(makeData({"A", "B", "C"}, 5.0f));
        int updates = 0;
        while (p.active() && updates < 20) { tick(p, t, {}, 1000.0f); ++updates; }
        CHECK_FALSE(p.active());
        CHECK(updates <= 6);
        CHECK(t.ended == 1);
    }
}

TEST_CASE("Content fades in per beat and out at the end", "[briefing][player]") {
    BriefingPlayer p;
    Tally t;
    p.start(makeData({"A", "B"}, 2.0f));
    CHECK(p.fade() == 0.0f);

    float last = 0.0f;
    for (float s = 0.0f; s < BriefingPlayer::kFadeIn; s += kDt) {
        tick(p, t);
        REQUIRE(p.fade() >= last);
        REQUIRE(p.fade() <= 1.0f);
        last = p.fade();
    }
    tick(p, t);
    CHECK(p.fade() == 1.0f);

    // The second beat dips back to black and fades in again.
    for (int i = 0; i < 60 * 4 && p.beatIndex() == 0; ++i) tick(p, t);
    REQUIRE(p.beatIndex() == 1);
    CHECK(p.fade() < 0.2f);

    // The exit fades out monotonically to zero.
    tick(p, t, skip());
    last = p.fade();
    while (p.active()) {
        tick(p, t);
        REQUIRE(p.fade() <= last + 1e-6f);
        last = p.fade();
    }
    CHECK(p.fade() == 0.0f);
}

TEST_CASE("Whatever the player does, a briefing always ends", "[briefing][player]") {
    // Deterministic pseudo-random scripts: random beats, random key presses.
    std::uint32_t seed = 12345;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return (seed >> 8) & 0xFFFFu; };

    for (int scenario = 0; scenario < 200; ++scenario) {
        BriefingData d;
        float idleSeconds = 1.0f;  // the longest it can take if nobody touches a key
        const int beats = 1 + static_cast<int>(rnd() % 10);
        for (int i = 0; i < beats; ++i) {
            BriefingBeat b;
            b.text = std::string(1 + rnd() % kMaxBeatChars, 'A');
            b.hold = 1.0f + static_cast<float>(rnd() % 90) / 10.0f;
            idleSeconds += static_cast<float>(b.text.size()) / BriefingPlayer::kCharsPerSec + b.hold + 0.1f;
            d.beats.push_back(b);
        }
        const std::uint32_t pressPermille[] = {0, 5, 50, 300, 1000};
        const std::uint32_t chance = pressPermille[scenario % 5];

        BriefingPlayer p;
        Tally t;
        p.start(d);
        int ticks = 0;
        const int limit = static_cast<int>(idleSeconds * 60.0f) + 120;
        while (p.active() && ticks < limit) {
            BriefingInput in;
            in.advance = (rnd() % 1000) < chance;
            in.skip    = (rnd() % 1000) < chance / 10;  // ESC is rarer
            tick(p, t, in);
            ++ticks;
        }
        INFO("scenario " << scenario << " beats=" << beats << " chance=" << chance);
        REQUIRE_FALSE(p.active());           // never stuck
        CHECK(t.ended == 1);                 // and reported once
        CHECK(p.end() != BriefingEnd::None);
        CHECK(t.started <= beats);
    }
}

// ---------------------------------------------------------------------------
// Overlay
// ---------------------------------------------------------------------------

TEST_CASE("The overlay draws nothing unless a briefing is active", "[briefing][overlay]") {
    BriefingPlayer p;
    std::vector<OverlayRect> out;
    buildBriefingOverlay(p, 16.0f / 9.0f, out);
    CHECK(out.empty());

    p.start(makeData({"HELLO"}));
    buildBriefingOverlay(p, 16.0f / 9.0f, out);
    CHECK_FALSE(out.empty());

    p.reset();
    out.clear();
    buildBriefingOverlay(p, 16.0f / 9.0f, out);
    CHECK(out.empty());
}

TEST_CASE("The overlay starts black, reveals text gradually, and stays on screen", "[briefing][overlay]") {
    BriefingData d = makeData({"THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG. THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG."}, 3.0f);
    d.title = "TITLE";
    BriefingPlayer p;
    Tally t;
    p.start(d);
    std::vector<OverlayRect> out;

    buildBriefingOverlay(p, 16.0f / 9.0f, out);          // before any tick: fully black, no text yet
    REQUIRE_FALSE(out.empty());
    CHECK(out.front().color.a == Approx(1.0f));
    const std::size_t beforeTyping = out.size();

    std::size_t last = beforeTyping;
    bool grew = false;
    for (int i = 0; i < 60 * 3; ++i) {
        tick(p, t);
        out.clear();
        buildBriefingOverlay(p, 16.0f / 9.0f, out);
        if (out.size() > last) grew = true;
        REQUIRE(out.size() >= last);                     // text never un-appears while it types
        last = out.size();
    }
    CHECK(grew);
    CHECK(last > beforeTyping + 20);                     // plenty of glyph rects appeared
    CHECK(out.front().color.a == Approx(0.0f).margin(1e-4f));  // black cover fully lifted
}

TEST_CASE("Overlay rectangles are always valid and inside the screen", "[briefing][overlay]") {
    BriefingData d;
    d.title = std::string(kMaxTitleChars, 'W');
    BriefingBeat longText;
    longText.text = std::string(kMaxBeatChars - 40, 'W') + " " + std::string(30, 'I');
    BriefingBeat splitWord;
    splitWord.text = std::string(300, 'M');                      // one huge word: must be hard-split
    BriefingBeat breaks;
    breaks.text = "A\n\nB\n\n\nC\nD\nE";                              // the most explicit lines the parser allows
    BriefingBeat manyLines;                                      // built in code, bypassing the parser's line limit
    for (int i = 0; i < 20; ++i) manyLines.text += std::string(i ? "\n" : "") + "LINE";
    d.beats = {longText, splitWord, breaks, manyLines};

    for (const float aspect : {1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
        for (std::size_t b = 0; b < d.beats.size(); ++b) {
            INFO("aspect " << aspect << " beat " << b);
            BriefingPlayer p;
            Tally t;
            p.start(d);
            for (std::size_t i = 0; i < b; ++i) { run(p, t, 0.4f); tick(p, t, advance()); tick(p, t, advance()); }
            REQUIRE(p.beatIndex() == b);
            for (int i = 0; i < 60 * 20 && !p.fullyRevealed(); ++i) tick(p, t);  // let the text finish typing
            REQUIRE(p.active());
            REQUIRE(p.beatIndex() == b);

            std::vector<OverlayRect> out;
            buildBriefingOverlay(p, aspect, out);
            REQUIRE_FALSE(out.empty());
            for (const OverlayRect& r : out) {
                REQUIRE(std::isfinite(r.min.x + r.min.y + r.max.x + r.max.y));
                REQUIRE(r.min.x <= r.max.x);
                REQUIRE(r.min.y <= r.max.y);
                REQUIRE(r.min.x >= -1.001f);
                REQUIRE(r.max.x <= 1.001f);
                REQUIRE(r.min.y >= -1.001f);
                REQUIRE(r.max.y <= 1.001f);
                REQUIRE(r.color.a >= 0.0f);
                REQUIRE(r.color.a <= 1.0f);
            }
        }
    }
}

TEST_CASE("The key hint disappears while the briefing is fading out", "[briefing][overlay]") {
    BriefingPlayer p;
    Tally t;
    p.start(makeData({"HELLO THERE"}, 5.0f));
    run(p, t, 1.0f);
    std::vector<OverlayRect> playing;
    buildBriefingOverlay(p, 16.0f / 9.0f, playing);

    tick(p, t, skip());
    REQUIRE(p.leaving());
    std::vector<OverlayRect> leaving;
    buildBriefingOverlay(p, 16.0f / 9.0f, leaving);
    CHECK(leaving.size() < playing.size());
}

// ---------------------------------------------------------------------------
// Level integration
// ---------------------------------------------------------------------------

TEST_CASE("Levels name the briefing that plays when they end", "[briefing][level]") {
    const LevelLoad a = parseLevel("level name=x briefing=outro_one\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
    REQUIRE(a.clean());
    CHECK(a.level.briefing == "outro_one");
    const LevelTransition t = completionTransition(a.level);
    CHECK(t.briefing == "outro_one");
    CHECK(t.type == TransitionType::Menu);       // no next_level: back to the menu afterwards
    CHECK(t.next.empty());

    const LevelLoad b = parseLevel("level name=x briefing=outro next_level=hub\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
    REQUIRE(b.clean());
    const LevelTransition t2 = completionTransition(b.level);
    CHECK(t2.briefing == "outro");               // the briefing plays first...
    CHECK(t2.type == TransitionType::Level);     // ...then the next level
    CHECK(t2.next == "hub");

    const LevelLoad none = parseLevel("level name=x\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
    CHECK(completionTransition(none.level).briefing.empty());
}

TEST_CASE("A bad briefing id is rejected without half-applying the level record", "[briefing][level][malformed]") {
    for (const char* bad : {"Outro", "../x", "a/b", "has space"}) {
        INFO(bad);
        const std::string line = std::string("level name=x briefing=") + bad;
        // "has space" splits into a second token, which is itself malformed.
        const LevelLoad r = parseLevel(line + "\nspawn pos=0,0,0\nblock min=0,0 max=1,1 top=1\n");
        REQUIRE_FALSE(r.issues.empty());
        CHECK(r.issues[0].line == 1);
        CHECK(r.level.briefing.empty());
        CHECK(r.level.name == "unnamed");
    }
}
