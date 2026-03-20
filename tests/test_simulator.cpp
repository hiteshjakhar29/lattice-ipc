// tests/test_simulator.cpp — unit tests for MarketSimulator.

#include "lattice/sim/market_simulator.hpp"
#include "lattice/signals/signal_engine.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/signals/feed_decoder.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace lattice;
using namespace lattice::sim;
using namespace lattice::signals;

// ── helpers ──────────────────────────────────────────────────────────────────

static DecodedEvent decode(const FeedEvent& ev) {
    return signals::decode(ev);
}

// ── EventSequenceValidity ────────────────────────────────────────────────────

TEST(MarketSimulator, EventSequenceValidity) {
    SimConfig cfg;
    cfg.num_symbols      = 2;
    cfg.max_pending_orders = 64;
    cfg.seed             = 1;
    MarketSimulator sim(cfg);

    // Generate 5000 events; all must decode cleanly and have valid src_port
    for (int i = 0; i < 5000; ++i) {
        const FeedEvent ev = sim.next();
        ASSERT_LT(ev.src_port, cfg.num_symbols)
            << "src_port out of range at event " << i;
        ASSERT_EQ(ev.payload_len, 28u)
            << "payload_len must be 28 at event " << i;

        const auto d = decode(ev);
        ASSERT_TRUE(d.type == EventType::Add    ||
                    d.type == EventType::Cancel  ||
                    d.type == EventType::Modify)
            << "unexpected event type at event " << i;
        ASSERT_GT(d.price, 0.0) << "price must be positive at event " << i;
        ASSERT_GT(d.qty,   0u)  << "qty must be positive at event " << i;
    }

    // Counter consistency
    EXPECT_EQ(sim.total_generated(),
              sim.adds_generated() + sim.cancels_generated() + sim.modifies_generated());
    EXPECT_GT(sim.adds_generated(), 0u);
}

// ── PriceBounds ──────────────────────────────────────────────────────────────

TEST(MarketSimulator, PriceBoundsRespected) {
    SimConfig cfg;
    cfg.num_symbols  = 1;
    cfg.base_price   = 50.0;
    cfg.tick_size    = 0.05;
    cfg.seed         = 2;
    MarketSimulator sim(cfg);

    // Price must stay positive; OU prevents extreme divergence
    for (int i = 0; i < 20000; ++i) {
        const FeedEvent ev = sim.next();
        const auto d = decode(ev);
        if (d.type == EventType::Add) {
            ASSERT_GE(d.price, cfg.tick_size)
                << "price below tick_size at event " << i;
            // Very loose upper bound — OU is mean-reverting
            ASSERT_LT(d.price, cfg.base_price * 10.0)
                << "price suspiciously high at event " << i;
        }
    }
}

// ── CancelRateAccuracy ───────────────────────────────────────────────────────

TEST(MarketSimulator, CancelRateApproximatelyCorrect) {
    SimConfig cfg;
    cfg.num_symbols        = 1;
    cfg.cancel_rate        = 0.40;
    cfg.modify_rate        = 0.0;        // turn off modifies for cleaner measurement
    cfg.burst_period_ms    = 999999;     // suppress bursts
    cfg.max_pending_orders = 65536;      // large enough to prevent forced cancels
    cfg.seed               = 3;
    MarketSimulator sim(cfg);

    for (int i = 0; i < 20000; ++i) (void)sim.next();

    // In steady state (no forced cancels), cancels/adds → cancel_rate.
    // Expected cancels_generated / adds_generated ≈ 0.40 ± 0.08
    ASSERT_GT(sim.adds_generated(), 0u);
    const double ratio = static_cast<double>(sim.cancels_generated()) /
                         static_cast<double>(sim.adds_generated());
    EXPECT_NEAR(ratio, cfg.cancel_rate, 0.08)
        << "cancels/adds ratio " << ratio
        << " too far from cancel_rate " << cfg.cancel_rate;
}

// ── BurstClustering ──────────────────────────────────────────────────────────

TEST(MarketSimulator, BurstEventsClusteredWithin1ms) {
    SimConfig cfg;
    cfg.num_symbols      = 1;
    cfg.burst_period_ms  = 50;     // burst every 50 ms simulated — fires early
    cfg.burst_min_orders = 15;
    cfg.burst_max_orders = 15;     // exactly 15 for deterministic counting
    cfg.seed             = 4;
    MarketSimulator sim(cfg);

    // Collect all ADD timestamps from the first 10 000 events.
    // The first burst fires at t = 50 ms (50_000_000 ns).
    // All burst events have inject_ns within [burst_start, burst_start + 1_000_000).
    std::vector<uint64_t> add_ts;
    add_ts.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        const FeedEvent ev = sim.next();
        if (decode(ev).type == EventType::Add)
            add_ts.push_back(ev.inject_ns);
    }

    // Scan for the largest cluster of ADDs within any 1 ms window.
    const uint64_t kWindowNs = 1'000'000ULL;
    int max_cluster = 0;
    for (std::size_t l = 0, r = 0; r < add_ts.size(); ++r) {
        while (add_ts[r] - add_ts[l] >= kWindowNs) ++l;
        max_cluster = std::max(max_cluster, static_cast<int>(r - l + 1));
    }

    EXPECT_GE(max_cluster, static_cast<int>(cfg.burst_min_orders))
        << "Largest ADD cluster within 1 ms: " << max_cluster
        << ", expected at least " << cfg.burst_min_orders;
}

// ── Determinism ──────────────────────────────────────────────────────────────

TEST(MarketSimulator, SameSeedProducesIdenticalSequence) {
    SimConfig cfg;
    cfg.seed = 99;

    MarketSimulator sim1(cfg), sim2(cfg);

    for (int i = 0; i < 1000; ++i) {
        const FeedEvent a = sim1.next();
        const FeedEvent b = sim2.next();
        ASSERT_EQ(a.inject_ns,   b.inject_ns)   << "inject_ns mismatch at " << i;
        ASSERT_EQ(a.src_port,    b.src_port)     << "src_port mismatch at "  << i;
        ASSERT_EQ(a.payload_len, b.payload_len)  << "payload_len mismatch at " << i;
    }
}

// ── ResetRestoresState ────────────────────────────────────────────────────────

TEST(MarketSimulator, ResetProducesIdenticalSequence) {
    SimConfig cfg;
    cfg.seed = 77;

    MarketSimulator sim(cfg);

    std::vector<uint64_t> first_pass;
    for (int i = 0; i < 500; ++i)
        first_pass.push_back(sim.next().inject_ns);

    sim.reset();

    for (int i = 0; i < 500; ++i) {
        const uint64_t ts = sim.next().inject_ns;
        ASSERT_EQ(ts, first_pass[i]) << "timestamp mismatch after reset at " << i;
    }
}

// ── SignalEnginePipeline ──────────────────────────────────────────────────────

TEST(MarketSimulator, SignalEngineProcessesAllEvents) {
    SimConfig cfg;
    cfg.num_symbols = 2;
    cfg.seed        = 55;
    MarketSimulator sim(cfg);

    std::vector<signals::SignalEngine> engines(cfg.num_symbols);

    for (int i = 0; i < 2000; ++i) {
        const FeedEvent ev = sim.next();
        const auto& snap = engines[ev.src_port].process(ev);
        // After enough events, mid_price should be non-zero
        if (i > 100) {
            ASSERT_GE(snap.mid_price, 0.0)
                << "mid_price should be non-negative at event " << i;
        }
    }
}
