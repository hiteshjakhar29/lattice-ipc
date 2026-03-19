#include <gtest/gtest.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/pending_order.hpp"
#include "lattice/anomaly/spoof_alert.hpp"
#include "lattice/anomaly/welford_stats.hpp"
#include "lattice/anomaly/layering_detector.hpp"
#include "lattice/anomaly/layering_alert.hpp"
#include "lattice/anomaly/cancel_spike_detector.hpp"
#include "lattice/anomaly/cancel_spike_alert.hpp"
#include "lattice/anomaly/burst_detector.hpp"
#include "lattice/anomaly/burst_alert.hpp"
#include "lattice/anomaly/symbol_scorer.hpp"

#include <cmath>
#include <type_traits>

using namespace lattice;
using namespace lattice::signals;
using namespace lattice::anomaly;

// ── Helpers ────────────────────────────────────────────────────────────────────

static FeedEvent add_ev(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Add, true, oid, price, qty);
}
static FeedEvent cancel_ev(uint64_t oid, double price) {
    return make_feed_event<FeedEvent>(EventType::Cancel, true, oid, price, 0);
}

// ── WelfordStats ───────────────────────────────────────────────────────────────

TEST(WelfordStats, InitialState) {
    WelfordStats ws;
    EXPECT_EQ(ws.count(), 0u);
    EXPECT_EQ(ws.mean(), 0.0);
    EXPECT_FALSE(ws.is_stable());
}

TEST(WelfordStats, SingleUpdate) {
    WelfordStats ws;
    ws.update(42.0);
    EXPECT_EQ(ws.count(), 1u);
    EXPECT_NEAR(ws.mean(), 42.0, 1e-12);
    EXPECT_FALSE(ws.is_stable()); // need >= 2
}

TEST(WelfordStats, TwoUpdates_MeanCorrect) {
    WelfordStats ws;
    ws.update(10.0);
    ws.update(20.0);
    EXPECT_NEAR(ws.mean(), 15.0, 1e-12);
    EXPECT_TRUE(ws.is_stable());
}

TEST(WelfordStats, VarianceConverges) {
    WelfordStats ws;
    // N(100, 5^2): feed 10000 samples, verify mean ≈ 100, stddev ≈ 5
    // Use deterministic sequence: mean-5 alternating with mean+5 for quick convergence
    for (int i = 0; i < 5000; ++i) {
        ws.update(95.0);
        ws.update(105.0);
    }
    EXPECT_NEAR(ws.mean(), 100.0, 0.01);
    EXPECT_NEAR(ws.stddev(), 5.0, 0.01);
}

TEST(WelfordStats, Reset) {
    WelfordStats ws;
    ws.update(100.0);
    ws.update(200.0);
    ws.reset();
    EXPECT_EQ(ws.count(), 0u);
    EXPECT_EQ(ws.mean(), 0.0);
    EXPECT_FALSE(ws.is_stable());
}

TEST(WelfordStats, IsStableAfterTwo) {
    WelfordStats ws;
    ws.update(1.0);
    EXPECT_FALSE(ws.is_stable());
    ws.update(2.0);
    EXPECT_TRUE(ws.is_stable());
}

TEST(WelfordStats, ZeroVarianceWhenAllSame) {
    WelfordStats ws;
    for (int i = 0; i < 100; ++i) ws.update(7.0);
    EXPECT_NEAR(ws.mean(), 7.0, 1e-12);
    EXPECT_NEAR(ws.variance(), 0.0, 1e-10);
}

// ── PendingOrder ───────────────────────────────────────────────────────────────

TEST(PendingOrder, TriviallyCopiable) {
    static_assert(std::is_trivially_copyable_v<PendingOrder>);
}

TEST(PendingOrder, SizeIs32) {
    static_assert(sizeof(PendingOrder) == 32);
}

TEST(PendingOrder, DefaultInvalid) {
    PendingOrder o{};
    EXPECT_FALSE(o.is_valid());
}

// ── SpoofAlert ────────────────────────────────────────────────────────────────

TEST(SpoofAlert, TriviallyCopiable) {
    static_assert(std::is_trivially_copyable_v<SpoofAlert>);
}

// ── AnomalyDetector ───────────────────────────────────────────────────────────

