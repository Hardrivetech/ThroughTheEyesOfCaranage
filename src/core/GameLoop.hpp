#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace carnage {

struct LoopConfig {
    std::chrono::nanoseconds tick{1'000'000'000 / 60}; // 60 Hz simulation
    std::chrono::milliseconds maxFrameTime{250};       // clamp after stalls/breakpoints
    int maxStepsPerFrame = 8;                          // spiral-of-death guard
};

struct LoopStats {
    std::uint64_t ticks  = 0;
    std::uint64_t frames = 0;
};

// Fixed-timestep simulation decoupled from an uncapped render loop.
//   poll()          – pump OS events, once per frame
//   update(dt)      – deterministic tick, dt in seconds (constant)
//   render(alpha)   – alpha in [0,1) for state interpolation
//   quit()          – true to leave the loop
template <class Poll, class Update, class Render, class Quit>
LoopStats runLoop(const LoopConfig& cfg, Poll&& poll, Update&& update,
                  Render&& render, Quit&& quit) {
    using Clock = std::chrono::steady_clock;

    const double dt = std::chrono::duration<double>(cfg.tick).count();
    LoopStats stats;
    std::chrono::nanoseconds accumulator{0};
    Clock::time_point previous = Clock::now();

    while (!quit()) {
        const Clock::time_point now = Clock::now();
        const auto frameTime = std::min<std::chrono::nanoseconds>(now - previous, cfg.maxFrameTime);
        previous = now;
        accumulator += frameTime;

        poll();

        int steps = 0;
        while (accumulator >= cfg.tick && steps < cfg.maxStepsPerFrame) {
            update(dt);
            accumulator -= cfg.tick;
            ++stats.ticks;
            ++steps;
        }
        if (accumulator >= cfg.tick) accumulator = std::chrono::nanoseconds{0}; // drop backlog

        const double alpha = static_cast<double>(accumulator.count()) /
                             static_cast<double>(cfg.tick.count());
        render(alpha);
        ++stats.frames;
    }
    return stats;
}

} // namespace carnage
