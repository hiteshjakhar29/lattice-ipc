#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/anomaly/burst_alert.hpp"
#include "lattice/anomaly/welford_stats.hpp"

#include <cstdint>
#include <vector>

namespace lattice::anomaly {

struct BurstConfig {
    double   z_threshold = 4.0;   ///< Alert if order_qty z-score exceeds this value
    uint32_t max_symbols = 64;    ///< Symbol table capacity; must be power of two
};

/// Per-symbol order-size tracking entry.
struct BurstEntry {
    uint16_t     symbol_id = 0;
    bool         valid     = false;
    uint8_t      _pad[5]   = {};
    WelfordStats qty_stats;   ///< Online mean/variance of Add order sizes

    [[nodiscard]] bool is_valid()  const noexcept { return valid; }
    void               invalidate() noexcept { valid = false; symbol_id = 0; }
};

/// Detects unusually large Add orders relative to each symbol's rolling order-size
/// distribution. Fires BurstAlert when order_qty > mean + z_threshold * stddev.
///
/// The Z-score is computed against the historical distribution (stats updated
/// AFTER the check), so each new order is evaluated against prior observations.
///
/// Symbol is identified by FeedEvent::src_port.
/// Zero heap allocation after construction.
class BurstDetector {
public:
    explicit BurstDetector(const BurstConfig& cfg = {});

    [[nodiscard]] bool process(const FeedEvent& ev, BurstAlert& alert_out) noexcept;
    [[nodiscard]] bool process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                          BurstAlert& alert_out) noexcept;

    [[nodiscard]] uint64_t alerts_fired() const noexcept { return alerts_fired_; }
    void reset() noexcept;

private:
    BurstConfig             cfg_;
    uint32_t                mask_;
    std::vector<BurstEntry> table_;
    uint64_t                alerts_fired_ = 0;

    BurstEntry*    find_or_insert(uint16_t symbol_id) noexcept;
    static uint64_t clock_ns() noexcept;
};

} // namespace lattice::anomaly
