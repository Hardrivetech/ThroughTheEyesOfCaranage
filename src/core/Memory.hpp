#pragma once
#include <cstddef>

namespace carnage::mem {

struct Stats {
    std::size_t liveBytes;
    std::size_t peakBytes;
    std::size_t allocCount;
    std::size_t freeCount;
};

// Counters are maintained by the global operator new/delete replacements
// (Memory.cpp) from process start; init/shutdown only bracket the report.
bool  init();
void  shutdown();
Stats stats();

} // namespace carnage::mem
