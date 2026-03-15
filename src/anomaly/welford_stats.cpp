#include "lattice/anomaly/welford_stats.hpp"

namespace lattice::anomaly {

void WelfordStats::update(double value) noexcept {
    ++count_;
    const double delta  = value - mean_;
    mean_              += delta / static_cast<double>(count_);
    const double delta2 = value - mean_;
    m2_                += delta * delta2;
}

void WelfordStats::reset() noexcept {
    count_ = 0;
    mean_  = 0.0;
    m2_    = 0.0;
}

} // namespace lattice::anomaly
