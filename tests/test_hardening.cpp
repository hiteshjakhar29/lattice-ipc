// tests/test_hardening.cpp — tests for config management, shm error codes,
// is_healthy(), and the watchdog.

#include "lattice/config/lattice_config.hpp"
#include "lattice/recovery/watchdog.hpp"
#include "lattice/shm/shm_channel.hpp"
#include "lattice/shm/shm_error.hpp"
#include "lattice/shm/shm_reader.hpp"
#include "lattice/shm/shm_writer.hpp"
#include "lattice/feed_event.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

using namespace lattice;
using namespace lattice::config;
using namespace lattice::recovery;
using namespace lattice::shm;

// ── RAII env-var guard ────────────────────────────────────────────────────────

struct ScopedEnv {
    std::string key;
    ScopedEnv(const char* k, const char* v) : key(k) { ::setenv(k, v, 1); }
    ~ScopedEnv() { ::unsetenv(key.c_str()); }
};

// ─────────────────────────────────────────────────────────────────────────────
// LatticeConfig — defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(LatticeConfig, DefaultsPassValidation) {
    LatticeConfig cfg;
    const auto errors = cfg.validate();
    EXPECT_TRUE(errors.empty())
        << "Default config should be valid; errors:\n"
        << [&]{ std::string s; for (auto& e : errors) s += "  " + e + "\n"; return s; }();
}

TEST(LatticeConfig, DefaultsHaveSensibleValues) {
    LatticeConfig cfg;
    EXPECT_EQ(cfg.shm_name, "/lattice_feed");
    EXPECT_EQ(cfg.shm_capacity, 4096u);
    EXPECT_EQ(cfg.num_symbols, 4u);
    EXPECT_DOUBLE_EQ(cfg.base_price, 100.0);
    EXPECT_EQ(cfg.watchdog_timeout_ms, 1000u);
}

// ─────────────────────────────────────────────────────────────────────────────
// LatticeConfig — validate()
// ─────────────────────────────────────────────────────────────────────────────

TEST(LatticeConfig, InvalidShmNameFails) {
    LatticeConfig cfg;
    cfg.shm_name = "no_leading_slash";
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, EmptyShmNameFails) {
    LatticeConfig cfg;
    cfg.shm_name = "";
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, NonPowerOfTwoShmCapacityFails) {
    LatticeConfig cfg;
    cfg.shm_capacity = 3000;  // not a power of two
    auto errors = cfg.validate();
    ASSERT_FALSE(errors.empty());
    bool found = false;
    for (const auto& e : errors)
        if (e.find("shm_capacity") != std::string::npos) { found = true; break; }
    EXPECT_TRUE(found) << "Expected shm_capacity error";
}

TEST(LatticeConfig, NonPowerOfTwoMaxTrackedOrdersFails) {
    LatticeConfig cfg;
    cfg.max_tracked_orders = 1000;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, PositiveZScoreThresholdFails) {
    LatticeConfig cfg;
    cfg.z_score_threshold = 1.0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, ZeroBasePriceFails) {
    LatticeConfig cfg;
    cfg.base_price = 0.0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, NegativeBasePriceFails) {
    LatticeConfig cfg;
    cfg.base_price = -10.0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, CancelRateAboveOneFails) {
    LatticeConfig cfg;
    cfg.cancel_rate = 1.5;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, ZeroNumSymbolsFails) {
    LatticeConfig cfg;
    cfg.num_symbols = 0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, ZeroWatchdogTimeoutFails) {
    LatticeConfig cfg;
    cfg.watchdog_timeout_ms = 0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, ZeroArrivalRateFails) {
    LatticeConfig cfg;
    cfg.order_arrival_rate = 0.0;
    EXPECT_FALSE(cfg.validate().empty());
}

TEST(LatticeConfig, MultipleErrorsAllReported) {
    LatticeConfig cfg;
    cfg.shm_name           = "bad";
    cfg.shm_capacity       = 3;
    cfg.base_price         = -1.0;
    cfg.watchdog_timeout_ms = 0;
    const auto errors = cfg.validate();
    EXPECT_GE(errors.size(), 3u) << "Expected at least 3 distinct errors";
}

// ─────────────────────────────────────────────────────────────────────────────
// LatticeConfig — load_from_env()
// ─────────────────────────────────────────────────────────────────────────────

TEST(LatticeConfig, LoadFromEnvOverridesDefaults) {
    ScopedEnv e1("LATTICE_SHM_NAME",    "/env_shm");
    ScopedEnv e2("LATTICE_NUM_SYMBOLS", "8");
    ScopedEnv e3("LATTICE_BASE_PRICE",  "200.0");

    const auto cfg = LatticeConfig::load_from_env();
    EXPECT_EQ(cfg.shm_name, "/env_shm");
    EXPECT_EQ(cfg.num_symbols, 8u);
    EXPECT_DOUBLE_EQ(cfg.base_price, 200.0);
}

