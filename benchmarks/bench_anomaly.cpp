#include <benchmark/benchmark.h>

#include "lattice/feed_event.hpp"
#include "lattice/signals/feed_decoder.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/welford_stats.hpp"
#include "lattice/anomaly/layering_detector.hpp"
#include "lattice/anomaly/layering_alert.hpp"
#include "lattice/anomaly/cancel_spike_detector.hpp"
#include "lattice/anomaly/cancel_spike_alert.hpp"
#include "lattice/anomaly/burst_detector.hpp"
#include "lattice/anomaly/burst_alert.hpp"
#include "lattice/anomaly/symbol_scorer.hpp"

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

static void BM_LayeringDetector_AddLargeOrder(benchmark::State& state) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.max_symbols     = 64;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        // Alternate symbols to keep counts below threshold (no alert overhead)
        uint16_t sym = static_cast<uint16_t>((oid % 16) + 1);
        benchmark::DoNotOptimize(
            det.process_with_time(sym_add_ev(sym, oid, true, 100.0, 500), t, alert));
        ++oid;
        t += 100'000'000ULL; // advance time so window expires between clusters
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LayeringDetector_AddLargeOrder);

static void BM_LayeringDetector_AlertPath(benchmark::State& state) {
    LayeringConfig cfg;
    cfg.qty_threshold   = 100;
    cfg.count_threshold = 3;
    cfg.window_ns       = 500'000'000'000ULL; // huge window — never expires
    cfg.max_symbols     = 64;
    LayeringDetector det(cfg);

    LayeringAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        // 4 consecutive adds on same symbol/side → fires, then resets count
        for (int i = 0; i < 4; ++i) {
            det.process_with_time(sym_add_ev(1, oid++, true, 100.0, 500), t, alert);
            t += 1;
        }
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_LayeringDetector_AlertPath);

// ── CancelSpikeDetector ───────────────────────────────────────────────────────

static void BM_CancelSpikeDetector_WithinWindow(benchmark::State& state) {
    CancelSpikeConfig cfg;
    cfg.rate_window_ns = 1'000'000'000ULL;
    cfg.max_symbols    = 64;
    CancelSpikeDetector det(cfg);

    CancelSpikeAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        // Cancels within same window (no bucket close, no WelfordStats update)
        benchmark::DoNotOptimize(
            det.process_with_time(sym_cancel_ev(1, oid++, true, 100.0), t, alert));
        t += 1'000'000ULL; // 1 ms steps — stays in same 1 s window
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CancelSpikeDetector_WithinWindow);

static void BM_CancelSpikeDetector_WindowClose(benchmark::State& state) {
    CancelSpikeConfig cfg;
    cfg.rate_window_ns = 1'000'000'000ULL;
    cfg.max_symbols    = 64;
    CancelSpikeDetector det(cfg);

    CancelSpikeAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        // Each cancel closes the previous window (t advances by > 1 s each time)
        benchmark::DoNotOptimize(
            det.process_with_time(sym_cancel_ev(1, oid++, true, 100.0), t, alert));
        t += 1'100'000'000ULL;
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CancelSpikeDetector_WindowClose);

// ── BurstDetector ─────────────────────────────────────────────────────────────

static void BM_BurstDetector_NormalOrder(benchmark::State& state) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    cfg.max_symbols = 64;
    BurstDetector det(cfg);

    BurstAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        uint32_t qty = static_cast<uint32_t>(1000 + (oid % 3));
        benchmark::DoNotOptimize(
            det.process_with_time(sym_add_ev(1, oid++, true, 100.0, qty), t++, alert));
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BurstDetector_NormalOrder);

static void BM_BurstDetector_MultiSymbol(benchmark::State& state) {
    BurstConfig cfg;
    cfg.z_threshold = 4.0;
    cfg.max_symbols = 64;
    BurstDetector det(cfg);

    BurstAlert alert{};
    uint64_t oid = 1;
    uint64_t t   = 0;
    for (auto _ : state) {
        uint16_t sym = static_cast<uint16_t>((oid % 16) + 1);
        uint32_t qty = static_cast<uint32_t>(1000 + (oid % 5));
        benchmark::DoNotOptimize(
            det.process_with_time(sym_add_ev(sym, oid++, true, 100.0, qty), t++, alert));
        benchmark::DoNotOptimize(alert);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_BurstDetector_MultiSymbol);

// ── SymbolScorer ──────────────────────────────────────────────────────────────

static void BM_SymbolScorer_RecordAlert(benchmark::State& state) {
    ScorerConfig cfg;
    cfg.max_symbols     = 256;
    cfg.half_life_ns    = 5'000'000'000ULL;
    cfg.alert_increment = 0.1;
    SymbolScorer scorer(cfg);

    uint64_t t = 0;
    for (auto _ : state) {
        uint16_t sym = static_cast<uint16_t>(t % 16);
        scorer.record_alert(sym, t);
        benchmark::DoNotOptimize(scorer.score(sym, t));
        t += 1'000'000ULL;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SymbolScorer_RecordAlert);

static void BM_SymbolScorer_ScoreQuery(benchmark::State& state) {
    ScorerConfig cfg;
    cfg.max_symbols     = 256;
    cfg.half_life_ns    = 5'000'000'000ULL;
    cfg.alert_increment = 0.2;
    SymbolScorer scorer(cfg);

    // Pre-populate a few symbols
    for (uint16_t s = 1; s <= 16; ++s) scorer.record_alert(s, 0);

    uint64_t t = 1'000'000'000ULL;
    for (auto _ : state) {
        uint16_t sym = static_cast<uint16_t>((t / 1'000'000ULL) % 16 + 1);
        benchmark::DoNotOptimize(scorer.score(sym, t));
        t += 1'000'000ULL;
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_SymbolScorer_ScoreQuery);
