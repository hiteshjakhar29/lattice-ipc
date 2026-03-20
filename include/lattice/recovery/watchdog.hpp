#pragma once
// Header-only deadline watchdog for stall detection.
//
// The monitored thread calls kick() periodically; the supervisor calls
// is_alive() to check whether kick() was called within the timeout window.
//
// Thread-safe: kick() and is_alive() may be called from different threads.

#include <atomic>
#include <chrono>
#include <cstdint>

namespace lattice::recovery {

class Watchdog {
public:
    explicit Watchdog(uint32_t timeout_ms) noexcept
        : timeout_ns_(static_cast<uint64_t>(timeout_ms) * 1'000'000ULL)
        , last_kick_ns_(now_ns())
    {}

    /// Reset the watchdog deadline.  Called by the monitored thread.
    void kick() noexcept {
        last_kick_ns_.store(now_ns(), std::memory_order_release);
    }

    /// Returns true if kick() was called within the configured timeout window.
    [[nodiscard]] bool is_alive() const noexcept {
        return elapsed_ns() < timeout_ns_;
    }

    /// Nanoseconds elapsed since the last kick (or construction).
    [[nodiscard]] uint64_t elapsed_ns() const noexcept {
        return now_ns() - last_kick_ns_.load(std::memory_order_acquire);
    }

    [[nodiscard]] uint64_t timeout_ns() const noexcept { return timeout_ns_; }
    [[nodiscard]] uint32_t timeout_ms() const noexcept {
        return static_cast<uint32_t>(timeout_ns_ / 1'000'000ULL);
    }

private:
    static uint64_t now_ns() noexcept {
        using namespace std::chrono;
        return static_cast<uint64_t>(
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }

    uint64_t              timeout_ns_;
    std::atomic<uint64_t> last_kick_ns_;
};

} // namespace lattice::recovery
