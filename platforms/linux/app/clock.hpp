#pragma once

// Single process-wide monotonic millisecond clock, shared by the backend (which stamps
// captured frames) and the controller (which schedules scans/ticks), so every timestamp
// fed into the pure engines is on one consistent timeline.

#include <chrono>
#include <cstdint>

namespace zd {
namespace linux_app {

inline uint32_t nowMs() {
    static const auto t0 = std::chrono::steady_clock::now();
    const auto dt = std::chrono::steady_clock::now() - t0;
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(dt).count());
}

}  // namespace linux_app
}  // namespace zd
