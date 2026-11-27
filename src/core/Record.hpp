#pragma once
// A tiny reader for the project's plain-text data files (levels, briefings):
// one `record key=value key="quoted value"` per line, '#' comments.
// Header-only and engine-free, so any loader can use it and tests can reach it.
#include <glm/glm.hpp>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace carnage::rec {

// A problem found while reading a data file. line 0 = not tied to one line.
struct Issue {
    int         line = 0;
    std::string message;
    std::string file{};  // the file the problem is in, when it is not the one being loaded (e.g. an included POI)
};

// "file:line: message", using `topFile` for issues in the file being loaded itself.
inline std::string describe(const Issue& i, std::string_view topFile) {
    std::string out = i.file.empty() ? std::string(topFile) : i.file;
    if (i.line > 0) out += ":" + std::to_string(i.line);
    return out + ": " + i.message;
}

// Ids (levels, briefings) are lower-case letters, digits and '_' only, so an id
// can never be a path.
inline bool validId(std::string_view id) {
    return !id.empty() && id.size() <= 64 &&
           std::all_of(id.begin(), id.end(), [](char c) {
               return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
           });
}

inline std::string_view trim(std::string_view s) {
    constexpr std::string_view ws = " \t\r\n";
    const std::size_t a = s.find_first_not_of(ws);
    if (a == std::string_view::npos) return {};
    return s.substr(a, s.find_last_not_of(ws) - a + 1);
}

inline std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t i = s.find(sep, start);
        out.push_back(s.substr(start, i == std::string_view::npos ? i : i - start));
        if (i == std::string_view::npos) return out;
        start = i + 1;
    }
}

