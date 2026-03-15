#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/anomaly/pending_order.hpp"
#include "lattice/anomaly/spoof_alert.hpp"
#include "lattice/anomaly/welford_stats.hpp"

#include <cstdint>
#include <vector>

namespace lattice::anomaly {

/// Tunable parameters for AnomalyDetector.
struct AnomalyConfig {
    uint32_t qty_threshold       = 1000;          ///< Min qty to track as a "large order"
    uint64_t time_window_ns      = 500'000'000ULL;///< Evict orders older than this (500 ms)
    double   z_score_threshold   = -2.0;          ///< Alert if z_score < threshold (fast cancel)
    uint32_t max_tracked_orders  = 4096;          ///< Must be a power of two
    uint32_t rolling_window_sz   = 256;           ///< Circular buffer size for cancel latencies
};

/// Detects spoofing: large orders placed near the BBO and quickly cancelled.
///
/// Algorithm:
///   1. On ADD with qty >= qty_threshold: record PendingOrder in a fixed-size
///      open-addressing hash table (no heap in hot path after construction).
///   2. On CANCEL of a tracked order: compute elapsed_ns, update WelfordStats,
///      compute Z-score. If z_score < z_score_threshold, emit SpoofAlert.
///   3. Periodically evict orders older than time_window_ns.
///
/// All heap allocation occurs in the constructor (fixed-size vectors).
/// process() is zero-allocation.
class AnomalyDetector {
public:
    explicit AnomalyDetector(const AnomalyConfig& cfg = {});

    /// Feed one FeedEvent. If a SpoofAlert fires, fills alert_out and returns true.
    /// Otherwise returns false. Zero heap allocation.
    [[nodiscard]] bool process(const FeedEvent& ev, SpoofAlert& alert_out) noexcept;

    /// Feed one FeedEvent with a caller-supplied timestamp (useful for testing).
    [[nodiscard]] bool process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                         SpoofAlert& alert_out) noexcept;

    [[nodiscard]] const WelfordStats& stats()          const noexcept { return stats_; }
    [[nodiscard]] uint64_t            alerts_fired()   const noexcept { return alerts_fired_; }
    [[nodiscard]] uint64_t            orders_tracked() const noexcept { return orders_tracked_; }

    void reset() noexcept;

private:
    AnomalyConfig cfg_;
    uint32_t      mask_;  // cfg_.max_tracked_orders - 1; power-of-two mask

    // Fixed-capacity open-addressing hash table (no rehashing, no heap in hot path)
    std::vector<PendingOrder> order_table_;

    // Circular buffer of recent cancel latencies (ns) — diagnostics
    std::vector<double> latency_window_;
    uint32_t            window_head_ = 0;

    WelfordStats stats_;
    uint64_t     alerts_fired_   = 0;
    uint64_t     orders_tracked_ = 0;

    void         on_add_event   (const lattice::signals::DecodedEvent& d,
                                  uint64_t now_ns) noexcept;
    bool         on_cancel_event(const lattice::signals::DecodedEvent& d,
                                  uint64_t now_ns, SpoofAlert& out) noexcept;
    void         evict_stale    (uint64_t now_ns) noexcept;

    PendingOrder* find_order  (uint64_t order_id) noexcept;
    void          insert_order(const PendingOrder& o) noexcept;
    void          remove_order(uint64_t order_id) noexcept;

    static uint64_t clock_ns() noexcept;
};

} // namespace lattice::anomaly