TEST(LatticeConfig, LoadFromEnvMissingVarsKeepDefaults) {
    // Ensure the vars are NOT set
    ::unsetenv("LATTICE_SHM_NAME");
    ::unsetenv("LATTICE_NUM_SYMBOLS");

    const auto cfg = LatticeConfig::load_from_env();
    EXPECT_EQ(cfg.shm_name, "/lattice_feed");
    EXPECT_EQ(cfg.num_symbols, 4u);
}

TEST(LatticeConfig, LoadFromEnvBoolVars) {
    {
        ScopedEnv e1("LATTICE_LOG_ALERTS",  "0");
        ScopedEnv e2("LATTICE_LOG_SIGNALS", "1");
        const auto cfg = LatticeConfig::load_from_env();
        EXPECT_FALSE(cfg.log_alerts);
        EXPECT_TRUE(cfg.log_signals);
    }
    {
        ScopedEnv e1("LATTICE_LOG_ALERTS",  "true");
        ScopedEnv e2("LATTICE_LOG_SIGNALS", "false");
        const auto cfg = LatticeConfig::load_from_env();
        EXPECT_TRUE(cfg.log_alerts);
        EXPECT_FALSE(cfg.log_signals);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LatticeConfig — load_from_file()
// ─────────────────────────────────────────────────────────────────────────────

TEST(LatticeConfig, LoadFromFileNotFoundReturnsError) {
    const auto result = LatticeConfig::load_from_file("/nonexistent/path/lattice.cfg");
    EXPECT_FALSE(result.has_value());
    EXPECT_FALSE(result.error().empty());
}

TEST(LatticeConfig, LoadFromFileValidParsesAllFields) {
    const char* path = "/tmp/lattice_test_valid.cfg";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f) << "Cannot create temp file";
        f << "# lattice config\n";
        f << "SHM_NAME=/test_feed\n";
        f << "SHM_CAPACITY=8192\n";
        f << "NUM_SYMBOLS=2\n";
        f << "BASE_PRICE=50.0\n";
        f << "CANCEL_RATE=0.30\n";
        f << "WATCHDOG_TIMEOUT_MS=500\n";
        f << "LOG_SIGNALS=true\n";
    }
    const auto result = LatticeConfig::load_from_file(path);
    ASSERT_TRUE(result.has_value()) << "load_from_file should succeed";
    const auto& cfg = result.value();
    EXPECT_EQ(cfg.shm_name, "/test_feed");
    EXPECT_EQ(cfg.shm_capacity, 8192u);
    EXPECT_EQ(cfg.num_symbols, 2u);
    EXPECT_DOUBLE_EQ(cfg.base_price, 50.0);
    EXPECT_DOUBLE_EQ(cfg.cancel_rate, 0.30);
    EXPECT_EQ(cfg.watchdog_timeout_ms, 500u);
    EXPECT_TRUE(cfg.log_signals);
    ::remove(path);
}

TEST(LatticeConfig, LoadFromFileCommentsAndBlankLinesIgnored) {
    const char* path = "/tmp/lattice_test_comments.cfg";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f);
        f << "# this is a comment\n";
        f << "\n";
        f << "   # indented comment\n";
        f << "NUM_SYMBOLS=3\n";
        f << "\n";
    }
    const auto result = LatticeConfig::load_from_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().num_symbols, 3u);
    ::remove(path);
}

TEST(LatticeConfig, LoadFromFileUnknownKeysSkipped) {
    const char* path = "/tmp/lattice_test_unknown.cfg";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f);
        f << "UNKNOWN_KEY=some_value\n";
        f << "NUM_SYMBOLS=6\n";
    }
    // Should not fail — unknown keys are silently skipped
    const auto result = LatticeConfig::load_from_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().num_symbols, 6u);
    ::remove(path);
}

