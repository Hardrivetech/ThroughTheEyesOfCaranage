#pragma once
// Developer console: pure logic (no GLFW, GL or game state) so it is unit-testable.
// Commands are registered by the game (main.cpp) as closures over live state.
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <deque>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace carnage::dev {

// Ok = text to print (may be empty, may contain '\n'); Error = shown in red.
using CmdResult = std::expected<std::string, std::string>;

inline char toLowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

inline std::string toLower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = toLowerAscii(c);
    return r;
}

// Splits on whitespace; double quotes group words ("a b" -> one token).
inline std::vector<std::string> tokenize(std::string_view line) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false, have = false;
    for (const char c : line) {
        if (c == '"') { inQuote = !inQuote; have = true; continue; }
        if (!inQuote && (c == ' ' || c == '\t')) {
            if (have) { out.push_back(std::move(cur)); cur.clear(); have = false; }
            continue;
        }
        cur.push_back(c);
        have = true;
    }
    if (have) out.push_back(std::move(cur));
    return out;
}

inline std::optional<float> parseFloat(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (s.front() == '+') s.remove_prefix(1);
    float v = 0.0f;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

inline std::optional<int> parseInt(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (s.front() == '+') s.remove_prefix(1);
    int v = 0;
    const auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || p != s.data() + s.size()) return std::nullopt;
    return v;
}

struct LogLine {
    std::string text;
    bool        error = false;
};

class Console {
public:
    using Handler = std::function<CmdResult(std::span<const std::string> args)>;

    static constexpr std::size_t kMaxLog     = 64;
    static constexpr std::size_t kMaxHistory = 32;
    static constexpr std::size_t kMaxInput   = 80;

    void add(std::string name, std::string help, Handler fn) {
        commands_.push_back({toLower(name), std::move(help), std::move(fn)});
        std::sort(commands_.begin(), commands_.end(),
                  [](const Entry& a, const Entry& b) { return a.name < b.name; });
    }

    // Runs one line. Case-insensitive command name. Output is appended to the log.
    CmdResult execute(std::string_view line) {
        const std::vector<std::string> tok = tokenize(line);
        if (tok.empty()) return std::string{};
        pushHistory(std::string(line));
        print("> " + std::string(line));

        const std::string name = toLower(tok.front());
        CmdResult         res  = std::unexpected("unknown command: " + name + " (try help)");
        for (const Entry& e : commands_) {
            if (e.name == name) {
                res = e.fn(std::span<const std::string>(tok).subspan(1));
                break;
            }
        }
        if (res) { if (!res->empty()) print(*res); }
        else     print(res.error(), true);
        return res;
    }

    // Multi-line help text, used by the built-in `help` command.
    std::string helpText() const {
        std::string s;
        for (const Entry& e : commands_) {
            if (!s.empty()) s += '\n';
            s += e.name + " - " + e.help;
        }
        return s;
    }

    std::vector<std::string> complete(std::string_view prefix) const {
        std::vector<std::string> out;
        const std::string p = toLower(prefix);
        for (const Entry& e : commands_)
            if (e.name.rfind(p, 0) == 0) out.push_back(e.name);
        return out;
    }

    // Splits on '\n' so multi-line output becomes several scrollback rows.
    void print(std::string_view text, bool error = false) {
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
            log_.push_back({std::string(text.substr(start, end - start)), error});
            if (log_.size() > kMaxLog) log_.pop_front();
            if (nl == std::string_view::npos) break;
            start = nl + 1;
        }
    }

    // ---- line editing -------------------------------------------------
    const std::string& input() const { return input_; }
    void type(char c) {
        if (c < 32 || c > 126 || input_.size() >= kMaxInput) return;
        input_.push_back(c);
    }
    void backspace() { if (!input_.empty()) input_.pop_back(); }
    void clearInput() { input_.clear(); histPos_ = -1; }

    // Enter: runs the current line and clears it.
    CmdResult submit() {
        const std::string line = input_;
        clearInput();
        return execute(line);
    }

    // Tab: completes a unique/shared prefix of the first word.
    void tabComplete() {
        if (input_.find(' ') != std::string::npos) return;
        const auto m = complete(input_);
        if (m.empty()) return;
        if (m.size() == 1) { input_ = m.front() + " "; return; }
        std::string common = m.front();
        for (const std::string& s : m) {
            std::size_t i = 0;
            while (i < common.size() && i < s.size() && common[i] == s[i]) ++i;
            common.resize(i);
        }
        input_ = common;
        std::string list;
        for (const std::string& s : m) list += s + "  ";
        print(list);
    }

    // Up/Down: walk history (newest first).
    void historyUp() {
        if (history_.empty()) return;
        if (histPos_ < 0) histPos_ = static_cast<int>(history_.size());
        if (histPos_ > 0) --histPos_;
        input_ = history_[static_cast<std::size_t>(histPos_)];
    }
    void historyDown() {
        if (histPos_ < 0) return;
        ++histPos_;
        if (histPos_ >= static_cast<int>(history_.size())) { clearInput(); return; }
        input_ = history_[static_cast<std::size_t>(histPos_)];
    }

    const std::deque<LogLine>& log() const { return log_; }
    void clearLog() { log_.clear(); }
    std::size_t commandCount() const { return commands_.size(); }

private:
    struct Entry {
        std::string name, help;
        Handler     fn;
    };

    void pushHistory(std::string line) {
        if (history_.empty() || history_.back() != line) history_.push_back(std::move(line));
        if (history_.size() > kMaxHistory) history_.erase(history_.begin());
        histPos_ = -1;
    }

    std::vector<Entry>       commands_;
    std::deque<LogLine>      log_;
    std::vector<std::string> history_;
    std::string              input_;
    int                      histPos_ = -1;
};

} // namespace carnage::dev
