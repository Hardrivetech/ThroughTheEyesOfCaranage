#include "game/Briefing.hpp"

#include "ui/Text.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace carnage {

using rec::Record;

namespace {

// `\n` (backslash, n) in a quoted value becomes a real line break.
std::string unescapeBreaks(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') { out += '\n'; ++i; }
        else out += s[i];
    }
    return out;
}

// Returns an empty string if every character can be drawn.
std::string drawableProblem(std::string_view s) {
    for (const char c : s)
        if (c != '\n' && !ui::hasGlyph(c))
            return std::string("character '") + c + "' cannot be drawn by the HUD font";
    return {};
}

bool parseBeat(Record& r, BriefingData& data, int line) {
    BriefingBeat b;
    b.line = line;
    if (const auto t = r.raw("text")) {
        b.text = unescapeBreaks(*t);
        if (b.text.find_first_not_of(" \n") == std::string::npos) r.fail("'text' is empty");
        else if (b.text.size() > kMaxBeatChars)
            r.fail("'text' is too long (" + std::to_string(b.text.size()) + " > " +
                   std::to_string(kMaxBeatChars) + " characters)");
        else if (static_cast<std::size_t>(std::count(b.text.begin(), b.text.end(), '\n')) + 1 > kMaxBeatLines)
            r.fail("'text' has too many line breaks (the limit is " + std::to_string(kMaxBeatLines) + " lines)");
        else if (const std::string why = drawableProblem(b.text); !why.empty()) r.fail(why);
    } else {
        r.fail("missing 'text'");
    }
    if (const auto img = r.raw("image")) {
        b.image = std::string(*img);
        if (!validImagePath(b.image))
            r.fail("'image' must be a plain relative path to a .png, .jpg, .jpeg or .bmp: '" + b.image + "'");
    }
    b.hold = r.number("hold", b.hold);
    if (!r.failed() && (b.hold < 1.0f || b.hold > 60.0f)) r.fail("'hold' must be between 1 and 60 seconds");
    if (const auto s = r.raw("sfx")) {
        if (*s == "none")           b.sfx = BriefingSfx::None;
        else if (*s == "click")     b.sfx = BriefingSfx::Click;
        else if (*s == "impact")    b.sfx = BriefingSfx::Impact;
        else if (*s == "explosion") b.sfx = BriefingSfx::Explosion;
        else r.fail("unknown sfx '" + std::string(*s) + "' (none, click, impact or explosion)");
    }
    r.finish();
    if (r.failed()) return false;
    if (data.beats.size() >= kMaxBriefingBeats)
        return r.fail("too many beats (the limit is " + std::to_string(kMaxBriefingBeats) + ")");
    data.beats.push_back(std::move(b));
    return true;
}

bool parseHeader(Record& r, BriefingData& data) {
    std::string name = data.name, title;
    bool        music = data.music;
    if (const auto n = r.raw("name")) {
        name = std::string(*n);
        if (!rec::validId(name)) r.fail("'name' must be an id (lower-case letters, digits, _): '" + name + "'");
    }
    if (const auto t = r.raw("title")) {
        title = std::string(*t);
        if (title.size() > kMaxTitleChars) r.fail("'title' is too long (max " + std::to_string(kMaxTitleChars) + " characters)");
        else if (const std::string why = drawableProblem(title); !why.empty() || title.find('\n') != std::string::npos)
            r.fail(why.empty() ? "'title' must be a single line" : why);
    }
    music = r.flag("music", music);
    r.finish();
    if (r.failed()) return false;
    data.name  = std::move(name);
    data.title = std::move(title);
    data.music = music;
    return true;
}

} // namespace

bool validImagePath(std::string_view p) {
    if (p.empty() || p.size() > 128 || p.front() == '/' || p.find("..") != std::string_view::npos) return false;
    if (!std::all_of(p.begin(), p.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                   c == '_' || c == '-' || c == '.' || c == '/';
        }))
        return false;
    const std::size_t dot = p.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= p.size() || p.find('/', dot) != std::string_view::npos) return false;
    std::string ext(p.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp";
}

std::string briefingPath(std::string_view assetDir, std::string_view id) {
    return std::string(assetDir) + "/briefings/" + std::string(id) + ".brf";
}

