#include <benchmark/benchmark.h>

#include "lattice/feed_event.hpp"
#include "lattice/shm/shm_channel.hpp"

#include <atomic>
#include <cstring>
#include <thread>

using namespace lattice;
using namespace lattice::shm;

static constexpr std::size_t kCap = 65536;
using Channel = ShmChannel<FeedEvent, kCap>;

static FeedEvent make_event(uint64_t seq) noexcept {
    FeedEvent ev{};
    ev.inject_ns   = seq;
    ev.payload_len = 8;
    return ev;
}

// ── Single-threaded: write into an empty ring ──────────────────────────────────

static void BM_ShmWriter_TryWrite(benchmark::State& state) {
    Channel ch("/lattice_bench_write");
    FeedEvent ev = make_event(1);
    FeedEvent out{};
    for (auto _ : state) {
        ch.writer().try_write(ev);
        ch.reader().try_read(out); // drain so ring stays empty
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ShmWriter_TryWrite);

// ── Single-threaded: read from a pre-filled ring ───────────────────────────────

static void BM_ShmReader_TryRead(benchmark::State& state) {
    Channel ch("/lattice_bench_read");
    // Pre-fill one slot, re-fill after each drain
    ch.writer().try_write(make_event(1));
    FeedEvent out{};
    for (auto _ : state) {
        ch.reader().try_read(out);
        ch.writer().try_write(make_event(2)); // keep one available
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ShmReader_TryRead);

// ── Single-threaded round-trip ─────────────────────────────────────────────────

static void BM_ShmRoundTrip(benchmark::State& state) {
    Channel ch("/lattice_bench_rt");
    FeedEvent ev  = make_event(42);
    FeedEvent out{};
    for (auto _ : state) {
        ch.writer().try_write(ev);
        ch.reader().try_read(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(sizeof(FeedEvent)));
}
BENCHMARK(BM_ShmRoundTrip);

// ── Batch fill then drain ──────────────────────────────────────────────────────

static void BM_ShmBatchFill(benchmark::State& state) {
    static constexpr int kBatch = 512;
    Channel ch("/lattice_bench_batch");
    FeedEvent ev  = make_event(0);
    FeedEvent out{};
    for (auto _ : state) {
        for (int i = 0; i < kBatch; ++i) ch.writer().try_write(ev);
        for (int i = 0; i < kBatch; ++i) ch.reader().try_read(out);
        benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * kBatch);
}
BENCHMARK(BM_ShmBatchFill);

// ── Multi-threaded SPSC throughput ─────────────────────────────────────────────

static void BM_ShmSpscThroughput(benchmark::State& state) {
    static constexpr std::size_t kBatch = 1024;
    Channel ch("/lattice_bench_throughput");

    std::atomic<bool> running{true};
    std::atomic<int64_t> consumed{0};

    std::thread consumer([&] {
        FeedEvent out{};
        while (running.load(std::memory_order_relaxed)) {
            if (ch.reader().try_read(out)) {
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
        while (ch.reader().try_read(out)) {
            consumed.fetch_add(1, std::memory_order_relaxed);
        }
    });

    for (auto _ : state) {
        for (std::size_t i = 0; i < kBatch; ++i) {
            while (!ch.writer().try_write(make_event(i))) {}
        }
    }

    running.store(false, std::memory_order_relaxed);
    consumer.join();

    const int64_t total =
        static_cast<int64_t>(state.iterations()) * static_cast<int64_t>(kBatch);
    state.SetItemsProcessed(total);
    state.SetBytesProcessed(total * static_cast<int64_t>(sizeof(FeedEvent)));
}
BENCHMARK(BM_ShmSpscThroughput)->UseRealTime();
