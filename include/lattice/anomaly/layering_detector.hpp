#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/anomaly/layering_alert.hpp"

#include <cstdint>
#include <vector>

namespace lattice::anomaly {

struct LayeringConfig {
    uint32_t qty_threshold   = 1000;             ///< Min qty for a "large" order
    uint64_t window_ns       = 500'000'000ULL;   ///< Rolling window (default 500 ms)
    uint32_t count_threshold = 3;                ///< Alert when count *exceeds* this value
    uint32_t max_symbols     = 64;               ///< Symbol table capacity; must be power of two
};

/// Per-(symbol, side) state for layering detection.
struct LayeringEntry {
    static constexpr uint8_t kMaxPerSide = 8;

    uint16_t symbol_id = 0;
    bool     valid     = false;
    uint8_t  bid_count = 0;
    uint8_t  ask_count = 0;
    uint8_t  _pad[3]   = {};
    uint64_t bid_times[kMaxPerSide] = {};
    uint64_t ask_times[kMaxPerSide] = {};

    [[nodiscard]] bool is_valid()  const noexcept { return valid; }
    void               invalidate() noexcept {
        valid = false; symbol_id = 0; bid_count = 0; ask_count = 0;
    }
};

/// Detects layering: >count_threshold large orders on the same side of a symbol
/// placed within window_ns nanoseconds.
///
/// Symbol is identified by FeedEvent::src_port.
/// Zero heap allocation after construction.
class LayeringDetector {
public:
    explicit LayeringDetector(const LayeringConfig& cfg = {});

    /// Feed one event. Returns true and fills alert_out if layering is detected.
    [[nodiscard]] bool process(const FeedEvent& ev, LayeringAlert& alert_out) noexcept;
    [[nodiscard]] bool process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                          LayeringAlert& alert_out) noexcept;

    [[nodiscard]] uint64_t alerts_fired() const noexcept { return alerts_fired_; }
    void reset() noexcept;

private:
    LayeringConfig             cfg_;
    uint32_t                   mask_;
    std::vector<LayeringEntry> table_;
    uint64_t                   alerts_fired_ = 0;

    LayeringEntry* find_or_insert(uint16_t symbol_id) noexcept;
    static uint64_t clock_ns() noexcept;
};

} // namespace lattice::anomaly
