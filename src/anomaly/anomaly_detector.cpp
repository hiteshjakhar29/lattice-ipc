#include "lattice/anomaly/anomaly_detector.hpp"

#include <algorithm>
#include <cassert>
#include <ctime>

namespace lattice::anomaly {

using lattice::signals::EventType;
using lattice::signals::decode;

AnomalyDetector::AnomalyDetector(const AnomalyConfig& cfg)
    : cfg_(cfg)
    , mask_(cfg.max_tracked_orders - 1)
    , order_table_(cfg.max_tracked_orders)   // all slots default-init to invalid
    , latency_window_(cfg.rolling_window_sz, 0.0)
{
    assert((cfg.max_tracked_orders & mask_) == 0 &&
           "max_tracked_orders must be a power of two");
}

uint64_t AnomalyDetector::clock_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

bool AnomalyDetector::process(const FeedEvent& ev, SpoofAlert& alert_out) noexcept {
    return process_with_time(ev, clock_ns(), alert_out);
}

bool AnomalyDetector::process_with_time(const FeedEvent& ev, uint64_t now_ns,
                                         SpoofAlert& alert_out) noexcept {
    const auto d = decode(ev);

    switch (d.type) {
        case EventType::Add:
            on_add_event(d, now_ns);
            return false;

        case EventType::Cancel:
            return on_cancel_event(d, now_ns, alert_out);

        default:
            return false;
    }
}

void AnomalyDetector::on_add_event(const lattice::signals::DecodedEvent& d,
                                    uint64_t now_ns) noexcept {
    if (d.qty < cfg_.qty_threshold) return;

    PendingOrder o;
    o.order_id  = d.order_id;
    o.price     = d.price;
    o.qty       = d.qty;
    o.placed_ns = now_ns;
    insert_order(o);
    ++orders_tracked_;
}

bool AnomalyDetector::on_cancel_event(const lattice::signals::DecodedEvent& d,
                                       uint64_t now_ns,
                                       SpoofAlert& out) noexcept {
    evict_stale(now_ns);

    PendingOrder* p = find_order(d.order_id);
    if (!p) return false;

    const uint64_t elapsed_ns = now_ns - p->placed_ns;

    // Record for alert
    const uint64_t placed_ns = p->placed_ns;
    const double   price     = p->price;
    const uint32_t qty       = p->qty;

    // Update rolling window
    latency_window_[window_head_] = static_cast<double>(elapsed_ns);
    window_head_ = (window_head_ + 1) % cfg_.rolling_window_sz;

    // Update Welford stats
    stats_.update(static_cast<double>(elapsed_ns));

    remove_order(d.order_id);

    if (!stats_.is_stable()) return false;

    const double sd = stats_.stddev();
    if (sd <= 0.0) return false;

    const double z = (static_cast<double>(elapsed_ns) - stats_.mean()) / sd;
    if (z >= cfg_.z_score_threshold) return false;

    out.order_id     = d.order_id;
    out.price        = price;
    out.qty          = qty;
    out.placed_ns    = placed_ns;
    out.cancelled_ns = now_ns;
    out.z_score      = z;
    ++alerts_fired_;
    return true;
}

void AnomalyDetector::evict_stale(uint64_t now_ns) noexcept {
    for (auto& slot : order_table_) {
        if (slot.is_valid() && (now_ns - slot.placed_ns) > cfg_.time_window_ns) {
            slot.invalidate();
        }
    }
}

PendingOrder* AnomalyDetector::find_order(uint64_t order_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(order_id) & mask_;
    for (uint32_t i = 0; i < cfg_.max_tracked_orders; ++i) {
        PendingOrder& slot = order_table_[idx];
        if (!slot.is_valid()) return nullptr;          // empty slot → not found
        if (slot.order_id == order_id) return &slot;
        idx = (idx + 1) & mask_;
    }
    return nullptr;
}

void AnomalyDetector::insert_order(const PendingOrder& o) noexcept {
    uint32_t idx = static_cast<uint32_t>(o.order_id) & mask_;
    for (uint32_t i = 0; i < cfg_.max_tracked_orders; ++i) {
        PendingOrder& slot = order_table_[idx];
        if (!slot.is_valid()) {
            slot = o;
            return;
        }
        if (slot.order_id == o.order_id) {
            slot = o; // update existing
            return;
        }
        idx = (idx + 1) & mask_;
    }
    // Table full — overwrite the home slot (rare; better than dropping silently)
    order_table_[static_cast<uint32_t>(o.order_id) & mask_] = o;
}

void AnomalyDetector::remove_order(uint64_t order_id) noexcept {
    uint32_t idx = static_cast<uint32_t>(order_id) & mask_;
    for (uint32_t i = 0; i < cfg_.max_tracked_orders; ++i) {
        PendingOrder& slot = order_table_[idx];
        if (!slot.is_valid()) return;
        if (slot.order_id == order_id) {
            slot.invalidate();
            // Rehash subsequent entries in the same probe chain to avoid gaps
            uint32_t next = (idx + 1) & mask_;
            while (order_table_[next].is_valid()) {
                PendingOrder displaced = order_table_[next];
                order_table_[next].invalidate();
                insert_order(displaced);
                next = (next + 1) & mask_;
            }
            return;
        }
        idx = (idx + 1) & mask_;
    }
}

void AnomalyDetector::reset() noexcept {
    for (auto& slot : order_table_) slot.invalidate();
    std::fill(latency_window_.begin(), latency_window_.end(), 0.0);
    window_head_    = 0;
    alerts_fired_   = 0;
    orders_tracked_ = 0;
    stats_.reset();
}

} // namespace lattice::anomaly
