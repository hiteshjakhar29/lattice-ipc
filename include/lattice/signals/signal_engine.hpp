#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/signals/order_book.hpp"
#include "lattice/signals/rolling_window.hpp"
#include "lattice/signals/signal_snapshot.hpp"

#include <cstdint>

namespace lattice::signals {

/// Wraps OrderBook and computes microstructure signals on every FeedEvent.
///
/// Hot path (BBO change):  recompute_signals() — full signal set refresh.
/// Fast path (deep change): update VAMP + timestamp only.
/// Trade events additionally update Trade Flow Imbalance.
class SignalEngine {
public:
    SignalEngine() = default;

    /// Feed one event through the book and return the current snapshot.
    [[nodiscard]] const SignalSnapshot& process(const FeedEvent& ev) noexcept;

    [[nodiscard]] const SignalSnapshot& last_snapshot() const noexcept {
        return last_snapshot_;
    }

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }

    void reset() noexcept;

private:
    struct TradeRecord { uint32_t qty; bool is_buy; };

    OrderBook      book_;
    SignalSnapshot last_snapshot_{};

    double prev_best_bid_qty_ = 0.0;
    double prev_best_ask_qty_ = 0.0;

    RollingWindow<double, 10>      ofi_window_{};
    RollingWindow<double, 20>      obi_window_{};
    RollingWindow<TradeRecord, 50> trade_window_{};

    void   recompute_signals(uint64_t ts_ns) noexcept;
    double compute_vamp()  const noexcept;
    double compute_tfi()   const noexcept;

    static uint64_t clock_ns() noexcept;
};

} // namespace lattice::signals