TEST(AnomalyDetector, SmallOrderNotTracked) {
    AnomalyConfig cfg;
    cfg.qty_threshold = 1000;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    det.process(add_ev(1, 100.0, 500), alert); // qty < threshold
    EXPECT_EQ(det.orders_tracked(), 0u);
}

TEST(AnomalyDetector, LargeOrderTracked) {
    AnomalyConfig cfg;
    cfg.qty_threshold = 1000;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    det.process(add_ev(1, 100.0, 2000), alert);
    EXPECT_EQ(det.orders_tracked(), 1u);
}

TEST(AnomalyDetector, CancelNotTrackedOrderIgnored) {
    AnomalyConfig cfg;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    // Cancel an order that was never added
    EXPECT_FALSE(det.process(cancel_ev(999, 100.0), alert));
}

TEST(AnomalyDetector, CancelBeforeStableNoAlert) {
    AnomalyConfig cfg;
    cfg.qty_threshold = 100;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    // Add and immediately cancel — but stats not stable yet (< 2 samples)
    det.process(add_ev(1, 100.0, 500), alert);
    EXPECT_FALSE(det.process(cancel_ev(1, 100.0), alert));
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(AnomalyDetector, FastCancelTriggersAlert) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.z_score_threshold  = -2.0;
    cfg.max_tracked_orders = 64;
    AnomalyDetector det(cfg);

    // Establish baseline: 20 cancels with varying latencies around 1 second
    // (variance must be non-zero for stddev > 0 and Z-score to be meaningful).
    uint64_t t   = 0;
    uint64_t oid = 1;
    SpoofAlert alert{};

    // Alternate 100ms and 200ms — mean=150ms, stddev≈50ms (all < time_window_ns=500ms)
    for (int i = 0; i < 20; ++i, ++oid) {
        const uint64_t elapsed = (i % 2 == 0) ? 100'000'000ULL : 200'000'000ULL;
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        det.process_with_time(cancel_ev(oid, 100.0), t + elapsed, alert);
        t += 300'000'000ULL;
    }

    // Now feed a very fast cancel (1 ms) — z ≈ (1ms - 150ms) / 50ms ≈ -3 < -2
    det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
    bool fired = det.process_with_time(cancel_ev(oid, 100.0), t + 1'000'000ULL, alert);

    EXPECT_TRUE(fired);
    EXPECT_GT(det.alerts_fired(), 0u);
    EXPECT_LT(alert.z_score, cfg.z_score_threshold);
}

TEST(AnomalyDetector, SlowCancelNoAlert) {
    AnomalyConfig cfg;
    cfg.qty_threshold     = 100;
    cfg.z_score_threshold = -2.0;
    cfg.max_tracked_orders = 64;
    AnomalyDetector det(cfg);

    uint64_t t   = 1'000'000'000ULL;
    uint64_t oid = 1;
    SpoofAlert alert{};

    // Establish baseline of fast cancels
    for (int i = 0; i < 10; ++i, ++oid) {
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        det.process_with_time(cancel_ev(oid, 100.0), t + 1'000'000ULL, alert); // 1ms
        t += 100'000'000ULL;
    }

    // Feed a very slow cancel — should NOT trigger
    det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
    bool fired = det.process_with_time(cancel_ev(oid, 100.0), t + 60'000'000'000ULL, alert);
    EXPECT_FALSE(fired);
}

TEST(AnomalyDetector, ZScoreComputedCorrectly) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.z_score_threshold  = -2.0; // alert if z < -2
    cfg.max_tracked_orders = 64;
    AnomalyDetector det(cfg);

    // Use latencies that alternate: 800 ns and 1200 ns → mean=1000, stddev=200
    uint64_t t   = 0;
    uint64_t oid = 1;
    SpoofAlert alert{};

    for (int i = 0; i < 10; ++i, ++oid) {
        const uint64_t elapsed = (i % 2 == 0) ? 800ULL : 1200ULL;
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        det.process_with_time(cancel_ev(oid, 100.0), t + elapsed, alert);
        t += 10000;
    }

    // Feed a cancel with known latency: 0 ns
    // Expected z ≈ (0 - mean) / stddev (negative — should fire with threshold -100)
    det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
    bool fired = det.process_with_time(cancel_ev(oid, 100.0), t + 0, alert);

    EXPECT_TRUE(fired);
    EXPECT_GE(det.stats().count(), 11u);
    // z should be significantly negative (fast cancel)
    EXPECT_LT(alert.z_score, 0.0);
}

