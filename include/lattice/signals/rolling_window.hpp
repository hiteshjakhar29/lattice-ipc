#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace lattice::signals {

/// Fixed-size circular buffer with no heap allocation.
/// Index 0 is the oldest element, index count()-1 is the newest.
template<typename T, std::size_t N>
struct RollingWindow {
    static_assert(N > 0, "RollingWindow size must be positive");

    void push(T val) noexcept {
        buf_[head_] = val;
        head_ = (head_ + 1) % N;
        if (count_ < N) ++count_;
    }

    std::size_t count() const noexcept { return count_; }
    bool        full()  const noexcept { return count_ == N; }

    /// Access by logical index: 0 = oldest, count()-1 = newest.
    T operator[](std::size_t i) const noexcept {
        return buf_[(head_ + N - count_ + i) % N];
    }

    /// Arithmetic mean over all stored values. Requires T convertible to double.
    double mean() const noexcept {
        if (count_ == 0) return 0.0;
        double sum = 0.0;
        for (std::size_t i = 0; i < count_; ++i)
            sum += static_cast<double>(buf_[(head_ + N - count_ + i) % N]);
        return sum / static_cast<double>(count_);
    }

    /// Population standard deviation. Returns 0 if fewer than 2 elements.
    double stddev() const noexcept {
        if (count_ < 2) return 0.0;
        const double m = mean();
        double var = 0.0;
        for (std::size_t i = 0; i < count_; ++i) {
            const double d = static_cast<double>(buf_[(head_ + N - count_ + i) % N]) - m;
            var += d * d;
        }
        return std::sqrt(var / static_cast<double>(count_));
    }

    void clear() noexcept {
        buf_   = {};
        head_  = 0;
        count_ = 0;
    }

private:
    std::array<T, N> buf_{};
    std::size_t      head_  = 0;
    std::size_t      count_ = 0;
};

} // namespace lattice::signals