BriefingLoad parseBriefing(std::string_view text) {
    BriefingLoad out;
    bool haveHeader = false;
    const auto issue = [&](int line, std::string msg) { out.issues.push_back({line, std::move(msg)}); };

    rec::forEachRecordLine(text, [&](int lineNo, std::string_view kind, std::string_view rest) {
        Record r;
        if (!r.parse(rest)) { issue(lineNo, r.error()); return; }
        bool ok = false;
        if (kind == "briefing") {
            if (haveHeader) { issue(lineNo, "duplicate 'briefing' record ignored"); return; }
            ok = parseHeader(r, out.data);
            haveHeader = haveHeader || ok;
        } else if (kind == "beat") {
            ok = parseBeat(r, out.data, lineNo);
        } else {
            issue(lineNo, "unknown record type '" + std::string(kind) + "'");
            return;
        }
        if (!ok) issue(lineNo, std::string(kind) + ": " + r.error());
    });

    if (out.data.beats.empty()) issue(0, "briefing has no beats");
    return out;
}

BriefingLoad loadBriefingFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        BriefingLoad out;
        out.opened = false;
        out.issues.push_back({0, "cannot open briefing file '" + path + "'"});
        return out;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return parseBriefing(ss.str());
}

std::vector<rec::Issue> checkBriefingImages(const BriefingData& data, std::string_view assetDir) {
    std::vector<rec::Issue> out;
    for (const BriefingBeat& b : data.beats) {
        if (b.image.empty()) continue;
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(assetDir) / b.image, ec))
            out.push_back({b.line, "image '" + b.image + "' not found (a plain background will be used)"});
    }
    return out;
}

std::vector<std::string> wrapText(std::string_view text, std::size_t maxChars) {
    maxChars = std::max<std::size_t>(maxChars, 1);
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        const std::string_view para = text.substr(start, nl == std::string_view::npos ? nl : nl - start);

        std::string line;
        const auto flush = [&] { lines.push_back(line); line.clear(); };
        std::size_t i = 0;
        bool any = false;
        while (i < para.size()) {
            while (i < para.size() && para[i] == ' ') ++i;
            if (i >= para.size()) break;
            std::size_t j = i;
            while (j < para.size() && para[j] != ' ') ++j;
            std::string_view word = para.substr(i, j - i);
            i = j;
            any = true;
            while (word.size() > maxChars) {  // a word longer than a line is split
                if (!line.empty()) flush();
                lines.emplace_back(word.substr(0, maxChars));
                word.remove_prefix(maxChars);
            }
            if (word.empty()) continue;
            if (!line.empty() && line.size() + 1 + word.size() > maxChars) flush();
            if (!line.empty()) line += ' ';
            line += word;
        }
        if (!line.empty() || !any) flush();  // a blank paragraph is a blank line

        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return lines;
}

// ---------------------------------------------------------------------------

void BriefingPlayer::start(BriefingData data) {
    reset();
    data_  = std::move(data);
    phase_ = Phase::Showing;
    if (data_.beats.empty()) { beginLeaving(BriefingEnd::Finished); return; }
    beginBeat();
}

void BriefingPlayer::reset() { *this = BriefingPlayer{}; }

void BriefingPlayer::beginBeat() {
    beatT_ = shown_ = holdT_ = 0.0f;
    beatEvent_ = true;
}

void BriefingPlayer::beginLeaving(BriefingEnd how) {
    phase_   = Phase::Leaving;
    leaveTo_ = how;
    leaveT_  = 0.0f;
}

std::size_t BriefingPlayer::revealedChars() const {
    const BriefingBeat* b = beat();
    if (!b) return 0;
    return std::min(b->text.size(), static_cast<std::size_t>(shown_));
}

bool BriefingPlayer::fullyRevealed() const {
    const BriefingBeat* b = beat();
    return b && revealedChars() >= b->text.size();
}

float BriefingPlayer::fade() const {
    if (phase_ == Phase::Showing) return std::clamp(beatT_ / kFadeIn, 0.0f, 1.0f);
    if (phase_ == Phase::Leaving) return 1.0f - std::clamp(leaveT_ / kFadeOut, 0.0f, 1.0f);
    return 0.0f;
}

