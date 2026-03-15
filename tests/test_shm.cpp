#include <gtest/gtest.h>

#include "lattice/feed_event.hpp"
#include "lattice/shm/shm_channel.hpp"
#include "lattice/shm/shm_layout.hpp"
#include "lattice/shm/shm_reader.hpp"
#include "lattice/shm/shm_writer.hpp"

#include <atomic>
#include <cstring>
#include <thread>

using namespace lattice;
using namespace lattice::shm;

// ── Helpers ────────────────────────────────────────────────────────────────────

static constexpr std::size_t kCap = 64;
using Layout  = ShmLayout<FeedEvent, kCap>;
using Writer  = ShmWriter<FeedEvent, kCap>;
using Reader  = ShmReader<FeedEvent, kCap>;
using Channel = ShmChannel<FeedEvent, kCap>;

// Use a unique name per test to avoid cross-test pollution.
static int g_test_counter = 0;
static std::string shm_name() {
    return "/lattice_test_" + std::to_string(++g_test_counter);
}

static FeedEvent make_event(uint64_t seq) {
    FeedEvent ev{};
    ev.inject_ns  = seq;
    ev.receive_ns = seq + 1000;
    ev.payload_len = 8;
    std::memcpy(ev.payload, &seq, sizeof(seq));
    return ev;
}

// ── Layout tests ───────────────────────────────────────────────────────────────

TEST(ShmLayout, MagicAndVersionConstants) {
    EXPECT_EQ(Layout::kMagic,   0x4C41545449434500ULL);
    EXPECT_EQ(Layout::kVersion, 1u);
    EXPECT_EQ(Layout::kMask,    kCap - 1);
}

TEST(ShmLayout, HeaderIs64Bytes) {
    EXPECT_EQ(sizeof(Layout::Header), 64u);
}

TEST(ShmLayout, WriteIdxOnSeparateCacheLine) {
    Layout l{};
    const auto hdr_end = reinterpret_cast<uintptr_t>(&l) + sizeof(Layout::Header);
    const auto widx    = reinterpret_cast<uintptr_t>(&l.write_idx);
    EXPECT_GE(widx, hdr_end);
    EXPECT_EQ(widx % 64, 0u); // cache-line aligned
}

TEST(ShmLayout, ReadIdxOnSeparateCacheLine) {
    Layout l{};
    const auto widx = reinterpret_cast<uintptr_t>(&l.write_idx);
    const auto ridx = reinterpret_cast<uintptr_t>(&l.read_idx);
    EXPECT_GE(ridx - widx, 64u); // at least one cache line apart
    EXPECT_EQ(ridx % 64, 0u);
}

// ── ShmWriter tests ────────────────────────────────────────────────────────────

TEST(ShmWriter, OpensAndInitsHeader) {
    const auto name = shm_name();
    Writer w(name);
    ASSERT_TRUE(w.is_open()) << to_string(w.last_error());

    // Map ourselves to inspect header
    Reader r(name);
    ASSERT_TRUE(r.is_attached()) << to_string(r.last_error());
}

TEST(ShmWriter, TryWriteSucceeds) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    EXPECT_TRUE(ch.writer().try_write(make_event(1)));
}

TEST(ShmWriter, TryWriteFullRingReturnsFalse) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    // Fill all N slots
    for (std::size_t i = 0; i < kCap; ++i) {
        EXPECT_TRUE(ch.writer().try_write(make_event(i))) << "slot " << i;
    }
    EXPECT_FALSE(ch.writer().try_write(make_event(999)));
}

// ── ShmReader tests ────────────────────────────────────────────────────────────

TEST(ShmReader, TryReadEmptyReturnsFalse) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    FeedEvent out{};
    EXPECT_FALSE(ch.reader().try_read(out));
}

TEST(ShmReader, BadMagicReturnsError) {
    const auto name = shm_name();
    {
        Writer w(name); // create and immediately destroy (shm_unlink in dtor)
    }
    // shm no longer exists — open should fail
    Reader r(name);
    EXPECT_FALSE(r.is_attached());
    EXPECT_NE(r.last_error(), ShmError::None);
}

// ── Round-trip tests ───────────────────────────────────────────────────────────

TEST(ShmChannel, SpscRoundTrip) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    constexpr int kN = 500;
    for (int i = 0; i < kN; ++i) {
        ch.writer().write_blocking(make_event(static_cast<uint64_t>(i)));
        FeedEvent out{};
        ASSERT_TRUE(ch.reader().try_read(out)) << "item " << i;
        EXPECT_EQ(out.inject_ns, static_cast<uint64_t>(i));
    }
}

TEST(ShmChannel, WrapAround) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    // Write/read pairs that push indices past the capacity boundary multiple times
    for (int iter = 0; iter < 3; ++iter) {
        for (std::size_t i = 0; i < kCap; ++i) {
            ASSERT_TRUE(ch.writer().try_write(make_event(i)));
        }
        for (std::size_t i = 0; i < kCap; ++i) {
            FeedEvent out{};
            ASSERT_TRUE(ch.reader().try_read(out));
            EXPECT_EQ(out.inject_ns, i);
        }
    }
}

TEST(ShmChannel, FeedEventFieldsPreserved) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    FeedEvent ev{};
    ev.inject_ns   = 0xDEADBEEFCAFEBABEULL;
    ev.receive_ns  = 0x0102030405060708ULL;
    ev.src_ip      = 0xC0A80001u;
    ev.src_port    = 9001;
    ev.dst_port    = 9002;
    ev.payload_len = 4;
    ev.payload[0]  = 0xAB;
    ev.payload[3]  = 0xCD;

    ASSERT_TRUE(ch.writer().try_write(ev));
    FeedEvent out{};
    ASSERT_TRUE(ch.reader().try_read(out));

    EXPECT_EQ(out.inject_ns,  ev.inject_ns);
    EXPECT_EQ(out.receive_ns, ev.receive_ns);
    EXPECT_EQ(out.src_ip,     ev.src_ip);
    EXPECT_EQ(out.src_port,   ev.src_port);
    EXPECT_EQ(out.dst_port,   ev.dst_port);
    EXPECT_EQ(out.payload_len, ev.payload_len);
    EXPECT_EQ(out.payload[0], ev.payload[0]);
    EXPECT_EQ(out.payload[3], ev.payload[3]);
}

TEST(ShmChannel, MultiThreadedSpsc) {
    const auto name = shm_name();
    Channel ch(name);
    ASSERT_TRUE(ch.is_ready());

    constexpr uint64_t kTotal = 10'000;
    std::atomic<bool> start{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (uint64_t i = 0; i < kTotal; ++i) {
            ch.writer().write_blocking(make_event(i));
        }
    });

    std::vector<uint64_t> received;
    received.reserve(kTotal);

    std::thread consumer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        while (received.size() < kTotal) {
            FeedEvent out{};
            if (ch.reader().try_read(out)) {
                received.push_back(out.inject_ns);
            }
        }
    });

    start.store(true, std::memory_order_release);
    producer.join();
    consumer.join();

    ASSERT_EQ(received.size(), kTotal);
    for (uint64_t i = 0; i < kTotal; ++i) {
        EXPECT_EQ(received[i], i);
    }
}
