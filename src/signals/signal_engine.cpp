#include "lattice/signals/signal_engine.hpp"

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
    const bool bbo_changed = book_.process(ev);
    if (bbo_changed) {
        recompute_signals(ts);
    } else {
        last_snapshot_.timestamp_ns = ts;
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
}

void SignalEngine::reset() noexcept {
    book_.clear();
    last_snapshot_ = SignalSnapshot{};
}

} // namespace lattice::signals
