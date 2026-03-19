#pragma once

#include <atomic>
#include <climits>
#include <cstddef>
#include <cstdint>

namespace lattice::obs {

/// Tracks min/max/mean queue depth and overflow events.
///
/// record(depth) is called each time a sample is taken.  min/max/mean are
/// maintained with atomic CAS updates so the tracker is safe to call from a
/// single producer while a separate reporter thread reads the stats.
///
/// "Overflow" is counted when depth >= capacity (capacity == 0 disables it).
/// Zero heap allocation. Trivially movable.
struct QueueDepthTracker {
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> min_{UINT64_MAX};
    std::atomic<uint64_t> max_{0};
    std::atomic<uint64_t> sum_{0};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> overflows_{0};
    uint64_t              capacity_{0};  ///< 0 = overflow tracking disabled

    void set_capacity(uint64_t cap) noexcept { capacity_ = cap; }

    void record(std::size_t depth) noexcept {
        const uint64_t d = static_cast<uint64_t>(depth);

        current_.store(d, std::memory_order_relaxed);

        // CAS-update min
        uint64_t old_min = min_.load(std::memory_order_relaxed);
        while (d < old_min &&
               !min_.compare_exchange_weak(old_min, d, std::memory_order_relaxed)) {}

        // CAS-update max
        uint64_t old_max = max_.load(std::memory_order_relaxed);
        while (d > old_max &&
               !max_.compare_exchange_weak(old_max, d, std::memory_order_relaxed)) {}

        sum_.fetch_add(d, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);

        if (capacity_ > 0 && d >= capacity_)
            overflows_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t current()   const noexcept {
        return current_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] uint64_t min_depth() const noexcept {
        const uint64_t v = min_.load(std::memory_order_relaxed);
        return (v == UINT64_MAX) ? 0 : v;   // 0 if no samples yet
    }
    [[nodiscard]] uint64_t max_depth() const noexcept {
        return max_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] double mean_depth() const noexcept {
        const uint64_t c = count_.load(std::memory_order_relaxed);
        if (c == 0) return 0.0;
        return static_cast<double>(sum_.load(std::memory_order_relaxed)) /
               static_cast<double>(c);
    }
    [[nodiscard]] uint64_t overflows() const noexcept {
        return overflows_.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        current_.store(0, std::memory_order_relaxed);
        min_.store(UINT64_MAX, std::memory_order_relaxed);
        max_.store(0, std::memory_order_relaxed);
        sum_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_relaxed);
        overflows_.store(0, std::memory_order_relaxed);
    }
};

} // namespace lattice::obs
