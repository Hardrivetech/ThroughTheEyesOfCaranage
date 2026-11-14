#include "core/Log.hpp"

#include <chrono>
#include <cstdio>

namespace carnage::log {

namespace {
using Clock = std::chrono::steady_clock;
Clock::time_point g_start = Clock::now();

constexpr const char* label(Level l) {
    switch (l) {
        case Level::Debug: return "DBG";
        case Level::Info:  return "INF";
        case Level::Warn:  return "WRN";
        case Level::Error: return "ERR";
    }
    return "???";
}
} // namespace

bool init() {
    g_start = Clock::now();
    return true;
}

void shutdown() { std::fflush(nullptr); }

void write(Level level, std::string_view message) {
    const double t = std::chrono::duration<double>(Clock::now() - g_start).count();
    std::FILE* out = level >= Level::Warn ? stderr : stdout;
    std::fprintf(out, "[%9.3f] [%s] %.*s\n", t, label(level),
                 static_cast<int>(message.size()), message.data());
}

} // namespace carnage::log
