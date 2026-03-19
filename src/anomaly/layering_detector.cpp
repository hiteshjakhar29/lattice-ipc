#include "lattice/anomaly/layering_detector.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <cassert>
#include <ctime>

namespace lattice::anomaly {

using lattice::signals::EventType;
using lattice::signals::decode;

LayeringDetector::LayeringDetector(const LayeringConfig& cfg)
    : cfg_(cfg)
    , mask_(cfg.max_symbols - 1)
    , table_(cfg.max_symbols)
{
    assert((cfg.max_symbols & mask_) == 0 &&
           "LayeringConfig::max_symbols must be a power of two");
}

uint64_t LayeringDetector::clock_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

LayeringEntry* LayeringDetector::find_or_insert(uint16_t symbol_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(symbol_id) & mask_;
    for (uint32_t i = 0; i < static_cast<uint32_t>(table_.size()); ++i) {
        LayeringEntry& slot = table_[idx];
        if (!slot.is_valid()) {
            slot.invalidate(); // zero counts
            slot.symbol_id = symbol_id;
            slot.valid     = true;
            return &slot;
        }
        if (slot.symbol_id == symbol_id) return &slot;
        idx = (idx + 1) & mask_;
    }
    // Table full — overwrite home slot
    LayeringEntry& home = table_[static_cast<uint32_t>(symbol_id) & mask_];
    home.invalidate();
    home.symbol_id = symbol_id;
    home.valid     = true;
    return &home;
}

bool LayeringDetector::process(const FeedEvent& ev, LayeringAlert& alert_out) noexcept {
    return process_with_time(ev, clock_ns(), alert_out);
}

bool LayeringDetector::process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                          LayeringAlert& alert_out) noexcept {
    const auto d = decode(ev);
    if (d.type != EventType::Add)     return false;
    if (d.qty  <  cfg_.qty_threshold) return false;

    const uint16_t  sym   = ev.src_port;
    LayeringEntry*  entry = find_or_insert(sym);
    if (!entry) return false;

    uint64_t* times = d.is_bid ? entry->bid_times : entry->ask_times;
    uint8_t&  cnt   = d.is_bid ? entry->bid_count : entry->ask_count;

    // Compact: drop entries that have fallen outside the window.
    uint8_t w = 0;
    for (uint8_t r = 0; r < cnt; ++r) {
        if (now_ns - times[r] <= cfg_.window_ns) {
            times[w++] = times[r];
        }
    }
    cnt = w;

    // Add current timestamp (if capacity allows; otherwise overwrite oldest).
    if (cnt < LayeringEntry::kMaxPerSide) {
        times[cnt++] = now_ns;
    } else {
        // Shift left (drop oldest) and append.
        for (uint8_t i = 0; i < LayeringEntry::kMaxPerSide - 1; ++i) {
            times[i] = times[i + 1];
        }
        times[LayeringEntry::kMaxPerSide - 1] = now_ns;
    }

    if (static_cast<uint32_t>(cnt) <= cfg_.count_threshold) return false;

    // Fire alert and reset this side's window to avoid repeated alerts.
    alert_out.symbol_id       = sym;
    alert_out.is_bid          = d.is_bid ? 1u : 0u;
    alert_out.order_count     = cnt;
    alert_out.window_start_ns = times[0];
    alert_out.triggered_ns    = now_ns;
    cnt = 0; // reset so we need a fresh cluster to re-fire
    ++alerts_fired_;
    return true;
}

void LayeringDetector::reset() noexcept {
    for (auto& slot : table_) slot.invalidate();
    alerts_fired_ = 0;
}

} // namespace lattice::anomaly
