#pragma once

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <cstdint>
#include <functional>
#include <map>

namespace lattice::signals {

/// Maintains a live, two-sided order book and a cached best-bid/offer (BBO).
///
/// Processes FeedEvents dispatched from the upstream shm channel and updates
/// the bid and ask price levels incrementally.
///
/// BBO is cached in four plain fields updated on every top-of-book change,
/// giving O(1) access from SignalEngine without touching the underlying maps.
///
/// Allocation policy:
///   std::map nodes are allocated when new price levels appear (warm-up phase).
///   In steady state, modify events update existing nodes in-place — no allocation.
class OrderBook {
public:
    OrderBook() = default;

    /// Process one FeedEvent. Returns true if the BBO changed (caller should
    /// recompute signals). Returns false for deep-book-only changes.
    [[nodiscard]] bool process(const FeedEvent& ev) noexcept;

    // ── BBO accessors (O(1) — reads cached fields) ────────────────────────────
    [[nodiscard]] double   best_bid()     const noexcept { return best_bid_; }
    [[nodiscard]] double   best_ask()     const noexcept { return best_ask_; }
    [[nodiscard]] uint32_t best_bid_qty() const noexcept { return best_bid_qty_; }
    [[nodiscard]] uint32_t best_ask_qty() const noexcept { return best_ask_qty_; }
    [[nodiscard]] bool     has_both_sides() const noexcept {
        return !bids_.empty() && !asks_.empty();
    }

    /// Fill `prices` and `qtys` with the best `n` bid levels (highest first).
    /// Returns the number of levels actually filled (may be less than n).
    std::size_t top_bids(double* prices, uint32_t* qtys, std::size_t n) const noexcept;

    /// Fill `prices` and `qtys` with the best `n` ask levels (lowest first).
    /// Returns the number of levels actually filled (may be less than n).
    std::size_t top_asks(double* prices, uint32_t* qtys, std::size_t n) const noexcept;

    /// Reset to empty state (both sides cleared, BBO zeroed).
    void clear() noexcept;

private:
    // Bid side: descending by price so begin() == best bid
    std::map<double, uint32_t, std::greater<double>> bids_;
    // Ask side: ascending by price so begin() == best ask
    std::map<double, uint32_t>                       asks_;

    double   best_bid_     = 0.0;
    double   best_ask_     = 0.0;
    uint32_t best_bid_qty_ = 0;
    uint32_t best_ask_qty_ = 0;

    void on_add   (double price, uint32_t qty, bool is_bid) noexcept;
    void on_modify(double price, uint32_t qty, bool is_bid) noexcept;
    void on_cancel(double price, bool is_bid)               noexcept;
    void on_trade (double price, uint32_t qty)              noexcept;

    // Refresh BBO cache from the top of each side's map. O(1) after construction.
    void refresh_bbo() noexcept;
};

} // namespace lattice::signals
