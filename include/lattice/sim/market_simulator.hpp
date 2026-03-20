#pragma once
// Header-only realistic market event simulator.
//
// Generates sequences of ADD / MODIFY / CANCEL / TRADE FeedEvents that obey
// realistic statistical properties:
//   • Poisson order arrival process (exponential inter-arrivals)
//   • Ornstein-Uhlenbeck mean-reverting mid-price
//   • Log-normal order size distribution
//   • Cancel ≈ cancel_rate × adds,  Modify ≈ modify_rate × adds
//   • Periodic bursts of 10–50 ADD events within a 1 ms window
//
// All randomness is seeded through SimConfig::seed for reproducibility.

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace lattice::sim {

/// All tunable simulation parameters with sensible defaults.
struct SimConfig {
    uint32_t num_symbols        = 4;       ///< Independent symbols (each gets its own book)
    double   tick_size          = 0.01;    ///< Minimum price increment
    double   base_price         = 100.0;   ///< Mean-reversion target for mid-price
    double   order_arrival_rate = 1000.0;  ///< Mean new-order arrivals per second (Poisson λ)

    double   cancel_rate        = 0.40;    ///< Cancels as a fraction of adds
    double   modify_rate        = 0.20;    ///< Modifies as a fraction of adds

    double   qty_mean_log       = 4.6;     ///< Log-normal μ for order qty  (≈ 100 units mean)
    double   qty_stddev_log     = 0.8;     ///< Log-normal σ for order qty

    double   price_reversion    = 0.05;    ///< OU mean-reversion speed θ
    double   price_volatility   = 0.002;   ///< OU diffusion σ per step

    uint32_t burst_period_ms    = 500;     ///< Milliseconds between burst episodes per symbol
    uint32_t burst_min_orders   = 10;      ///< Minimum orders in one burst
    uint32_t burst_max_orders   = 50;      ///< Maximum orders in one burst

    uint32_t max_pending_orders = 256;     ///< Per-symbol cap (prevents unbounded growth)
    uint64_t seed               = 42;      ///< RNG seed — same seed ⟹ identical sequence
};

/// Generates realistic order-book event sequences as FeedEvents.
///
/// Symbol identity is encoded in FeedEvent::src_port (0 .. num_symbols-1)
/// so anomaly detectors can track per-symbol state.
///
/// All events have payload_len = 28 so SignalEngine::process() decodes them.
class MarketSimulator {
public:
    explicit MarketSimulator(const SimConfig& cfg = {})
        : cfg_(cfg), rng_(cfg.seed)
    {
        symbols_.resize(cfg_.num_symbols);
        const uint64_t first_burst =
            static_cast<uint64_t>(cfg_.burst_period_ms) * 1'000'000ULL;
        for (uint32_t i = 0; i < cfg_.num_symbols; ++i) {
            symbols_[i].mid_price     = cfg_.base_price;
            symbols_[i].next_burst_ns = first_burst;
        }
    }

    // ── hot path ──────────────────────────────────────────────────────────────

    /// Returns the next generated FeedEvent and advances the simulated clock.
    [[nodiscard]] FeedEvent next() noexcept {
        // Drain any in-flight burst events first
        if (burst_pos_ < burst_queue_.size())
            return consume_burst();

        burst_queue_.clear();
        burst_pos_ = 0;

        // Advance simulated time by one Poisson inter-arrival
        current_ns_ += next_arrival_ns();

        // Round-robin symbol selection
        const uint32_t sym = next_symbol_++ % cfg_.num_symbols;
        auto& s = symbols_[sym];

        // Check if a burst episode is due for this symbol
        if (current_ns_ >= s.next_burst_ns) {
            fill_burst(sym, current_ns_);
            s.next_burst_ns = current_ns_ +
                              static_cast<uint64_t>(cfg_.burst_period_ms) * 1'000'000ULL;
            if (!burst_queue_.empty())
                return consume_burst();
        }

        return pick_event(sym, current_ns_);
    }

    // ── accessors ─────────────────────────────────────────────────────────────

    [[nodiscard]] uint64_t current_ns()         const noexcept { return current_ns_; }
    [[nodiscard]] uint64_t adds_generated()     const noexcept { return n_adds_; }
    [[nodiscard]] uint64_t cancels_generated()  const noexcept { return n_cancels_; }
    [[nodiscard]] uint64_t modifies_generated() const noexcept { return n_modifies_; }
    [[nodiscard]] uint64_t trades_generated()   const noexcept { return n_trades_; }
    [[nodiscard]] uint64_t total_generated()    const noexcept {
        return n_adds_ + n_cancels_ + n_modifies_ + n_trades_;
    }
    [[nodiscard]] const SimConfig& config() const noexcept { return cfg_; }

    void reset() noexcept {
        rng_ = std::mt19937_64(cfg_.seed);
        const uint64_t first_burst =
            static_cast<uint64_t>(cfg_.burst_period_ms) * 1'000'000ULL;
        for (uint32_t i = 0; i < cfg_.num_symbols; ++i) {
            symbols_[i].mid_price     = cfg_.base_price;
            symbols_[i].next_burst_ns = first_burst;
            symbols_[i].pending.clear();
        }
        burst_queue_.clear();
        burst_pos_     = 0;
        next_order_id_ = 1;
        current_ns_    = 0;
        next_symbol_   = 0;
        n_adds_ = n_cancels_ = n_modifies_ = n_trades_ = 0;
    }

private:
    // ── per-symbol state ──────────────────────────────────────────────────────

    struct PendingOrder {
        uint64_t order_id;
        double   price;
        uint32_t qty;
        bool     is_bid;
    };

    struct SymbolState {
        double                    mid_price     = 0.0;
        uint64_t                  next_burst_ns = 0;
        std::vector<PendingOrder> pending;
    };

    SimConfig                cfg_;
    std::mt19937_64          rng_;
    std::vector<SymbolState> symbols_;

    std::vector<FeedEvent>   burst_queue_;
    std::size_t              burst_pos_     = 0;

    uint64_t next_order_id_ = 1;
    uint64_t current_ns_    = 0;
    uint32_t next_symbol_   = 0;

    uint64_t n_adds_    = 0;
    uint64_t n_cancels_ = 0;
    uint64_t n_modifies_= 0;
    uint64_t n_trades_  = 0;

    // ── event selection ───────────────────────────────────────────────────────

    FeedEvent consume_burst() noexcept {
        current_ns_ = burst_queue_[burst_pos_].inject_ns;
        return burst_queue_[burst_pos_++];
    }

    FeedEvent pick_event(uint32_t sym, uint64_t ts) noexcept {
        auto& s = symbols_[sym];

        // Force a cancel when pending table is full
        if (s.pending.size() >= cfg_.max_pending_orders)
            return emit_cancel(sym, ts);

        if (s.pending.empty())
            return emit_add(sym, ts);

        // Probabilities derived from configured rates:
        //   P(cancel) = cancel_rate / (1 + cancel_rate + modify_rate)
        //   P(modify) = modify_rate / (1 + cancel_rate + modify_rate)
        //   P(add)    = 1           / (1 + cancel_rate + modify_rate)
        const double denom    = 1.0 + cfg_.cancel_rate + cfg_.modify_rate;
        const double p_cancel = cfg_.cancel_rate / denom;
        const double p_modify = cfg_.modify_rate / denom;

        std::uniform_real_distribution<double> u(0.0, 1.0);
        const double r = u(rng_);

        if (r < p_cancel)              return emit_cancel(sym, ts);
        if (r < p_cancel + p_modify)   return emit_modify(sym, ts);
        return emit_add(sym, ts);
    }

    // ── event emitters ────────────────────────────────────────────────────────

    FeedEvent emit_add(uint32_t sym, uint64_t ts) noexcept {
        auto& s = symbols_[sym];

        // Ornstein-Uhlenbeck price update
        std::normal_distribution<double> noise(0.0, 1.0);
        s.mid_price += cfg_.price_reversion * (cfg_.base_price - s.mid_price)
                     + cfg_.price_volatility * noise(rng_);
        s.mid_price = std::max(s.mid_price, cfg_.tick_size);

        const bool is_bid = (rng_() & 1u) != 0u;

        std::uniform_int_distribution<int> tick_off(0, 4);
        double price = is_bid
            ? s.mid_price - tick_off(rng_) * cfg_.tick_size
            : s.mid_price + tick_off(rng_) * cfg_.tick_size;
        price = std::round(price / cfg_.tick_size) * cfg_.tick_size;
        price = std::max(price, cfg_.tick_size);

        const uint32_t qty = next_qty();
        const uint64_t oid = next_order_id_++;

        s.pending.push_back({oid, price, qty, is_bid});
        ++n_adds_;

        return make_ev(signals::EventType::Add, is_bid, oid, price, qty, sym, ts);
    }

    FeedEvent emit_cancel(uint32_t sym, uint64_t ts) noexcept {
        auto& s = symbols_[sym];
        if (s.pending.empty()) return emit_add(sym, ts);

        const std::size_t i = random_pending_idx(s);
        const PendingOrder po = s.pending[i];
        s.pending.erase(s.pending.begin() + static_cast<std::ptrdiff_t>(i));
        ++n_cancels_;

        return make_ev(signals::EventType::Cancel,
                       po.is_bid, po.order_id, po.price, po.qty, sym, ts);
    }

    FeedEvent emit_modify(uint32_t sym, uint64_t ts) noexcept {
        auto& s = symbols_[sym];
        if (s.pending.empty()) return emit_add(sym, ts);

        const std::size_t i = random_pending_idx(s);
        PendingOrder& po = s.pending[i];

        std::uniform_real_distribution<double> delta(0.5, 1.5);
        po.qty = static_cast<uint32_t>(
            std::max(1.0, std::round(static_cast<double>(po.qty) * delta(rng_))));
        ++n_modifies_;

        return make_ev(signals::EventType::Modify,
                       po.is_bid, po.order_id, po.price, po.qty, sym, ts);
    }

    // Burst: cluster of ADD events all within a 1 ms window
    void fill_burst(uint32_t sym, uint64_t burst_start_ns) noexcept {
        burst_queue_.clear();
        burst_pos_ = 0;

        std::uniform_int_distribution<uint32_t> count_dist(
            cfg_.burst_min_orders, cfg_.burst_max_orders);
        const uint32_t n = count_dist(rng_);

        std::uniform_int_distribution<uint64_t> jitter(0, 999'999ULL); // 0–999 µs
        for (uint32_t k = 0; k < n; ++k) {
            const uint64_t ts = burst_start_ns + jitter(rng_);
            FeedEvent ev = emit_add(sym, ts);
            ev.inject_ns  = ts;
            ev.receive_ns = ts;
            burst_queue_.push_back(ev);
        }

        // Deliver in chronological order
        std::sort(burst_queue_.begin(), burst_queue_.end(),
                  [](const FeedEvent& a, const FeedEvent& b) {
                      return a.inject_ns < b.inject_ns;
                  });
    }

    // ── helpers ───────────────────────────────────────────────────────────────

    static FeedEvent make_ev(signals::EventType type, bool is_bid,
                              uint64_t order_id, double price, uint32_t qty,
                              uint32_t sym, uint64_t ts) noexcept {
        auto ev = signals::make_feed_event<FeedEvent>(type, is_bid, order_id, price, qty);
        ev.inject_ns  = ts;
        ev.receive_ns = ts;
        ev.src_port   = static_cast<uint16_t>(sym);
        return ev;
    }

    std::size_t random_pending_idx(const SymbolState& s) noexcept {
        std::uniform_int_distribution<std::size_t> d(0, s.pending.size() - 1);
        return d(rng_);
    }

    uint64_t next_arrival_ns() noexcept {
        std::exponential_distribution<double> exp_d(cfg_.order_arrival_rate);
        return static_cast<uint64_t>(exp_d(rng_) * 1e9);
    }

    uint32_t next_qty() noexcept {
        std::lognormal_distribution<double> ln(cfg_.qty_mean_log, cfg_.qty_stddev_log);
        return static_cast<uint32_t>(
            std::clamp(std::round(ln(rng_)), 1.0, 1'000'000.0));
    }
};

} // namespace lattice::sim
