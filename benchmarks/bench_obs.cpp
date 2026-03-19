#include "lattice/obs/latency_histogram.hpp"
#include "lattice/obs/queue_depth_tracker.hpp"
#include "lattice/obs/pipeline_stats.hpp"

#include <benchmark/benchmark.h>
#include <cstdint>

using namespace lattice::obs;

// ── LatencyHistogram ──────────────────────────────────────────────────────────

static void BM_LatencyHistogram_Record(benchmark::State& state) {
    LatencyHistogram h;
    uint64_t ns = 1;
    for (auto _ : state) {
        h.record(ns);
        ns = (ns * 6364136223846793005ULL + 1442695040888963407ULL) % 2'000'000;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_LatencyHistogram_Record);

static void BM_LatencyHistogram_P99(benchmark::State& state) {
    LatencyHistogram h;
    for (int i = 0; i < 10000; ++i) h.record(static_cast<uint64_t>(i * 100));
    for (auto _ : state) {
        benchmark::DoNotOptimize(h.p99());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_LatencyHistogram_P99);

static void BM_LatencyHistogram_P999(benchmark::State& state) {
    LatencyHistogram h;
    for (int i = 0; i < 10000; ++i) h.record(static_cast<uint64_t>(i * 100));
    for (auto _ : state) {
        benchmark::DoNotOptimize(h.p999());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_LatencyHistogram_P999);

// ── QueueDepthTracker ─────────────────────────────────────────────────────────

static void BM_QueueDepthTracker_Record(benchmark::State& state) {
    QueueDepthTracker t;
    t.set_capacity(1024);
    std::size_t depth = 0;
    for (auto _ : state) {
        t.record(depth);
        depth = (depth + 1) & 1023;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_QueueDepthTracker_Record);

static void BM_QueueDepthTracker_MeanQuery(benchmark::State& state) {
    QueueDepthTracker t;
    for (std::size_t i = 0; i < 1000; ++i) t.record(i);
    for (auto _ : state) {
        benchmark::DoNotOptimize(t.mean_depth());
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_QueueDepthTracker_MeanQuery);

// ── PipelineStats ─────────────────────────────────────────────────────────────

static void BM_PipelineStats_IncrementCounters(benchmark::State& state) {
    PipelineStats s;
    for (auto _ : state) {
        s.packets_received.fetch_add(1, std::memory_order_relaxed);
        s.packets_processed.fetch_add(1, std::memory_order_relaxed);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_PipelineStats_IncrementCounters);

static void BM_PipelineStats_RecordWriteLatency(benchmark::State& state) {
    PipelineStats s;
    uint64_t ns = 500;
    for (auto _ : state) {
        s.shm_write_latency.record(ns);
        ns = (ns < 2'000'000) ? ns + 100 : 100;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_PipelineStats_RecordWriteLatency);

static void BM_PipelineStats_RecordRingDepth(benchmark::State& state) {
    PipelineStats s;
    s.shm_ring_depth.set_capacity(65536);
    std::size_t depth = 0;
    for (auto _ : state) {
        s.shm_ring_depth.record(depth);
        depth = (depth + 1) & 65535;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_PipelineStats_RecordRingDepth);
