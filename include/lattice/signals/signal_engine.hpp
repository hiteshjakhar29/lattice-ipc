#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/signals/order_book.hpp"
#include "lattice/signals/signal_snapshot.hpp"

namespace lattice::signals {

/// Wraps OrderBook and computes OBI + microprice on every BBO change.
///
/// Hot path:
///   1. process(ev) → OrderBook::process(ev)
///   2. If BBO changed: recompute_signals() — 4 multiplies, 3 adds, 1 divide
///   3. If BBO unchanged: update timestamp only — zero arithmetic
///   Returns const ref to the updated SignalSnapshot.
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
    OrderBook      book_;
    SignalSnapshot last_snapshot_{};

    void recompute_signals(uint64_t ts_ns) noexcept;

    static uint64_t clock_ns() noexcept;
};

} // namespace lattice::signals
