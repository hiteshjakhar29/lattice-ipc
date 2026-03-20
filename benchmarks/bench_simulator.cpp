// bench_simulator.cpp — throughput benchmark: 1M events through full SignalEngine pipeline.

#include "lattice/sim/market_simulator.hpp"
#include "lattice/signals/signal_engine.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/spoof_alert.hpp"

#include <benchmark/benchmark.h>
#include <vector>

using namespace lattice;
using namespace lattice::sim;
using namespace lattice::signals;
using namespace lattice::anomaly;

// ── SimulatorNext ─────────────────────────────────────────────────────────────
// Baseline: cost of generating events (no signal computation).

static void BM_SimulatorNext(benchmark::State& state) {
    SimConfig cfg;
    cfg.num_symbols = 4;
    MarketSimulator sim(cfg);

    for (auto _ : state) {
        benchmark::DoNotOptimize(sim.next());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_SimulatorNext);

// ── SimulatorWithSignalEngine ─────────────────────────────────────────────────
// Full pipeline: MarketSimulator → SignalEngine (per symbol).

static void BM_SimulatorWithSignalEngine(benchmark::State& state) {
    SimConfig cfg;
    cfg.num_symbols = 4;
    MarketSimulator sim(cfg);
    std::vector<SignalEngine> engines(cfg.num_symbols);

    for (auto _ : state) {
        const FeedEvent ev = sim.next();
        benchmark::DoNotOptimize(engines[ev.src_port].process(ev));
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_SimulatorWithSignalEngine);

// ── SimulatorFullPipeline ─────────────────────────────────────────────────────
// Full pipeline: MarketSimulator → SignalEngine → AnomalyDetector.

static void BM_SimulatorFullPipeline(benchmark::State& state) {
    SimConfig cfg;
    cfg.num_symbols = 4;
    MarketSimulator sim(cfg);
    std::vector<SignalEngine>    engines(cfg.num_symbols);
    std::vector<AnomalyDetector> detectors(cfg.num_symbols);
    SpoofAlert alert{};

    for (auto _ : state) {
        const FeedEvent ev = sim.next();
        const uint32_t  sym = ev.src_port;
        benchmark::DoNotOptimize(engines[sym].process(ev));
        benchmark::DoNotOptimize(detectors[sym].process(ev, alert));
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_SimulatorFullPipeline);

// ── OneMillion ────────────────────────────────────────────────────────────────
// Fixed iteration count: process exactly 1M events through the full pipeline.

static void BM_OneMillion(benchmark::State& state) {
    SimConfig cfg;
    cfg.num_symbols = 4;
    std::vector<SignalEngine>    engines(cfg.num_symbols);
    std::vector<AnomalyDetector> detectors(cfg.num_symbols);
    SpoofAlert alert{};

    for (auto _ : state) {
        MarketSimulator sim(cfg);
        engines.assign(cfg.num_symbols, SignalEngine{});
        detectors.assign(cfg.num_symbols, AnomalyDetector{});

        for (int i = 0; i < 1'000'000; ++i) {
            const FeedEvent ev = sim.next();
            const uint32_t sym = ev.src_port;
            benchmark::DoNotOptimize(engines[sym].process(ev));
            benchmark::DoNotOptimize(detectors[sym].process(ev, alert));
        }
    }
    state.SetItemsProcessed(
        static_cast<int64_t>(state.iterations()) * 1'000'000LL);
}
BENCHMARK(BM_OneMillion)->Iterations(3);

BENCHMARK_MAIN();
