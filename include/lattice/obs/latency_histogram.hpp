#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace lattice::obs {

/// Fixed-bucket latency histogram with atomic counters.
///
/// Eight buckets (upper-bound in nanoseconds):
///   [0] <1 µs    [1] 1-5 µs   [2] 5-10 µs  [3] 10-50 µs
///   [4] 50-100 µs [5] 100-500 µs [6] 500 µs-1 ms [7] >1 ms
///
/// Thread-safe: record() uses relaxed atomics (no ordering needed for
/// independent histogram buckets). Percentile reads snapshot all buckets.
/// Zero heap allocation. Trivially movable.
struct LatencyHistogram {
    static constexpr int kBuckets = 8;

    /// Upper bound of each bucket in nanoseconds (last bucket = UINT64_MAX = >1 ms).
    static constexpr std::array<uint64_t, kBuckets> kBounds = {
        1'000ULL,       //  <1 µs
        5'000ULL,       //  1–5 µs
        10'000ULL,      //  5–10 µs
        50'000ULL,      //  10–50 µs
        100'000ULL,     //  50–100 µs
        500'000ULL,     //  100–500 µs
        1'000'000ULL,   //  500 µs–1 ms
        UINT64_MAX,     //  >1 ms
    };

    std::atomic<uint64_t> buckets[kBuckets] = {};

    void record(uint64_t ns) noexcept {
        for (int i = 0; i < kBuckets; ++i) {
            if (ns < kBounds[i]) {
                buckets[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        buckets[kBuckets - 1].fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t total() const noexcept {
        uint64_t n = 0;
        for (int i = 0; i < kBuckets; ++i)
            n += buckets[i].load(std::memory_order_relaxed);
        return n;
    }

    /// Returns the upper bound of the bucket containing the p-th percentile.
    /// Returns 0 if no samples recorded.
    [[nodiscard]] uint64_t percentile(double p) const noexcept {
        uint64_t counts[kBuckets];
        uint64_t n = 0;
        for (int i = 0; i < kBuckets; ++i) {
            counts[i] = buckets[i].load(std::memory_order_relaxed);
            n += counts[i];
        }
        if (n == 0) return 0;

        const uint64_t target = static_cast<uint64_t>(p * static_cast<double>(n));
        uint64_t cumulative = 0;
        for (int i = 0; i < kBuckets; ++i) {
            cumulative += counts[i];
            if (cumulative > target) return kBounds[i];
        }
        return kBounds[kBuckets - 1];
    }

    [[nodiscard]] uint64_t p50()  const noexcept { return percentile(0.50);  }
    [[nodiscard]] uint64_t p99()  const noexcept { return percentile(0.99);  }
    [[nodiscard]] uint64_t p999() const noexcept { return percentile(0.999); }

    void reset() noexcept {
        for (int i = 0; i < kBuckets; ++i)
            buckets[i].store(0, std::memory_order_relaxed);
    }
};

} // namespace lattice::obs
