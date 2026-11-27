#pragma once
#include "core/DrawTypes.hpp"
#include "core/Record.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace carnage {

// A briefing is a short sequence of "beats" (a background image and a few lines of
// text) shown between levels. Files live in assets/briefings/<id>.brf, one record
// per line, in the same `record key=value` style as levels (see core/Record.hpp):
//
//   briefing name=<id> [title="..."] [music=0|1]
//   beat     text="..." [image=<path under assets/>] [hold=<sec>] [sfx=none|click|impact|explosion]
//
// `text` may contain \n for a line break (up to 8 lines). `hold` is how long a beat stays after its
// text has finished appearing (default 4 s); the player can always advance sooner.
// A missing or unreadable image just shows a plain dark background.

enum class BriefingSfx { None, Click, Impact, Explosion };

inline constexpr std::size_t kMaxBriefingBeats = 40;
inline constexpr std::size_t kMaxBeatChars     = 360;
inline constexpr std::size_t kMaxBeatLines     = 8;    // explicit \n line breaks (word wrap adds more)
inline constexpr std::size_t kMaxTitleChars    = 40;

struct BriefingBeat {
    std::string text;
    std::string image;     // path under assets/, empty = none
    float       hold = 4.0f;
    BriefingSfx sfx  = BriefingSfx::None;
    int         line = 0;  // source line, for diagnostics
};

struct BriefingData {
    std::string               name = "unnamed";
    std::string               title;
    bool                      music = true;  // play the title music while it runs
    std::vector<BriefingBeat> beats;
};

struct BriefingLoad {
    BriefingData            data;
    std::vector<rec::Issue> issues;
    bool                    opened = true;  // false: the file could not be read

    bool clean() const { return opened && issues.empty(); }
};

BriefingLoad parseBriefing(std::string_view text);
BriefingLoad loadBriefingFile(const std::string& path);
std::string  briefingPath(std::string_view assetDir, std::string_view id);

// Image paths are relative, plain, and one of the formats the renderer can load.
bool validImagePath(std::string_view path);

// Reports beats whose image file does not exist under `assetDir` (they will
// show a plain background). Separate from parsing so parsing needs no disk.
std::vector<rec::Issue> checkBriefingImages(const BriefingData& data, std::string_view assetDir);

// Greedy word wrap. Explicit '\n' starts a new line; a word longer than a line
// is split. `maxChars` below 1 is treated as 1.
std::vector<std::string> wrapText(std::string_view text, std::size_t maxChars);

// ---------------------------------------------------------------------------
// Playback: a pure state machine (no GL, audio or input), so every path is testable.
// ---------------------------------------------------------------------------

struct BriefingInput {
    bool advance = false;  // SPACE / ENTER pressed this tick
    bool skip    = false;  // ESC pressed this tick
};

enum class BriefingEnd { None, Finished, Skipped };

struct BriefingEvents {
    bool        beatStarted = false;  // a new beat began this tick
    BriefingSfx sfx         = BriefingSfx::None;
    bool        ended       = false;  // the briefing is over (query end() for how)
};

class BriefingPlayer {
public:
    static constexpr float kFadeIn       = 0.5f;   // each beat fades in from black
    static constexpr float kFadeOut      = 0.4f;   // and the whole briefing fades out
    static constexpr float kCharsPerSec  = 45.0f;  // typewriter speed
    static constexpr float kAdvanceGrace = 0.3f;   // ignore SPACE/ENTER this long after start, so
                                                   // keys mashed at the end of a fight don't skip it
                                                   // (ESC always works)

    // Begins playing. A briefing with no beats ends on the first update().
    void start(BriefingData data);
    // Drops everything: back to inactive with no recorded ending.
    void reset();

    BriefingEvents update(float dt, const BriefingInput& in);

    bool                active() const { return phase_ == Phase::Showing || phase_ == Phase::Leaving; }
    bool                leaving() const { return phase_ == Phase::Leaving; }
    BriefingEnd         end() const { return end_; }
    const BriefingData& data() const { return data_; }
    std::size_t         beatIndex() const { return index_; }
    const BriefingBeat* beat() const { return active() && index_ < data_.beats.size() ? &data_.beats[index_] : nullptr; }
    std::size_t         revealedChars() const;
    bool                fullyRevealed() const;
    float               fade() const;  // 0..1 opacity of the beat content

private:
    enum class Phase { Idle, Showing, Leaving, Done };

    void beginBeat();
    void beginLeaving(BriefingEnd how);

    BriefingData data_;
    Phase        phase_   = Phase::Idle;
    BriefingEnd  end_     = BriefingEnd::None;
    BriefingEnd  leaveTo_ = BriefingEnd::None;
    std::size_t  index_   = 0;
    bool         beatEvent_ = false;  // report beatStarted on the next update
    float        total_  = 0.0f;      // time since start
    float        beatT_  = 0.0f;      // time in this beat
    float        shown_  = 0.0f;      // typewriter position, in characters
    float        holdT_  = 0.0f;      // time since the text finished appearing
    float        leaveT_ = 0.0f;
};

// Draws the current beat as screen-space rects: fade-from-black, text panel with
// typewriter text, title, beat counter and a key hint. The background image is
// drawn by the renderer underneath. Draws nothing when the player is inactive.
void buildBriefingOverlay(const BriefingPlayer& player, float aspect, std::vector<OverlayRect>& out);

} // namespace carnage