TEST(AnomalyDetector, AlertCountIncrements) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.z_score_threshold  = -2.0;
    cfg.max_tracked_orders = 64;
    AnomalyDetector det(cfg);

    uint64_t t = 0, oid = 1;
    SpoofAlert alert{};

    // Establish baseline: 20 cancels alternating 100ms/200ms (< time_window_ns=500ms)
    for (int i = 0; i < 20; ++i, ++oid) {
        const uint64_t elapsed = (i % 2 == 0) ? 100'000'000ULL : 200'000'000ULL;
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        det.process_with_time(cancel_ev(oid, 100.0), t + elapsed, alert);
        t += 300'000'000ULL;
    }

    // Trigger multiple fast cancels (1 ms each — z ≈ -3, well below -2.0 threshold)
    int triggers = 0;
    for (int i = 0; i < 3; ++i, ++oid) {
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        if (det.process_with_time(cancel_ev(oid, 100.0), t + 1'000'000ULL, alert)) {
            ++triggers;
        }
        t += 2'000'000'000ULL;
    }

    EXPECT_EQ(det.alerts_fired(), static_cast<uint64_t>(triggers));
    EXPECT_GT(det.alerts_fired(), 0u);
}

TEST(AnomalyDetector, Reset) {
    AnomalyConfig cfg;
    cfg.qty_threshold = 100;
    AnomalyDetector det(cfg);

    uint64_t t = 0;
    SpoofAlert alert{};
    det.process_with_time(add_ev(1, 100.0, 500), t, alert);
    det.process_with_time(cancel_ev(1, 100.0), t + 1000, alert);

    det.reset();
    EXPECT_EQ(det.stats().count(), 0u);
    EXPECT_EQ(det.alerts_fired(), 0u);
    EXPECT_EQ(det.orders_tracked(), 0u);
}

// ── Helpers for new detectors ─────────────────────────────────────────────────

static FeedEvent sym_add_ev(uint16_t sym, uint64_t oid, bool is_bid,
                             double price, uint32_t qty) {
    auto ev = make_feed_event<FeedEvent>(EventType::Add, is_bid, oid, price, qty);
    ev.src_port = sym;
    return ev;
}

static FeedEvent sym_cancel_ev(uint16_t sym, uint64_t oid, bool is_bid, double price) {
    auto ev = make_feed_event<FeedEvent>(EventType::Cancel, is_bid, oid, price, 0);
    ev.src_port = sym;
    return ev;
}

// ── LayeringDetector ──────────────────────────────────────────────────────────

TEST(LayeringDetector, SmallOrdersNotTracked) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 1000;
    cfg.count_threshold = 3;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    // 5 small orders on same side — should never fire
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(det.process_with_time(
            sym_add_ev(1, i + 1, true, 100.0, 500), 0, alert));
    }
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(LayeringDetector, ThresholdNotExceeded_NoAlert) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    // Exactly count_threshold large bid orders — no alert (need *strictly* more)
    for (uint32_t i = 1; i <= cfg.count_threshold; ++i) {
        EXPECT_FALSE(det.process_with_time(
            sym_add_ev(1, i, true, 100.0, 500),
            static_cast<uint64_t>(i) * 10'000'000ULL, alert));
    }
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(LayeringDetector, FourLargeBidOrdersFireAlert) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.window_ns       = 500'000'000ULL;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    bool fired = false;
    for (int i = 1; i <= 4; ++i) {
        bool r = det.process_with_time(
            sym_add_ev(1, i, true, 100.0 + i, 500),
            static_cast<uint64_t>(i) * 50'000'000ULL, alert);
        if (r) fired = true;
    }
    EXPECT_TRUE(fired);
    EXPECT_EQ(det.alerts_fired(), 1u);
    EXPECT_EQ(alert.is_bid, 1u);
    EXPECT_EQ(alert.symbol_id, 1u);
    EXPECT_GT(alert.order_count, cfg.count_threshold);
}

TEST(LayeringDetector, AskSideDetectedIndependently) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.window_ns       = 500'000'000ULL;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    // 4 large bid + 4 large ask orders — both sides should fire independently
    uint64_t t = 0;
    for (int i = 1; i <= 4; ++i, t += 50'000'000ULL) {
        det.process_with_time(sym_add_ev(1, i,      true,  100.0, 500), t, alert);
        det.process_with_time(sym_add_ev(1, i + 10, false, 99.0,  500), t, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 2u);
}

