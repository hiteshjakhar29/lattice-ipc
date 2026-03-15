#include "lattice/signals/order_book.hpp"

namespace lattice::signals {

bool OrderBook::process(const FeedEvent& ev) noexcept {
    const DecodedEvent d = decode(ev);

    const double   old_bid = best_bid_;
    const double   old_ask = best_ask_;
    const uint32_t old_bq  = best_bid_qty_;
    const uint32_t old_aq  = best_ask_qty_;

    switch (d.type) {
        case EventType::Add:    on_add(d.price, d.qty, d.is_bid);    break;
        case EventType::Modify: on_modify(d.price, d.qty, d.is_bid); break;
        case EventType::Cancel: on_cancel(d.price, d.is_bid);        break;
        case EventType::Trade:  on_trade(d.price, d.qty);            break;
        default:                return false;
    }

    return (best_bid_ != old_bid || best_ask_ != old_ask ||
            best_bid_qty_ != old_bq || best_ask_qty_ != old_aq);
}

void OrderBook::on_add(double price, uint32_t qty, bool is_bid) noexcept {
    if (is_bid) {
        bids_[price] += qty;
    } else {
        asks_[price] += qty;
    }
    refresh_bbo();
}

void OrderBook::on_modify(double price, uint32_t qty, bool is_bid) noexcept {
    if (is_bid) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            it->second = qty;
        } else {
            bids_[price] = qty;
        }
    } else {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            it->second = qty;
        } else {
            asks_[price] = qty;
        }
    }
    refresh_bbo();
}

void OrderBook::on_cancel(double price, bool is_bid) noexcept {
    if (is_bid) {
        bids_.erase(price);
    } else {
        asks_.erase(price);
    }
    refresh_bbo();
}

void OrderBook::on_trade(double price, uint32_t qty) noexcept {
    // Trades reduce qty on both sides at the traded price.
    // Try ask side first (aggressive buyer hits passive ask).
    auto ask_it = asks_.find(price);
    if (ask_it != asks_.end()) {
        if (ask_it->second <= qty) {
            asks_.erase(ask_it);
        } else {
            ask_it->second -= qty;
        }
    }
    auto bid_it = bids_.find(price);
    if (bid_it != bids_.end()) {
        if (bid_it->second <= qty) {
            bids_.erase(bid_it);
        } else {
            bid_it->second -= qty;
        }
    }
    refresh_bbo();
}

void OrderBook::refresh_bbo() noexcept {
    if (!bids_.empty()) {
        auto it    = bids_.begin();
        best_bid_     = it->first;
        best_bid_qty_ = it->second;
    } else {
        best_bid_     = 0.0;
        best_bid_qty_ = 0;
    }

    if (!asks_.empty()) {
        auto it    = asks_.begin();
        best_ask_     = it->first;
        best_ask_qty_ = it->second;
    } else {
        best_ask_     = 0.0;
        best_ask_qty_ = 0;
    }
}

std::size_t OrderBook::top_bids(double* prices, uint32_t* qtys, std::size_t n) const noexcept {
    std::size_t filled = 0;
    for (auto it = bids_.begin(); it != bids_.end() && filled < n; ++it, ++filled) {
        prices[filled] = it->first;
        qtys[filled]   = it->second;
    }
    return filled;
}

std::size_t OrderBook::top_asks(double* prices, uint32_t* qtys, std::size_t n) const noexcept {
    std::size_t filled = 0;
    for (auto it = asks_.begin(); it != asks_.end() && filled < n; ++it, ++filled) {
        prices[filled] = it->first;
        qtys[filled]   = it->second;
    }
    return filled;
}

void OrderBook::clear() noexcept {
    bids_.clear();
    asks_.clear();
    best_bid_     = 0.0;
    best_ask_     = 0.0;
    best_bid_qty_ = 0;
    best_ask_qty_ = 0;
}

} // namespace lattice::signals