TEST(LatticeConfig, LoadFromFileCaseInsensitiveKeys) {
    const char* path = "/tmp/lattice_test_case.cfg";
    {
        std::ofstream f(path);
        ASSERT_TRUE(f);
        f << "shm_name=/lower_case_shm\n";
        f << "Num_Symbols=5\n";
    }
    const auto result = LatticeConfig::load_from_file(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().shm_name, "/lower_case_shm");
    EXPECT_EQ(result.value().num_symbols, 5u);
    ::remove(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShmError — new error codes
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShmError, ToStringCoversAllCodes) {
    EXPECT_STREQ(to_string(ShmError::None),              "None");
    EXPECT_STREQ(to_string(ShmError::OpenFailed),        "OpenFailed");
    EXPECT_STREQ(to_string(ShmError::TruncateFailed),    "TruncateFailed");
    EXPECT_STREQ(to_string(ShmError::MapFailed),         "MapFailed");
    EXPECT_STREQ(to_string(ShmError::BadMagic),          "BadMagic");
    EXPECT_STREQ(to_string(ShmError::VersionMismatch),   "VersionMismatch");
    EXPECT_STREQ(to_string(ShmError::SizeMismatch),      "SizeMismatch");
    EXPECT_STREQ(to_string(ShmError::SegmentNotFound),   "SegmentNotFound");
    EXPECT_STREQ(to_string(ShmError::PermissionDenied),  "PermissionDenied");
    EXPECT_STREQ(to_string(ShmError::HealthCheckFailed), "HealthCheckFailed");
}

TEST(ShmError, ReaderSegmentNotFoundOnMissingShm) {
    // Use a name that is guaranteed not to exist
    ShmReader<FeedEvent, 64> reader("/lattice_no_such_segment_xyz9876");
    EXPECT_FALSE(reader.is_attached());
    EXPECT_EQ(reader.last_error(), ShmError::SegmentNotFound);
}

// ─────────────────────────────────────────────────────────────────────────────
// is_healthy()
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShmHealth, ChannelIsHealthyWhenOpen) {
    ShmChannel<FeedEvent, 64> ch("/lattice_health_ch1");
    ASSERT_TRUE(ch.is_ready());
    EXPECT_TRUE(ch.writer().is_healthy());
    EXPECT_TRUE(ch.reader().is_healthy());
}

TEST(ShmHealth, WriterIsHealthyAfterWrites) {
    ShmChannel<FeedEvent, 64> ch("/lattice_health_ch2");
    ASSERT_TRUE(ch.is_ready());

    FeedEvent ev{};
    for (int i = 0; i < 32; ++i) {
        ev.inject_ns = static_cast<uint64_t>(i);
        ASSERT_TRUE(ch.writer().try_write(ev));
    }
    EXPECT_TRUE(ch.writer().is_healthy());
    EXPECT_TRUE(ch.reader().is_healthy());
}

TEST(ShmHealth, FailedWriterIsNotHealthy) {
    // POSIX: shm names with embedded '/' after the leading one are invalid (EINVAL).
    ShmWriter<FeedEvent, 64> bad_writer("/embedded/slash/invalid");
    EXPECT_FALSE(bad_writer.is_open());
    EXPECT_FALSE(bad_writer.is_healthy());
}

TEST(ShmHealth, FailedReaderIsNotHealthy) {
    ShmReader<FeedEvent, 64> bad_reader("/lattice_no_such_segment_abc123");
    EXPECT_FALSE(bad_reader.is_attached());
    EXPECT_FALSE(bad_reader.is_healthy());
}

// ─────────────────────────────────────────────────────────────────────────────
// Watchdog
// ─────────────────────────────────────────────────────────────────────────────

TEST(Watchdog, IsAliveImmediatelyAfterConstruction) {
    Watchdog wd(200);
    EXPECT_TRUE(wd.is_alive());
}

TEST(Watchdog, IsAliveAfterKick) {
    Watchdog wd(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    wd.kick();
    EXPECT_TRUE(wd.is_alive());
}

TEST(Watchdog, IsDeadAfterTimeoutExpires) {
    Watchdog wd(20);  // 20 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(wd.is_alive());
}

TEST(Watchdog, KickResetsDeadline) {
    Watchdog wd(40);  // 40 ms
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    wd.kick();  // reset before expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // 30 ms after kick — still within 40 ms window
    EXPECT_TRUE(wd.is_alive());
}

TEST(Watchdog, ElapsedNsGrows) {
    Watchdog wd(1000);
    const uint64_t before = wd.elapsed_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const uint64_t after = wd.elapsed_ns();
    EXPECT_GT(after, before);
}

TEST(Watchdog, TimeoutAccessors) {
    Watchdog wd(250);
    EXPECT_EQ(wd.timeout_ms(), 250u);
    EXPECT_EQ(wd.timeout_ns(), 250'000'000ULL);
}

TEST(Watchdog, ConcurrentKickAndIsAlive) {
    Watchdog wd(50);  // 50 ms
    std::atomic<bool> done{false};

    std::thread kicker([&] {
        while (!done.load(std::memory_order_relaxed)) {
            wd.kick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        EXPECT_TRUE(wd.is_alive()) << "Watchdog should be alive at iteration " << i;
    }

    done.store(true, std::memory_order_relaxed);
    kicker.join();
}
