// tests/test_stress.cpp — production stress tests.
//
// 1. ShmChannel serial integrity   — 1M events, sequence numbers preserved, 0 corruptions
// 2. ShmChannel concurrent SPSC    — 2M events, producer and consumer on separate threads
// 3. Full pipeline counter consistency — simulator → ShmChannel → SignalEngine + AnomalyDetector,
//    all written events accounted for at the end

#include "lattice/shm/shm_channel.hpp"
#include "lattice/feed_event.hpp"
#include "lattice/signals/signal_engine.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/spoof_alert.hpp"
#include "lattice/sim/market_simulator.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace lattice;
using namespace lattice::shm;
using namespace lattice::signals;
using namespace lattice::anomaly;
using namespace lattice::sim;

// ─────────────────────────────────────────────────────────────────────────────
// 1. Serial integrity — no corruption over 1M events
// ─────────────────────────────────────────────────────────────────────────────
// We write events carrying a monotonic sequence number in inject_ns, then drain
// and verify every slot arrived in order.  Because the ring can fill up we
// interleave writes and reads.

TEST(Stress, ShmChannelSerialIntegrity) {
    constexpr int N = 1'000'000;
    ShmChannel<FeedEvent, 4096> ch("/lattice_stress_serial");
    ASSERT_TRUE(ch.is_ready());

    auto& wr = ch.writer();
    auto& rd = ch.reader();

    uint64_t next_seq  = 0;   // next sequence number to write
    uint64_t expected  = 0;   // next sequence number to read
    uint64_t written   = 0;
    uint64_t received  = 0;
    uint64_t corruptions = 0;

    while (written < static_cast<uint64_t>(N)) {
        // Write one event
        FeedEvent ev{};
        ev.inject_ns = next_seq;
        if (wr.try_write(ev)) {
            ++next_seq;
            ++written;
        }
        // Drain up to 8 events to keep the ring from filling
        for (int d = 0; d < 8; ++d) {
            FeedEvent out{};
            if (!rd.try_read(out)) break;
            if (out.inject_ns != expected++) ++corruptions;
            ++received;
        }
    }
    // Final drain
    {
        FeedEvent out{};
        while (rd.try_read(out)) {
            if (out.inject_ns != expected++) ++corruptions;
            ++received;
        }
    }

    EXPECT_EQ(written,    static_cast<uint64_t>(N));
    EXPECT_EQ(received,   static_cast<uint64_t>(N));
    EXPECT_EQ(corruptions, 0u) << "Data corruptions detected in ShmChannel";
}

// ─────────────────────────────────────────────────────────────────────────────
// 2. Concurrent SPSC — producer and consumer on separate threads, 2M events
// ─────────────────────────────────────────────────────────────────────────────

