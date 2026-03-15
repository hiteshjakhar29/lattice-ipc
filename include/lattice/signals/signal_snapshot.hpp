#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::signals {

/// Plain-old-data output of SignalEngine: one snapshot per FeedEvent.
///
/// Fits in 48 bytes — less than one cache line.
struct SignalSnapshot {
    uint64_t timestamp_ns = 0;    ///< CLOCK_MONOTONIC ns at computation time
    double   mid_price    = 0.0;  ///< (best_bid + best_ask) / 2
    double   microprice   = 0.0;  ///< Qty-weighted mid: (ask_qty*bid + bid_qty*ask) / (bid_qty+ask_qty)
    double   obi          = 0.0;  ///< Order Book Imbalance = (bid_qty-ask_qty)/(bid_qty+ask_qty), [-1,1]
    double   spread       = 0.0;  ///< best_ask - best_bid
};

static_assert(std::is_trivially_copyable_v<SignalSnapshot>);
static_assert(sizeof(SignalSnapshot) == 40);

} // namespace lattice::signals
