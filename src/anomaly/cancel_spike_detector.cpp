#include "lattice/anomaly/cancel_spike_detector.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <cassert>
#include <ctime>

namespace lattice::anomaly {

using lattice::signals::EventType;
using lattice::signals::decode;

CancelSpikeDetector::CancelSpikeDetector(const CancelSpikeConfig& cfg)
    : cfg_(cfg)
    , mask_(cfg.max_symbols - 1)
    , table_(cfg.max_symbols)
{
    assert((cfg.max_symbols & mask_) == 0 &&
           "CancelSpikeConfig::max_symbols must be a power of two");
}

uint64_t CancelSpikeDetector::clock_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

CancelRateEntry* CancelSpikeDetector::find_or_insert(uint16_t symbol_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(symbol_id) & mask_;
    for (uint32_t i = 0; i < static_cast<uint32_t>(table_.size()); ++i) {
        CancelRateEntry& slot = table_[idx];
        if (!slot.is_valid()) {
            slot.symbol_id       = symbol_id;
            slot.valid           = true;
            slot.cancel_count    = 0;
            slot.window_start_ns = 0;
            return &slot;
        }
        if (slot.symbol_id == symbol_id) return &slot;
        idx = (idx + 1) & mask_;
    }
    CancelRateEntry& home = table_[static_cast<uint32_t>(symbol_id) & mask_];
    home.symbol_id       = symbol_id;
    home.valid           = true;
    home.cancel_count    = 0;
    home.window_start_ns = 0;
    return &home;
}

bool CancelSpikeDetector::process(const FeedEvent& ev, CancelSpikeAlert& alert_out) noexcept {
    return process_with_time(ev, clock_ns(), alert_out);
}

bool CancelSpikeDetector::process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                              CancelSpikeAlert& alert_out) noexcept {
    const auto d = decode(ev);
    if (d.type != EventType::Cancel) return false;

    const uint16_t    sym   = ev.src_port;
    CancelRateEntry*  entry = find_or_insert(sym);
    if (!entry) return false;

    // First event for this symbol — start the first window.
    if (entry->window_start_ns == 0) {
        entry->window_start_ns = now_ns;
        entry->cancel_count    = 1;
        return false;
    }

    const uint64_t elapsed = now_ns - entry->window_start_ns;

    if (elapsed < cfg_.rate_window_ns) {
        // Still within the current bucket.
        ++entry->cancel_count;
        return false;
    }

    // Bucket has closed: compute rate for the completed window.
    const double elapsed_s = static_cast<double>(elapsed) / 1e9;
    const double rate      = static_cast<double>(entry->cancel_count) / elapsed_s;

    // Check against historical distribution BEFORE updating (so we compare
    // against prior data, not the spike itself).
    bool fired = false;
    if (entry->rate_stats.is_stable() && entry->rate_stats.stddev() > 0.0) {
        const double z = (rate - entry->rate_stats.mean()) / entry->rate_stats.stddev();
        if (z > cfg_.z_threshold) {
            alert_out.symbol_id       = sym;
            alert_out.cancels_per_sec = rate;
            alert_out.mean_rate       = entry->rate_stats.mean();
            alert_out.stddev_rate     = entry->rate_stats.stddev();
            alert_out.z_score         = z;
            ++alerts_fired_;
            fired = true;
        }
    }

    entry->rate_stats.update(rate);
    entry->window_start_ns = now_ns;
    entry->cancel_count    = 1;
    return fired;
}

void CancelSpikeDetector::reset() noexcept {
    for (auto& slot : table_) {
        slot.invalidate();
        slot.cancel_count    = 0;
        slot.window_start_ns = 0;
        slot.rate_stats.reset();
    }
    alerts_fired_ = 0;
}

} // namespace lattice::anomaly
