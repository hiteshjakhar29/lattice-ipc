#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/anomaly/cancel_spike_alert.hpp"
#include "lattice/anomaly/welford_stats.hpp"

#include <cstdint>
#include <vector>

namespace lattice::anomaly {

struct CancelSpikeConfig {
    uint64_t rate_window_ns = 1'000'000'000ULL; ///< Bucket width for rate measurement (1 s)
    double   z_threshold    = 3.0;              ///< Alert if z > this value
    uint32_t max_symbols    = 64;               ///< Symbol table capacity; must be power of two
};

/// Per-symbol cancel-rate tracking entry.
struct CancelRateEntry {
    uint16_t     symbol_id       = 0;
    bool         valid           = false;
    uint8_t      _pad[5]         = {};
    uint32_t     cancel_count    = 0;
    uint64_t     window_start_ns = 0;
    WelfordStats rate_stats;             ///< Tracks historical cancel rates (per second)

    [[nodiscard]] bool is_valid()  const noexcept { return valid; }
    void               invalidate() noexcept { valid = false; symbol_id = 0; }
};

/// Detects abnormal cancellation rate spikes using Welford Z-score per symbol.
///
/// Divides time into rate_window_ns buckets. At bucket boundary, computes
/// cancels/second for the completed bucket, updates WelfordStats, and fires
/// CancelSpikeAlert if z > z_threshold (checked against historical distribution).
///
/// Symbol is identified by FeedEvent::src_port.
/// Zero heap allocation after construction.
class CancelSpikeDetector {
public:
    explicit CancelSpikeDetector(const CancelSpikeConfig& cfg = {});

    [[nodiscard]] bool process(const FeedEvent& ev, CancelSpikeAlert& alert_out) noexcept;
    [[nodiscard]] bool process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                          CancelSpikeAlert& alert_out) noexcept;

    [[nodiscard]] uint64_t alerts_fired() const noexcept { return alerts_fired_; }
    void reset() noexcept;

private:
    CancelSpikeConfig             cfg_;
    uint32_t                      mask_;
    std::vector<CancelRateEntry>  table_;
    uint64_t                      alerts_fired_ = 0;

    CancelRateEntry* find_or_insert(uint16_t symbol_id) noexcept;
    static uint64_t  clock_ns() noexcept;
};

} // namespace lattice::anomaly
