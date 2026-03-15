#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::signals {

/// Plain-old-data output of SignalEngine: one snapshot per FeedEvent.
///
/// Fits in 80 bytes — less than two cache lines.
struct SignalSnapshot {
    uint64_t timestamp_ns = 0;    ///< CLOCK_MONOTONIC ns at computation time
    double   mid_price    = 0.0;  ///< (best_bid + best_ask) / 2
    double   microprice   = 0.0;  ///< Qty-weighted mid: (ask_qty*bid + bid_qty*ask) / (bid_qty+ask_qty)
    double   obi          = 0.0;  ///< Order Book Imbalance = (bid_qty-ask_qty)/(bid_qty+ask_qty), [-1,1]
    double   spread       = 0.0;  ///< best_ask - best_bid
    double   ofi          = 0.0;  ///< Order Flow Imbalance = delta_bid_qty - delta_ask_qty at BBO
    double   vamp         = 0.0;  ///< Volume Adjusted Mid Price: VWAP across top 3 bid+ask levels
    double   obi_mean     = 0.0;  ///< Rolling mean of OBI over last 20 BBO updates
    double   obi_std      = 0.0;  ///< Rolling population stddev of OBI over last 20 BBO updates
    double   tfi          = 0.5;  ///< Trade Flow Imbalance: buy_vol/(buy_vol+sell_vol) over last 50 trades [0,1]; 0.5 = no data
};

static_assert(std::is_trivially_copyable_v<SignalSnapshot>);
static_assert(sizeof(SignalSnapshot) == 80);

} // namespace lattice::signals