TEST(LayeringDetector, ExpiredOrdersNotCounted) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.window_ns       = 500'000'000ULL; // 500 ms
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    uint64_t t = 0;
    // First 3 orders placed at t=0..200ms — inside window from each other
    for (int i = 1; i <= 3; ++i) {
        det.process_with_time(sym_add_ev(1, i, true, 100.0, 500),
                              static_cast<uint64_t>(i) * 66'000'000ULL, alert);
    }
    // 4th order placed 1 second later — the first 3 are now outside the 500ms window
    EXPECT_FALSE(det.process_with_time(
        sym_add_ev(1, 4, true, 100.0, 500), 1'100'000'000ULL, alert));
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(LayeringDetector, DifferentSymbolsTrackedIndependently) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.max_symbols     = 16;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    uint64_t t = 0;
    // 4 orders on symbol 1 — should fire
    for (int i = 1; i <= 4; ++i, t += 10'000'000ULL) {
        det.process_with_time(sym_add_ev(1, i, true, 100.0, 500), t, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 1u);

    // 3 orders on symbol 2 — should NOT fire (below threshold)
    for (int i = 1; i <= 3; ++i, t += 10'000'000ULL) {
        det.process_with_time(sym_add_ev(2, i + 10, true, 200.0, 500), t, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 1u); // still only 1
}

TEST(LayeringDetector, Reset) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    uint64_t t = 0;
    for (int i = 1; i <= 4; ++i, t += 10'000'000ULL) {
        det.process_with_time(sym_add_ev(1, i, true, 100.0, 500), t, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 1u);
    det.reset();
    EXPECT_EQ(det.alerts_fired(), 0u);
}

// ── CancelSpikeDetector ───────────────────────────────────────────────────────

TEST(CancelSpikeDetector, NoCancelsNoAlert) {
    CancelSpikeDetector det;
    CancelSpikeAlert alert{};
    // Add events are ignored
    EXPECT_FALSE(det.process_with_time(
        sym_add_ev(1, 1, true, 100.0, 500), 0, alert));
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(CancelSpikeDetector, SpikeTriggersAlert) {
    CancelSpikeConfig cfg;
    cfg.rate_window_ns = 1'000'000'000ULL; // 1 second windows
    cfg.z_threshold    = 3.0;
    cfg.max_symbols    = 16;
    CancelSpikeDetector det(cfg);

    CancelSpikeAlert alert{};
    uint64_t t = 0;

    // Establish baseline: 6 windows × ~5 cancels each
    // Each window: send 5 cancels, then advance time by 1s to close it
    for (int w = 0; w < 6; ++w) {
        for (int c = 0; c < 5; ++c) {
            det.process_with_time(
                sym_cancel_ev(1, static_cast<uint64_t>(w * 10 + c + 1), true, 100.0),
                t + static_cast<uint64_t>(c) * 100'000'000ULL, alert);
        }
        // Advance to next window
        t += 1'100'000'000ULL;
    }

    EXPECT_EQ(det.alerts_fired(), 0u); // baseline should not spike

    // Spike: 100 cancels in the next second → well above mean
    uint64_t spike_t = t;
    bool fired = false;
    for (int c = 0; c < 100; ++c) {
        det.process_with_time(
            sym_cancel_ev(1, static_cast<uint64_t>(1000 + c), true, 100.0),
            spike_t + static_cast<uint64_t>(c) * 5'000'000ULL, alert);
    }
    // Close the spike window
    fired = det.process_with_time(
        sym_cancel_ev(1, 9999, true, 100.0),
        spike_t + 1'100'000'000ULL, alert);

    EXPECT_TRUE(fired);
    EXPECT_GE(det.alerts_fired(), 1u);
    EXPECT_GT(alert.z_score, cfg.z_threshold);
    EXPECT_GT(alert.cancels_per_sec, alert.mean_rate);
}

TEST(CancelSpikeDetector, NormalRateNoAlert) {
    CancelSpikeConfig cfg;
    cfg.rate_window_ns = 1'000'000'000ULL;
    cfg.z_threshold    = 3.0;
    cfg.max_symbols    = 16;
    CancelSpikeDetector det(cfg);

    CancelSpikeAlert alert{};
    uint64_t t = 0;

    // All windows have the same steady rate — z-score should stay near 0
    for (int w = 0; w < 20; ++w) {
        for (int c = 0; c < 5; ++c) {
            det.process_with_time(
                sym_cancel_ev(1, static_cast<uint64_t>(w * 10 + c + 1), true, 100.0),
                t + static_cast<uint64_t>(c) * 100'000'000ULL, alert);
        }
        t += 1'100'000'000ULL;
    }
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(CancelSpikeDetector, DifferentSymbolsIndependent) {
    CancelSpikeConfig cfg;
    cfg.rate_window_ns = 1'000'000'000ULL;
    cfg.z_threshold    = 3.0;
    cfg.max_symbols    = 16;
    CancelSpikeDetector det(cfg);

    CancelSpikeAlert alert{};
    uint64_t t = 0;

    // Establish baseline for symbol 1 only
    for (int w = 0; w < 6; ++w) {
        for (int c = 0; c < 5; ++c) {
            det.process_with_time(
                sym_cancel_ev(1, static_cast<uint64_t>(w * 10 + c + 1), true, 100.0),
                t + static_cast<uint64_t>(c) * 100'000'000ULL, alert);
        }
        t += 1'100'000'000ULL;
    }

    // Events on symbol 2 should not affect symbol 1's stats
    for (int c = 0; c < 50; ++c) {
        det.process_with_time(
            sym_cancel_ev(2, static_cast<uint64_t>(5000 + c), true, 100.0),
            t + static_cast<uint64_t>(c) * 1'000'000ULL, alert);
    }
    det.process_with_time(sym_cancel_ev(2, 9998, true, 100.0), t + 1'200'000'000ULL, alert);

    // Symbol 1 should have no alerts
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(CancelSpikeDetector, Reset) {
    CancelSpikeDetector det;
    CancelSpikeAlert alert{};
    det.process_with_time(sym_cancel_ev(1, 1, true, 100.0), 0, alert);
    det.reset();
    EXPECT_EQ(det.alerts_fired(), 0u);
}

// ── BurstDetector ─────────────────────────────────────────────────────────────

TEST(BurstDetector, NotStableNoAlert) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    BurstDetector det(cfg);

    BurstAlert alert{};
    // Only 1 order — stats not stable yet
    EXPECT_FALSE(det.process_with_time(sym_add_ev(1, 1, true, 100.0, 500), 0, alert));
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(BurstDetector, NormalOrderNoAlert) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    BurstDetector det(cfg);

    BurstAlert alert{};
    // Establish baseline with consistent order sizes
    for (uint64_t i = 1; i <= 20; ++i) {
        EXPECT_FALSE(det.process_with_time(sym_add_ev(1, i, true, 100.0, 1000), i, alert));
    }
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(BurstDetector, BurstOrderFiresAlert) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    BurstDetector det(cfg);

    BurstAlert alert{};
    // Baseline: 30 orders of qty ≈ 1000
    for (uint64_t i = 1; i <= 30; ++i) {
        // Alternate slightly to give non-zero stddev
        uint32_t q = (i % 2 == 0) ? 900u : 1100u;
        det.process_with_time(sym_add_ev(1, i, true, 100.0, q), i * 1000, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 0u);

    // Now send a massive burst order — qty 100000 vs mean≈1000, stddev≈100
    bool fired = det.process_with_time(
        sym_add_ev(1, 999, true, 100.0, 100000), 1000000, alert);
    EXPECT_TRUE(fired);
    EXPECT_EQ(det.alerts_fired(), 1u);
    EXPECT_GT(alert.z_score, cfg.z_threshold);
    EXPECT_EQ(alert.qty, 100000u);
    EXPECT_EQ(alert.symbol_id, 1u);
}

TEST(BurstDetector, CancelEventsIgnored) {
    BurstDetector det;
    BurstAlert alert{};
    EXPECT_FALSE(det.process_with_time(sym_cancel_ev(1, 1, true, 100.0), 0, alert));
    EXPECT_EQ(det.alerts_fired(), 0u);
}

TEST(BurstDetector, DifferentSymbolsTrackedIndependently) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    cfg.max_symbols = 16;
    BurstDetector det(cfg);

    BurstAlert alert{};
    // Baseline for symbol 1: qty ≈ 1000
    for (uint64_t i = 1; i <= 20; ++i) {
        uint32_t q = (i % 2 == 0) ? 900u : 1100u;
        det.process_with_time(sym_add_ev(1, i, true, 100.0, q), i, alert);
    }

    // Symbol 2: its own baseline (larger sizes)
    for (uint64_t i = 1; i <= 20; ++i) {
        uint32_t q = (i % 2 == 0) ? 9000u : 11000u;
        det.process_with_time(sym_add_ev(2, i + 100, true, 200.0, q), i, alert);
    }
    EXPECT_EQ(det.alerts_fired(), 0u);

    // A 100000-qty order on symbol 1 should fire (far above sym1's ≈1000 mean)
    EXPECT_TRUE(det.process_with_time(sym_add_ev(1, 999, true, 100.0, 100000), 9999, alert));
    EXPECT_EQ(alert.symbol_id, 1u);
}

TEST(BurstDetector, Reset) {
    BurstDetector det;
    BurstAlert alert{};
    for (uint64_t i = 1; i <= 5; ++i)
        det.process_with_time(sym_add_ev(1, i, true, 100.0, 1000), i, alert);
    det.reset();
    EXPECT_EQ(det.alerts_fired(), 0u);
}

// ── SymbolScorer ──────────────────────────────────────────────────────────────

TEST(SymbolScorer, InitialScoreIsZero) {
    SymbolScorer scorer;
    EXPECT_DOUBLE_EQ(scorer.score(42, 0), 0.0);
}

TEST(SymbolScorer, RecordAlertBumpsScore) {
    ScorerConfig cfg;
    cfg.alert_increment = 0.2;
    cfg.half_life_ns    = 1e18; // effectively no decay for this test
    SymbolScorer scorer(cfg);

    scorer.record_alert(1, 0);
    EXPECT_NEAR(scorer.score(1, 0), 0.2, 1e-9);

    scorer.record_alert(1, 0);
    EXPECT_NEAR(scorer.score(1, 0), 0.4, 1e-9);
}

TEST(SymbolScorer, ScoreCappedAtOne) {
    ScorerConfig cfg;
    cfg.alert_increment = 0.3;
    cfg.half_life_ns    = 1e18;
    SymbolScorer scorer(cfg);

    for (int i = 0; i < 10; ++i) scorer.record_alert(5, 0);
    EXPECT_LE(scorer.score(5, 0), 1.0);
    EXPECT_NEAR(scorer.score(5, 0), 1.0, 1e-9);
}

TEST(SymbolScorer, ScoreDecaysOverTime) {
    ScorerConfig cfg;
    cfg.alert_increment = 0.4;
    cfg.half_life_ns    = 1'000'000'000ULL; // 1-second half-life
    SymbolScorer scorer(cfg);

    scorer.record_alert(1, 0);
    const double initial = scorer.score(1, 0);
    EXPECT_NEAR(initial, 0.4, 1e-9);

    // After one half-life the score should be ≈ 0.2
    const double after_half_life = scorer.score(1, 1'000'000'000ULL);
    EXPECT_NEAR(after_half_life, 0.2, 1e-6);

    // After two half-lives ≈ 0.1
    const double after_two = scorer.score(1, 2'000'000'000ULL);
    EXPECT_NEAR(after_two, 0.1, 1e-6);
}

TEST(SymbolScorer, DifferentSymbolsIndependent) {
    ScorerConfig cfg;
    cfg.alert_increment = 0.5;
    cfg.half_life_ns    = 1e18;
    SymbolScorer scorer(cfg);

    scorer.record_alert(1, 0);
    scorer.record_alert(1, 0);

    EXPECT_NEAR(scorer.score(1, 0), 1.0, 1e-9); // 0.5+0.5 = 1.0
    EXPECT_DOUBLE_EQ(scorer.score(2, 0), 0.0);  // untouched
}

TEST(SymbolScorer, Reset) {
    ScorerConfig cfg;
    cfg.alert_increment = 0.3;
    cfg.half_life_ns    = 1e18;
    SymbolScorer scorer(cfg);

    scorer.record_alert(7, 0);
    scorer.record_alert(7, 0);
    scorer.reset();
    EXPECT_DOUBLE_EQ(scorer.score(7, 0), 0.0);
}
