#include "lattice/anomaly/burst_detector.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <cassert>
#include <ctime>

namespace lattice::anomaly {

using lattice::signals::EventType;
using lattice::signals::decode;

BurstDetector::BurstDetector(const BurstConfig& cfg)
    : cfg_(cfg)
    , mask_(cfg.max_symbols - 1)
    , table_(cfg.max_symbols)
{
    assert((cfg.max_symbols & mask_) == 0 &&
           "BurstConfig::max_symbols must be a power of two");
}

uint64_t BurstDetector::clock_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

BurstEntry* BurstDetector::find_or_insert(uint16_t symbol_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(symbol_id) & mask_;
    for (uint32_t i = 0; i < static_cast<uint32_t>(table_.size()); ++i) {
        BurstEntry& slot = table_[idx];
        if (!slot.is_valid()) {
            slot.symbol_id = symbol_id;
            slot.valid     = true;
            return &slot;
        }
        if (slot.symbol_id == symbol_id) return &slot;
        idx = (idx + 1) & mask_;
    }
    BurstEntry& home = table_[static_cast<uint32_t>(symbol_id) & mask_];
    home.symbol_id = symbol_id;
    home.valid     = true;
    return &home;
}

bool BurstDetector::process(const FeedEvent& ev, BurstAlert& alert_out) noexcept {
    return process_with_time(ev, clock_ns(), alert_out);
}

bool BurstDetector::process_with_time(const FeedEvent& ev, uint64_t /*now_ns*/,
                                       BurstAlert& alert_out) noexcept {
    const auto d = decode(ev);
    if (d.type != EventType::Add) return false;

    const uint16_t sym   = ev.src_port;
    BurstEntry*    entry = find_or_insert(sym);
    if (!entry) return false;

    const double qty_d = static_cast<double>(d.qty);

    // Check against historical distribution BEFORE updating stats.
    bool fired = false;
    if (entry->qty_stats.is_stable() && entry->qty_stats.stddev() > 0.0) {
        const double z = (qty_d - entry->qty_stats.mean()) / entry->qty_stats.stddev();
        if (z > cfg_.z_threshold) {
            alert_out.symbol_id  = sym;
            alert_out.qty        = d.qty;
            alert_out.order_id   = d.order_id;
            alert_out.mean_qty   = entry->qty_stats.mean();
            alert_out.stddev_qty = entry->qty_stats.stddev();
            alert_out.z_score    = z;
            ++alerts_fired_;
            fired = true;
        }
    }

    entry->qty_stats.update(qty_d);
    return fired;
}

void BurstDetector::reset() noexcept {
    for (auto& slot : table_) {
        slot.invalidate();
        slot.qty_stats.reset();
    }
    alerts_fired_ = 0;
}

} // namespace lattice::anomaly
