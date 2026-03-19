#include "lattice/obs/latency_histogram.hpp"
#include "lattice/obs/queue_depth_tracker.hpp"
#include "lattice/obs/pipeline_stats.hpp"
#include "lattice/shm/shm_channel.hpp"
#include "lattice/feed_event.hpp"

#include <gtest/gtest.h>
#include <cstdint>

using namespace lattice::obs;
using namespace lattice::shm;
using lattice::FeedEvent;

// ── LatencyHistogram ──────────────────────────────────────────────────────────

TEST(LatencyHistogram, InitialState) {
    LatencyHistogram h;
    EXPECT_EQ(h.total(), 0u);
    EXPECT_EQ(h.p50(),   0u);
    EXPECT_EQ(h.p99(),   0u);
    EXPECT_EQ(h.p999(),  0u);
}

TEST(LatencyHistogram, RecordPlacesInCorrectBucket) {
    LatencyHistogram h;
    h.record(500);           // <1 µs  → bucket 0
    EXPECT_EQ(h.buckets[0].load(), 1u);
    for (int i = 1; i < LatencyHistogram::kBuckets; ++i)
        EXPECT_EQ(h.buckets[i].load(), 0u);
}

TEST(LatencyHistogram, AllBucketBoundaries) {
    LatencyHistogram h;
    // One sample just below each boundary, plus one in the last bucket
    h.record(999);           // <1 µs
    h.record(1'000);         // 1–5 µs (== lower bound of bucket 1)
    h.record(4'999);         // 1–5 µs
    h.record(5'000);         // 5–10 µs
    h.record(9'999);         // 5–10 µs
    h.record(10'000);        // 10–50 µs
    h.record(49'999);        // 10–50 µs
    h.record(50'000);        // 50–100 µs
    h.record(99'999);        // 50–100 µs
    h.record(100'000);       // 100–500 µs
    h.record(499'999);       // 100–500 µs
    h.record(500'000);       // 500 µs–1 ms
    h.record(999'999);       // 500 µs–1 ms
    h.record(1'000'000);     // >1 ms
    EXPECT_EQ(h.total(), 14u);
    EXPECT_EQ(h.buckets[0].load(), 1u);
    EXPECT_EQ(h.buckets[1].load(), 2u);
    EXPECT_EQ(h.buckets[2].load(), 2u);
    EXPECT_EQ(h.buckets[3].load(), 2u);
    EXPECT_EQ(h.buckets[4].load(), 2u);
    EXPECT_EQ(h.buckets[5].load(), 2u);
    EXPECT_EQ(h.buckets[6].load(), 2u);
    EXPECT_EQ(h.buckets[7].load(), 1u);
}

TEST(LatencyHistogram, P50SingleBucket) {
    LatencyHistogram h;
    // All samples in the <1 µs bucket
    h.record(100);
    h.record(200);
    h.record(300);
    // p50 == upper bound of bucket 0 == 1000
    EXPECT_EQ(h.p50(), 1'000u);
}

TEST(LatencyHistogram, P99HeavyTailBucket) {
    LatencyHistogram h;
    // 99 samples in bucket 0, 1 sample in last bucket
    for (int i = 0; i < 99; ++i) h.record(500);
    h.record(2'000'000);
    // p99 should hit the last bucket
    EXPECT_EQ(h.p99(), LatencyHistogram::kBounds[7]);
}

TEST(LatencyHistogram, Reset) {
    LatencyHistogram h;
    h.record(1'000);
    h.record(50'000);
    h.reset();
    EXPECT_EQ(h.total(), 0u);
    EXPECT_EQ(h.p50(),   0u);
}

TEST(LatencyHistogram, P999HighPercentile) {
    LatencyHistogram h;
    // 999 fast, 1 slow
    for (int i = 0; i < 999; ++i) h.record(500);
    h.record(5'000'000);
    EXPECT_EQ(h.p999(), LatencyHistogram::kBounds[7]);
}

// ── QueueDepthTracker ─────────────────────────────────────────────────────────

TEST(QueueDepthTracker, InitialState) {
    QueueDepthTracker t;
    EXPECT_EQ(t.current(),   0u);
    EXPECT_EQ(t.min_depth(), 0u);   // sentinel -> 0 before first sample
    EXPECT_EQ(t.max_depth(), 0u);
    EXPECT_DOUBLE_EQ(t.mean_depth(), 0.0);
    EXPECT_EQ(t.overflows(), 0u);
}

TEST(QueueDepthTracker, RecordUpdatesAll) {
    QueueDepthTracker t;
    t.record(10);
    EXPECT_EQ(t.current(),   10u);
    EXPECT_EQ(t.min_depth(), 10u);
    EXPECT_EQ(t.max_depth(), 10u);
    EXPECT_DOUBLE_EQ(t.mean_depth(), 10.0);
}

TEST(QueueDepthTracker, MinMaxTracked) {
    QueueDepthTracker t;
    t.record(5);
    t.record(15);
    t.record(10);
    EXPECT_EQ(t.min_depth(), 5u);
    EXPECT_EQ(t.max_depth(), 15u);
    EXPECT_EQ(t.current(),   10u);
}

TEST(QueueDepthTracker, MeanComputed) {
    QueueDepthTracker t;
    t.record(0);
    t.record(10);
    t.record(20);
    EXPECT_DOUBLE_EQ(t.mean_depth(), 10.0);
}

TEST(QueueDepthTracker, OverflowCountedWhenCapacitySet) {
    QueueDepthTracker t;
    t.set_capacity(16);
    t.record(8);   // below capacity
    t.record(16);  // == capacity → overflow
    t.record(20);  // > capacity → overflow
    EXPECT_EQ(t.overflows(), 2u);
}

TEST(QueueDepthTracker, NoOverflowWhenCapacityZero) {
    QueueDepthTracker t;
    t.record(9999);
    EXPECT_EQ(t.overflows(), 0u);
}

TEST(QueueDepthTracker, Reset) {
    QueueDepthTracker t;
    t.set_capacity(8);
    t.record(4);
    t.record(10);
    t.reset();
    EXPECT_EQ(t.current(),   0u);
    EXPECT_EQ(t.min_depth(), 0u);
    EXPECT_EQ(t.max_depth(), 0u);
    EXPECT_DOUBLE_EQ(t.mean_depth(), 0.0);
    EXPECT_EQ(t.overflows(), 0u);
}

// ── PipelineStats ─────────────────────────────────────────────────────────────

TEST(PipelineStats, InitialCountersAreZero) {
    PipelineStats s;
    EXPECT_EQ(s.packets_received.load(),    0u);
    EXPECT_EQ(s.packets_dropped.load(),     0u);
    EXPECT_EQ(s.packets_processed.load(),   0u);
    EXPECT_EQ(s.signal_computations.load(), 0u);
    EXPECT_EQ(s.anomaly_alerts_fired.load(),0u);
}

TEST(PipelineStats, IncrementCounters) {
    PipelineStats s;
    s.packets_received.fetch_add(10);
    s.packets_processed.fetch_add(9);
    s.packets_dropped.fetch_add(1);
    EXPECT_EQ(s.packets_received.load(),  10u);
    EXPECT_EQ(s.packets_processed.load(),  9u);
    EXPECT_EQ(s.packets_dropped.load(),    1u);
}

TEST(PipelineStats, HistogramAndDepthAccessible) {
    PipelineStats s;
    s.shm_write_latency.record(800);
    s.signal_compute_latency.record(15'000);
    s.anomaly_check_latency.record(3'000);
    s.shm_ring_depth.record(4);

    EXPECT_EQ(s.shm_write_latency.total(),      1u);
    EXPECT_EQ(s.signal_compute_latency.total(),  1u);
    EXPECT_EQ(s.anomaly_check_latency.total(),   1u);
    EXPECT_EQ(s.shm_ring_depth.current(),        4u);
}

TEST(PipelineStats, Reset) {
    PipelineStats s;
    s.packets_received.fetch_add(5);
    s.shm_write_latency.record(1'000);
    s.shm_ring_depth.record(8);
    s.reset();
    EXPECT_EQ(s.packets_received.load(),     0u);
    EXPECT_EQ(s.shm_write_latency.total(),   0u);
    EXPECT_EQ(s.shm_ring_depth.current(),    0u);
}

TEST(PipelineStats, PrintReportDoesNotCrash) {
    PipelineStats s;
    s.packets_received.fetch_add(1000);
    s.packets_processed.fetch_add(990);
    s.packets_dropped.fetch_add(10);
    s.signal_computations.fetch_add(500);
    s.anomaly_alerts_fired.fetch_add(3);
    s.shm_write_latency.record(800);
    s.shm_write_latency.record(3'000);
    s.shm_ring_depth.record(16);
    EXPECT_NO_THROW(s.print_report());
}

// ── ShmWriter/ShmReader integration ──────────────────────────────────────────

static std::string obs_shm_name() {
    static int counter = 0;
    return "/test_obs_shm_" + std::to_string(++counter);
}

TEST(PipelineStats, ShmWriterIncrementsCounters) {
    using Chan = ShmChannel<FeedEvent, 64>;
    Chan ch(obs_shm_name());
    ASSERT_TRUE(ch.is_ready());

    PipelineStats stats;
    ch.writer().set_stats(&stats);

    FeedEvent ev{};
    for (int i = 0; i < 10; ++i) (void)ch.writer().try_write(ev);

    EXPECT_EQ(stats.packets_received.load(),  10u);
    EXPECT_EQ(stats.packets_processed.load(), 10u);
    EXPECT_EQ(stats.packets_dropped.load(),    0u);
}

TEST(PipelineStats, ShmWriterCountsDropsWhenFull) {
    using Chan = ShmChannel<FeedEvent, 4>;
    Chan ch(obs_shm_name());
    ASSERT_TRUE(ch.is_ready());

    PipelineStats stats;
    ch.writer().set_stats(&stats);

    FeedEvent ev{};
    // Ring capacity = 4; write 6 — 4 succeed, 2 dropped
    for (int i = 0; i < 6; ++i) (void)ch.writer().try_write(ev);

    EXPECT_EQ(stats.packets_received.load(),  6u);
    EXPECT_EQ(stats.packets_processed.load(), 4u);
    EXPECT_EQ(stats.packets_dropped.load(),   2u);
}

TEST(PipelineStats, ShmReaderUpdatesQueueDepth) {
    using Chan = ShmChannel<FeedEvent, 64>;
    Chan ch(obs_shm_name());
    ASSERT_TRUE(ch.is_ready());

    PipelineStats stats;
    ch.reader().set_stats(&stats);

    // Write 4 items, then read them all
    FeedEvent ev{};
    for (int i = 0; i < 4; ++i) (void)ch.writer().try_write(ev);

    FeedEvent out{};
    for (int i = 0; i < 4; ++i) (void)ch.reader().try_read(out);

    // After last read depth == 0; max depth should be 3 (4-1, 4-2, 4-3, 4-4)
    EXPECT_EQ(stats.shm_ring_depth.current(),   0u);
    EXPECT_EQ(stats.shm_ring_depth.max_depth(),  3u);
}