BriefingEvents BriefingPlayer::update(float dt, const BriefingInput& in) {
    BriefingEvents ev;
    if (!active()) return ev;
    dt = std::max(dt, 0.0f);
    total_ += dt;

    if (phase_ == Phase::Leaving) {
        leaveT_ += dt;
        if (leaveT_ >= kFadeOut) {
            phase_   = Phase::Done;
            end_     = leaveTo_;
            ev.ended = true;
        }
        return ev;
    }

    if (beatEvent_) {
        beatEvent_      = false;
        ev.beatStarted  = true;
        ev.sfx          = data_.beats[index_].sfx;
    }
    if (in.skip) { beginLeaving(BriefingEnd::Skipped); return ev; }

    const BriefingBeat& b = data_.beats[index_];
    beatT_ += dt;
    shown_ += kCharsPerSec * dt;
    if (fullyRevealed()) holdT_ += dt;

    bool next = false;
    if (in.advance && total_ >= kAdvanceGrace) {
        if (!fullyRevealed()) shown_ = static_cast<float>(b.text.size());  // first press: finish the text
        else next = true;                                                  // second: move on
    }
    if (fullyRevealed() && holdT_ >= b.hold) next = true;                  // or wait it out

    if (next) {
        if (index_ + 1 < data_.beats.size()) { ++index_; beginBeat(); }
        else beginLeaving(BriefingEnd::Finished);
    }
    return ev;
}

// ---------------------------------------------------------------------------

void buildBriefingOverlay(const BriefingPlayer& p, float aspect, std::vector<OverlayRect>& out) {
    const BriefingBeat* beat = p.beat();
    if (!beat) return;
    const float fade = p.fade();

    // Dip to black while a beat fades in (and over the whole screen on the way out).
    out.push_back({{-1.0f, -1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f - fade}});

    // Text size: the largest that keeps the beat within the space above the key hint.
    constexpr float kTop = -0.22f, kSpace = 0.66f;  // the text block may use y in [kTop - kSpace, kTop]
    float px = 0.0075f;
    std::vector<std::string> lines;
    for (const float candidate : {0.0075f, 0.0062f, 0.0050f}) {
        px = candidate;
        const float charW = 6.0f * px / aspect;
        const std::size_t cols = static_cast<std::size_t>(std::max(10.0f, std::floor(1.76f / charW)));
        lines = wrapText(beat->text, cols);
        if (static_cast<float>(lines.size()) * 11.0f * px <= kSpace) break;
    }

    const float step = 11.0f * px;
    // Safety net: even at the smallest size, never run off the bottom of the screen.
    const std::size_t maxLines = static_cast<std::size_t>(std::max(1.0f, std::floor(kSpace / step)));
    if (lines.size() > maxLines) lines.resize(maxLines);
    const float top  = kTop;
    const float panelBottom = top - static_cast<float>(lines.size()) * step - 0.02f;
    out.push_back({{-0.94f, panelBottom}, {0.94f, top + 0.05f}, {0.0f, 0.0f, 0.0f, 0.55f * fade}});

    // Once the text has fully appeared, draw all of it (a split long word has no
    // separator character to "pay" for its line break in the typewriter count).
    std::size_t budget = p.fullyRevealed() ? std::numeric_limits<std::size_t>::max() : p.revealedChars();
    float y = top;
    for (const std::string& line : lines) {
        const std::size_t n = std::min(budget, line.size());
        if (n > 0) {
            const std::string_view shown(line.data(), n);
            ui::drawText(out, shown, {-0.90f + 0.0015f / aspect, y - 0.0015f}, px, aspect, {0, 0, 0, 0.6f * fade});
            ui::drawText(out, shown, {-0.90f, y}, px, aspect, {0.95f, 0.93f, 0.88f, 0.95f * fade});
        }
        budget -= std::min(budget, line.size() + 1);  // +1: the break/space the wrap consumed
        y -= step;
        if (budget == 0 && n < line.size()) break;
    }

    if (!p.data().title.empty())
        ui::drawText(out, p.data().title, {-0.92f, 0.90f}, 0.0060f, aspect, {1.0f, 0.82f, 0.45f, 0.9f * fade});

    const std::string counter = std::to_string(p.beatIndex() + 1) + "/" + std::to_string(p.data().beats.size());
    const float cw = ui::textWidth(counter, 0.0045f, aspect);
    ui::drawText(out, counter, {0.92f - cw, 0.90f}, 0.0045f, aspect, {0.8f, 0.8f, 0.8f, 0.6f * fade});

    if (!p.leaving()) {
        const std::string hint = "SPACE / ENTER: NEXT     ESC: SKIP";
        const float hw = ui::textWidth(hint, 0.0045f, aspect);
        ui::drawText(out, hint, {-hw * 0.5f, -0.93f}, 0.0045f, aspect, {0.8f, 0.8f, 0.8f, 0.6f * fade});
    }
}

} // namespace carnage
