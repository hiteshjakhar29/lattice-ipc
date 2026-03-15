#pragma once

#include <cstdint>
#include <cmath>

namespace lattice::anomaly {

/// Online mean and variance tracker using Welford's algorithm.
///
/// All state fits in 24 bytes. No heap allocation. O(1) per observation.
/// is_stable() returns true once count >= 2 (stddev is undefined with < 2 samples).
class WelfordStats {
public:
    WelfordStats() noexcept = default;

    /// Feed one new observation (e.g., cancel latency in nanoseconds).
    void update(double value) noexcept;

    [[nodiscard]] double   mean()     const noexcept { return mean_; }
    [[nodiscard]] double   variance() const noexcept {
        return (count_ > 1) ? (m2_ / static_cast<double>(count_)) : 0.0;
    }
    [[nodiscard]] double   stddev()   const noexcept { return std::sqrt(variance()); }
    [[nodiscard]] uint64_t count()    const noexcept { return count_; }

    /// True once we have at least 2 samples (stddev is meaningful).
    [[nodiscard]] bool is_stable() const noexcept { return count_ >= 2; }

    void reset() noexcept;

private:
    uint64_t count_ = 0;
    double   mean_  = 0.0;
    double   m2_    = 0.0; ///< Sum of squared deviations from the running mean (Welford M2)
};

} // namespace lattice::anomaly