TEST(Stress, ShmChannelConcurrentNoCorruption) {
    constexpr int N = 2'000'000;
    ShmChannel<FeedEvent, 4096> ch("/lattice_stress_concurrent");
    ASSERT_TRUE(ch.is_ready());

    auto& wr = ch.writer();
    auto& rd = ch.reader();

    std::atomic<uint64_t> corruption_count{0};

    std::thread producer([&wr] {
        for (int i = 0; i < N; ++i) {
            FeedEvent ev{};
            ev.inject_ns = static_cast<uint64_t>(i);
            while (!wr.try_write(ev)) { /* spin until space */ }
        }
    });

    std::thread consumer([&rd, &corruption_count] {
        uint64_t expected = 0;
        int received = 0;
        while (received < N) {
            FeedEvent ev{};
            if (rd.try_read(ev)) {
                if (ev.inject_ns != expected++)
                    corruption_count.fetch_add(1, std::memory_order_relaxed);
                ++received;
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(corruption_count.load(), 0u)
        << "SPSC sequence violations detected — memory ordering broken";
}

// ─────────────────────────────────────────────────────────────────────────────
// 3. Full pipeline counter consistency
//    MarketSimulator → ShmChannel → SignalEngine + AnomalyDetector
//    Invariant: every written event is read back and processed exactly once.
// ─────────────────────────────────────────────────────────────────────────────

TEST(Stress, FullPipelineNoLostEvents) {
    constexpr int N = 100'000;

    SimConfig scfg;
    scfg.num_symbols = 4;
    scfg.seed        = 0xDEADBEEF;
    MarketSimulator sim(scfg);

    // Use a large enough ring that we never need to spin in the write path
    // (8192 > typical burst-window depth).
    ShmChannel<FeedEvent, 8192> ch("/lattice_stress_pipeline");
    ASSERT_TRUE(ch.is_ready());

    auto& wr = ch.writer();
    auto& rd = ch.reader();

    std::vector<SignalEngine>    engines(scfg.num_symbols);
    std::vector<AnomalyDetector> detectors(scfg.num_symbols);
    SpoofAlert alert{};

    uint64_t events_written   = 0;
    uint64_t events_processed = 0;

    // Interleave: write one, then drain to keep the ring below capacity.
    for (int i = 0; i < N; ++i) {
        FeedEvent ev = sim.next();

        // Guaranteed write: drain first if ring is close to full
        while (!wr.try_write(ev)) {
            FeedEvent out{};
            if (rd.try_read(out)) {
                engines[out.src_port].process(out);
                detectors[out.src_port].process(out, alert);
                ++events_processed;
            }
        }
        ++events_written;

        // Opportunistic drain — keep latency low
        for (int d = 0; d < 4; ++d) {
            FeedEvent out{};
            if (!rd.try_read(out)) break;
            engines[out.src_port].process(out);
            detectors[out.src_port].process(out, alert);
            ++events_processed;
        }
    }

    // Final drain
    {
        FeedEvent out{};
        while (rd.try_read(out)) {
            engines[out.src_port].process(out);
            detectors[out.src_port].process(out, alert);
            ++events_processed;
        }
    }

    EXPECT_EQ(events_written, static_cast<uint64_t>(N));
    EXPECT_EQ(events_processed, events_written)
        << "Lost events detected: written=" << events_written
        << " processed=" << events_processed;

    // Simulator internal counter must agree with our loop count
    EXPECT_EQ(sim.total_generated(), static_cast<uint64_t>(N));

    // Sanity: adds + cancels + modifies should sum to total
    EXPECT_EQ(sim.adds_generated() + sim.cancels_generated() + sim.modifies_generated(),
              static_cast<uint64_t>(N));
}

// ─────────────────────────────────────────────────────────────────────────────
// 4. Ring wrap-around integrity — fill and drain the ring many times
// ─────────────────────────────────────────────────────────────────────────────

TEST(Stress, RingWrapAroundIntegrity) {
    // Capacity=8 so we can test wrap-around exhaustively
    ShmChannel<FeedEvent, 8> ch("/lattice_stress_wrap");
    ASSERT_TRUE(ch.is_ready());

    auto& wr = ch.writer();
    auto& rd = ch.reader();

    constexpr int ROUNDS = 50'000;
    uint64_t seq_write = 0;
    uint64_t seq_read  = 0;
    uint64_t corruptions = 0;

    for (int r = 0; r < ROUNDS; ++r) {
        // Fill the ring completely
        for (int i = 0; i < 8; ++i) {
            FeedEvent ev{};
            ev.inject_ns = seq_write++;
            ASSERT_TRUE(wr.try_write(ev)) << "Ring should accept 8 items";
        }
        // Ring is now full — next write must fail
        {
            FeedEvent ev{};
            ev.inject_ns = 0xDEAD;
            EXPECT_FALSE(wr.try_write(ev)) << "Ring must be full after 8 writes";
        }
        // Drain all 8
        for (int i = 0; i < 8; ++i) {
            FeedEvent out{};
            ASSERT_TRUE(rd.try_read(out)) << "Ring should have 8 items";
            if (out.inject_ns != seq_read++) ++corruptions;
        }
        // Empty — next read must fail
        {
            FeedEvent out{};
            EXPECT_FALSE(rd.try_read(out)) << "Ring must be empty after draining";
        }
    }

    EXPECT_EQ(corruptions, 0u) << "Sequence corruption detected on ring wrap-around";
    EXPECT_EQ(seq_write, static_cast<uint64_t>(ROUNDS) * 8);
    EXPECT_EQ(seq_read,  static_cast<uint64_t>(ROUNDS) * 8);
}
