#include <gtest/gtest.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/pending_order.hpp"
#include "lattice/anomaly/spoof_alert.hpp"
#include "lattice/anomaly/welford_stats.hpp"

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
