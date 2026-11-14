#pragma once
#include "core/Log.hpp"

#include <cstddef>
#include <span>

namespace carnage {

struct Subsystem {
    const char* name;
    bool (*init)();
    void (*shutdown)();
};

// Initialises in table order; shuts down in exact reverse order, and only
// the subsystems that actually came up. Shutdown also runs from the destructor.
class SubsystemManager {
public:
    explicit SubsystemManager(std::span<const Subsystem> table) : table_(table) {}
    ~SubsystemManager() { shutdownAll(); }
    SubsystemManager(const SubsystemManager&)            = delete;
    SubsystemManager& operator=(const SubsystemManager&) = delete;

    bool initAll() {
        for (const Subsystem& s : table_) {
            if (!s.init()) {
                log::error("Subsystem '{}' failed to initialise", s.name);
                shutdownAll();
                return false;
            }
            ++initialised_;
            log::debug("Subsystem '{}' up", s.name);
        }
        return true;
    }

    void shutdownAll() {
        while (initialised_ > 0) {
            const Subsystem& s = table_[--initialised_];
            log::debug("Subsystem '{}' down", s.name);
            s.shutdown();
        }
    }

private:
    std::span<const Subsystem> table_;
    std::size_t                initialised_ = 0;
};

} // namespace carnage
