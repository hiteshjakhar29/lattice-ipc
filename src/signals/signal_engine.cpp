#include "lattice/signals/signal_engine.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <ctime>

namespace lattice::signals {

uint64_t SignalEngine::clock_ns() noexcept {
    struct timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

const SignalSnapshot& SignalEngine::process(const FeedEvent& ev) noexcept {
    const uint64_t ts = clock_ns();

    // Decode once here to detect trade direction for TFI.
    const DecodedEvent d = decode(ev);

    const bool bbo_changed = book_.process(ev);

    if (bbo_changed) {
        // OFI: signed delta of BBO quantities since last BBO update.
        const double bid_qty = static_cast<double>(book_.best_bid_qty());
        const double ask_qty = static_cast<double>(book_.best_ask_qty());
        ofi_window_.push((bid_qty - prev_best_bid_qty_) - (ask_qty - prev_best_ask_qty_));
        prev_best_bid_qty_ = bid_qty;
        prev_best_ask_qty_ = ask_qty;

        recompute_signals(ts);
    } else {
        last_snapshot_.timestamp_ns = ts;
    }

    // VAMP: always recompute — top 3 levels can change without a BBO move.
    last_snapshot_.vamp = compute_vamp();

    // TFI: update rolling trade window on every trade event.
    if (d.type == EventType::Trade) {
        trade_window_.push({d.qty, d.is_bid});
        last_snapshot_.tfi = compute_tfi();
    }

    return last_snapshot_;
}

void SignalEngine::recompute_signals(uint64_t ts_ns) noexcept {
    const double bid     = book_.best_bid();
    const double ask     = book_.best_ask();
    const double bid_qty = static_cast<double>(book_.best_bid_qty());
    const double ask_qty = static_cast<double>(book_.best_ask_qty());
    const double denom   = bid_qty + ask_qty;

    last_snapshot_.timestamp_ns = ts_ns;
    last_snapshot_.mid_price    = (bid + ask) * 0.5;
    last_snapshot_.spread       = ask - bid;

    if (denom > 0.0) {
        last_snapshot_.obi        = (bid_qty - ask_qty) / denom;
        last_snapshot_.microprice = (ask_qty * bid + bid_qty * ask) / denom;
    } else {
        last_snapshot_.obi        = 0.0;
        last_snapshot_.microprice = 0.0;
    }

    // Update rolling OBI window and compute stats.
    obi_window_.push(last_snapshot_.obi);
    last_snapshot_.obi_mean = obi_window_.mean();
    last_snapshot_.obi_std  = obi_window_.stddev();

    // Latest OFI value from rolling window.
    const std::size_t n = ofi_window_.count();
    last_snapshot_.ofi = (n > 0) ? ofi_window_[n - 1] : 0.0;
}

double SignalEngine::compute_vamp() const noexcept {
    static constexpr std::size_t DEPTH = 3;
    double   prices[DEPTH];
    uint32_t qtys[DEPTH];

    double num = 0.0, den = 0.0;

    const std::size_t nb = book_.top_bids(prices, qtys, DEPTH);
    for (std::size_t i = 0; i < nb; ++i) {
        const double q = static_cast<double>(qtys[i]);
        num += prices[i] * q;
        den += q;
    }

    const std::size_t na = book_.top_asks(prices, qtys, DEPTH);
    for (std::size_t i = 0; i < na; ++i) {
        const double q = static_cast<double>(qtys[i]);
        num += prices[i] * q;
        den += q;
    }

    return den > 0.0 ? num / den : 0.0;
}

double SignalEngine::compute_tfi() const noexcept {
    double buy_vol = 0.0, sell_vol = 0.0;
    for (std::size_t i = 0; i < trade_window_.count(); ++i) {
        const TradeRecord& tr = trade_window_[i];
        if (tr.is_buy) buy_vol  += static_cast<double>(tr.qty);
        else           sell_vol += static_cast<double>(tr.qty);
    }
    const double total = buy_vol + sell_vol;
    return total > 0.0 ? buy_vol / total : 0.5;
}

void SignalEngine::reset() noexcept {
    book_.clear();
    last_snapshot_     = SignalSnapshot{};
    prev_best_bid_qty_ = 0.0;
    prev_best_ask_qty_ = 0.0;
    ofi_window_.clear();
    obi_window_.clear();
    trade_window_.clear();
}

} // namespace lattice::signals
