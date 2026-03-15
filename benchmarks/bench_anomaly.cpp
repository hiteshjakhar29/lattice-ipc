#include <benchmark/benchmark.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/welford_stats.hpp"

using namespace lattice;
using namespace lattice::signals;
using namespace lattice::anomaly;

static FeedEvent add_ev(uint64_t oid, double price, uint32_t qty) {
    return make_feed_event<FeedEvent>(EventType::Add, true, oid, price, qty);
}
static FeedEvent cancel_ev(uint64_t oid, double price) {
    return make_feed_event<FeedEvent>(EventType::Cancel, true, oid, price, 0);
}

// ── WelfordStats ───────────────────────────────────────────────────────────────

static void BM_WelfordStats_Update(benchmark::State& state) {
    WelfordStats ws;
    double v = 1000.0;
    for (auto _ : state) {
        ws.update(v);
        benchmark::DoNotOptimize(ws.mean());
        v += 1.0;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_WelfordStats_Update);

// ── AnomalyDetector ────────────────────────────────────────────────────────────

static void BM_AnomalyDetector_AddLargeOrder(benchmark::State& state) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.max_tracked_orders = 4096;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    uint64_t oid = 1;
    for (auto _ : state) {
        // Insert then remove to keep table from filling
        det.process(add_ev(oid, 100.0, 500), alert);
        det.process(cancel_ev(oid, 100.0), alert);
        ++oid;
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AnomalyDetector_AddLargeOrder);

static void BM_AnomalyDetector_AddSmallOrder(benchmark::State& state) {
    AnomalyConfig cfg;
    cfg.qty_threshold = 10000; // high threshold — all orders below it
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    uint64_t oid = 1;
    FeedEvent ev = add_ev(1, 100.0, 50); // qty < threshold
    for (auto _ : state) {
        benchmark::DoNotOptimize(det.process(ev, alert));
        ++oid;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AnomalyDetector_AddSmallOrder);

static void BM_AnomalyDetector_CancelTracked(benchmark::State& state) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.max_tracked_orders = 4096;
    cfg.z_score_threshold  = -100.0; // very permissive — alert almost never fires
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;

    for (auto _ : state) {
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        benchmark::DoNotOptimize(
            det.process_with_time(cancel_ev(oid, 100.0), t + 1'000'000ULL, alert));
        ++oid;
        t += 2'000'000ULL;
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AnomalyDetector_CancelTracked);

static void BM_AnomalyDetector_CancelUntrackedMiss(benchmark::State& state) {
    AnomalyConfig cfg;
    cfg.max_tracked_orders = 4096;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    uint64_t oid = 1;
    for (auto _ : state) {
        // Cancel of an order that was never added — pure hash table miss
        benchmark::DoNotOptimize(det.process(cancel_ev(oid++, 100.0), alert));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AnomalyDetector_CancelUntrackedMiss);

static void BM_AnomalyDetector_MixedSequence(benchmark::State& state) {
    AnomalyConfig cfg;
    cfg.qty_threshold      = 100;
    cfg.max_tracked_orders = 4096;
    AnomalyDetector det(cfg);

    SpoofAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        det.process_with_time(add_ev(oid, 100.0, 500), t, alert);
        benchmark::DoNotOptimize(
            det.process_with_time(cancel_ev(oid, 100.0), t + 500'000ULL, alert));
        ++oid;
        t += 1'000'000ULL;
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AnomalyDetector_MixedSequence);