inline bool parseFloat(std::string_view s, float& out) {
    if (s.empty()) return false;
    float v = 0.0f;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

inline bool parseInt(std::string_view s, int& out) {
    if (s.empty()) return false;
    int v = 0;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
    out = v;
    return true;
}

// The key=value pairs of one record. Getters remember the first problem;
// finish() additionally rejects keys nobody asked for (typos must be visible).
class Record {
public:
    // Returns false (and sets the error) if a token is not key=value or repeats a key.
    // A value may be double-quoted to contain spaces: text="HELLO THERE".
    bool parse(std::string_view rest) {
        constexpr std::string_view ws = " \t";
        std::size_t i = 0;
        while (i < rest.size()) {
            const std::size_t a = rest.find_first_not_of(ws, i);
            if (a == std::string_view::npos) break;
            std::size_t tokEnd = rest.find_first_of(ws, a);
            if (tokEnd == std::string_view::npos) tokEnd = rest.size();
            const std::string_view tok = rest.substr(a, tokEnd - a);

            const std::size_t eq = rest.find('=', a);
            if (eq == std::string_view::npos || eq >= tokEnd || eq == a)
                return fail("expected key=value, got '" + std::string(tok) + "'");
            const std::string_view key = rest.substr(a, eq - a);

            std::string_view value;
            if (eq + 1 < rest.size() && rest[eq + 1] == '"') {
                const std::size_t close = rest.find('"', eq + 2);
                if (close == std::string_view::npos)
                    return fail("unterminated quote in '" + std::string(key) + "'");
                value = rest.substr(eq + 2, close - eq - 2);
                i = close + 1;
                if (i < rest.size() && ws.find(rest[i]) == std::string_view::npos)
                    return fail("unexpected text after the closing quote of '" + std::string(key) + "'");
            } else {
                value = rest.substr(eq + 1, tokEnd - eq - 1);
                i = tokEnd;
                if (value.empty()) return fail("expected key=value, got '" + std::string(tok) + "'");
            }
            if (std::any_of(kv_.begin(), kv_.end(), [&](const Kv& e) { return e.key == key; }))
                return fail("duplicate key '" + std::string(key) + "'");
            kv_.push_back({key, value, false});
        }
        return true;
    }

    bool has(std::string_view key) const {
        return std::any_of(kv_.begin(), kv_.end(), [&](const Kv& e) { return e.key == key; });
    }

    std::optional<std::string_view> raw(std::string_view key) {
        for (Kv& e : kv_)
            if (e.key == key) { e.used = true; return e.value; }
        return std::nullopt;
    }

    bool fail(std::string msg) {
        if (err_.empty()) err_ = std::move(msg);
        return false;
    }
    bool failed() const { return !err_.empty(); }
    const std::string& error() const { return err_; }

    float number(std::string_view key, float def) {
        const auto v = raw(key);
        if (!v) return def;
        float out = def;
        if (!parseFloat(*v, out)) fail("'" + std::string(key) + "' is not a number: '" + std::string(*v) + "'");
        return out;
    }
    float requiredNumber(std::string_view key) {
        if (!has(key)) { fail("missing '" + std::string(key) + "'"); return 0.0f; }
        return number(key, 0.0f);
    }
    int integer(std::string_view key, int def) {
        const auto v = raw(key);
        if (!v) return def;
        int out = def;
        if (!parseInt(*v, out)) fail("'" + std::string(key) + "' is not an integer: '" + std::string(*v) + "'");
        return out;
    }
    int requiredInteger(std::string_view key) {
        if (!has(key)) { fail("missing '" + std::string(key) + "'"); return 0; }
        return integer(key, 0);
    }
    bool flag(std::string_view key, bool def) {
        const auto v = raw(key);
        if (!v) return def;
        if (*v == "1" || *v == "true")  return true;
        if (*v == "0" || *v == "false") return false;
        fail("'" + std::string(key) + "' must be 0 or 1: '" + std::string(*v) + "'");
        return def;
    }

    // Reads `n` comma-separated floats into `out`. Returns false when absent
    // (an error if `required`) or malformed.
    bool vec(std::string_view key, float* out, int n, bool required) {
        const auto v = raw(key);
        if (!v) {
            if (required) fail("missing '" + std::string(key) + "'");
            return false;
        }
        const auto parts = split(*v, ',');
        if (static_cast<int>(parts.size()) != n) {
            fail("'" + std::string(key) + "' needs " + std::to_string(n) + " comma-separated numbers");
            return false;
        }
        for (int i = 0; i < n; ++i) {
            if (!parseFloat(parts[static_cast<std::size_t>(i)], out[i])) {
                fail("'" + std::string(key) + "' has a bad number: '" +
                     std::string(parts[static_cast<std::size_t>(i)]) + "'");
                return false;
            }
        }
        return true;
    }
    glm::vec3 vec3(std::string_view key, glm::vec3 def, bool required) {
        float v[3] = {def.x, def.y, def.z};
        return vec(key, v, 3, required) ? glm::vec3(v[0], v[1], v[2]) : def;
    }

    // Call once all expected keys were read.
    void finish() {
        for (const Kv& e : kv_)
            if (!e.used) { fail("unknown key '" + std::string(e.key) + "'"); return; }
    }

private:
    struct Kv {
        std::string_view key, value;
        bool             used;
    };
    std::vector<Kv> kv_;
    std::string     err_;
};

// Calls fn(lineNo, kind, rest) for every non-blank, non-comment line of `text`
// (1-based line numbers; "\n" and "\r\n" both work; a '#' inside a quoted value is
// text, not a comment). `kind` is the first word, `rest` the key=value tail.
template <class Fn>
void forEachRecordLine(std::string_view text, Fn&& fn) {
    int         lineNo = 0;
    std::size_t pos    = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;

        bool inQuote = false;
        for (std::size_t c = 0; c < line.size(); ++c) {
            if (line[c] == '"') inQuote = !inQuote;
            else if (line[c] == '#' && !inQuote) { line = line.substr(0, c); break; }
        }
        line = trim(line);
        if (line.empty()) continue;

        const std::size_t sp = line.find_first_of(" \t");
        fn(lineNo, line.substr(0, sp), sp == std::string_view::npos ? std::string_view{} : line.substr(sp));
    }
}

} // namespace carnage::rec
