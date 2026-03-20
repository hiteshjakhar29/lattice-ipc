#pragma once
// Header-only configuration management for lattice-ipc.
//
// Three sources (in increasing precedence for production use):
//   1. Compiled-in defaults (always present)
//   2. KEY=VALUE config file via load_from_file()
//   3. Environment variables via load_from_env()
//
// Validate the result with validate() before passing to any component.

#include "lattice/util/result.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace lattice::config {

struct LatticeConfig {
    // ── Shared-memory channel ──────────────────────────────────────────────────
    std::string shm_name     = "/lattice_feed";  ///< POSIX shm object name
    uint32_t    shm_capacity = 4096;             ///< Ring capacity (must be power of two)

    // ── Anomaly detector ──────────────────────────────────────────────────────
    uint32_t qty_threshold      = 1000;               ///< Min qty to track
    double   z_score_threshold  = -2.0;               ///< Alert if z-score below this
    uint32_t max_tracked_orders = 4096;               ///< Hash table size (power of two)
    uint64_t time_window_ns     = 500'000'000ULL;     ///< Eviction window (500 ms)
    uint32_t rolling_window_sz  = 256;                ///< Latency window size

    // ── Signal engine ─────────────────────────────────────────────────────────
    uint32_t ofi_window_size    = 10;
    uint32_t obi_window_size    = 20;
    uint32_t trade_window_size  = 50;

    // ── Market simulator ──────────────────────────────────────────────────────
    uint32_t num_symbols        = 4;
    double   base_price         = 100.0;
    double   order_arrival_rate = 1000.0;
    double   cancel_rate        = 0.40;
    double   modify_rate        = 0.20;
    uint64_t sim_seed           = 42;

    // ── Watchdog ──────────────────────────────────────────────────────────────
    uint32_t watchdog_timeout_ms = 1000;  ///< Declare stall if no kick within this window

    // ── Diagnostics ───────────────────────────────────────────────────────────
    bool     log_alerts        = true;
    bool     log_signals       = false;
    uint32_t stats_interval_ms = 5000;   ///< How often pipeline_stats::print_report() fires

    // ── Factory methods ───────────────────────────────────────────────────────

    /// Build config from LATTICE_* environment variables.
    /// Variables that are absent or empty keep their default values.
    [[nodiscard]] static LatticeConfig load_from_env() noexcept;

    /// Parse a KEY=VALUE config file.  Comment lines begin with '#'.
    /// Lines without '=' are silently ignored.
    /// Returns an error description on file-open failure; individual unknown
    /// keys are silently skipped (forward-compatibility).
    [[nodiscard]] static lattice::Result<LatticeConfig, std::string>
    load_from_file(std::string_view path);

    /// Check all values are within sane ranges.
    /// Returns a list of human-readable error messages; empty list ⟹ valid.
    [[nodiscard]] std::vector<std::string> validate() const;
};

// ── Implementation ─────────────────────────────────────────────────────────────

namespace detail {

inline std::string trim(std::string_view sv) {
    const auto start = sv.find_first_not_of(" \t\r\n");
    if (start == std::string_view::npos) return {};
    const auto end = sv.find_last_not_of(" \t\r\n");
    return std::string(sv.substr(start, end - start + 1));
}

inline bool is_power_of_two(uint32_t v) noexcept {
    return v >= 2 && (v & (v - 1)) == 0;
}

// Apply a single KEY=VALUE pair to cfg.  Returns false if key is unknown.
inline bool apply_kv(LatticeConfig& cfg,
                     const std::string& key, const std::string& val) {
    if (key == "SHM_NAME")            { cfg.shm_name            = val; return true; }
    if (key == "SHM_CAPACITY")        { cfg.shm_capacity         = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "QTY_THRESHOLD")       { cfg.qty_threshold        = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "Z_SCORE_THRESHOLD")   { cfg.z_score_threshold    = std::strtod(val.c_str(), nullptr); return true; }
    if (key == "MAX_TRACKED_ORDERS")  { cfg.max_tracked_orders   = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "TIME_WINDOW_NS")      { cfg.time_window_ns       = std::strtoull(val.c_str(), nullptr, 10); return true; }
    if (key == "ROLLING_WINDOW_SZ")   { cfg.rolling_window_sz    = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "OFI_WINDOW_SIZE")     { cfg.ofi_window_size      = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "OBI_WINDOW_SIZE")     { cfg.obi_window_size      = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "TRADE_WINDOW_SIZE")   { cfg.trade_window_size    = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "NUM_SYMBOLS")         { cfg.num_symbols          = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "BASE_PRICE")          { cfg.base_price           = std::strtod(val.c_str(), nullptr); return true; }
    if (key == "ORDER_ARRIVAL_RATE")  { cfg.order_arrival_rate   = std::strtod(val.c_str(), nullptr); return true; }
    if (key == "CANCEL_RATE")         { cfg.cancel_rate          = std::strtod(val.c_str(), nullptr); return true; }
    if (key == "MODIFY_RATE")         { cfg.modify_rate          = std::strtod(val.c_str(), nullptr); return true; }
    if (key == "SIM_SEED")            { cfg.sim_seed             = std::strtoull(val.c_str(), nullptr, 10); return true; }
    if (key == "WATCHDOG_TIMEOUT_MS") { cfg.watchdog_timeout_ms  = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "STATS_INTERVAL_MS")   { cfg.stats_interval_ms    = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10)); return true; }
    if (key == "LOG_ALERTS")  {
        cfg.log_alerts  = (val == "1" || val == "true" || val == "yes");
        return true;
    }
    if (key == "LOG_SIGNALS") {
        cfg.log_signals = (val == "1" || val == "true" || val == "yes");
        return true;
    }
    return false; // unknown key — forward-compatible skip
}

} // namespace detail

inline LatticeConfig LatticeConfig::load_from_env() noexcept {
    LatticeConfig cfg;
    // Helper: try to apply env var "LATTICE_<KEY>" as file-key "<KEY>".
    auto try_env = [&](const char* env_key, const char* cfg_key) {
        const char* v = std::getenv(env_key);
        if (v && *v) detail::apply_kv(cfg, cfg_key, v);
    };
    try_env("LATTICE_SHM_NAME",            "SHM_NAME");
    try_env("LATTICE_SHM_CAPACITY",        "SHM_CAPACITY");
    try_env("LATTICE_QTY_THRESHOLD",       "QTY_THRESHOLD");
    try_env("LATTICE_Z_SCORE_THRESHOLD",   "Z_SCORE_THRESHOLD");
    try_env("LATTICE_MAX_TRACKED_ORDERS",  "MAX_TRACKED_ORDERS");
    try_env("LATTICE_TIME_WINDOW_NS",      "TIME_WINDOW_NS");
    try_env("LATTICE_ROLLING_WINDOW_SZ",   "ROLLING_WINDOW_SZ");
    try_env("LATTICE_OFI_WINDOW_SIZE",     "OFI_WINDOW_SIZE");
    try_env("LATTICE_OBI_WINDOW_SIZE",     "OBI_WINDOW_SIZE");
    try_env("LATTICE_TRADE_WINDOW_SIZE",   "TRADE_WINDOW_SIZE");
    try_env("LATTICE_NUM_SYMBOLS",         "NUM_SYMBOLS");
    try_env("LATTICE_BASE_PRICE",          "BASE_PRICE");
    try_env("LATTICE_ORDER_ARRIVAL_RATE",  "ORDER_ARRIVAL_RATE");
    try_env("LATTICE_CANCEL_RATE",         "CANCEL_RATE");
    try_env("LATTICE_MODIFY_RATE",         "MODIFY_RATE");
    try_env("LATTICE_SIM_SEED",            "SIM_SEED");
    try_env("LATTICE_WATCHDOG_TIMEOUT_MS", "WATCHDOG_TIMEOUT_MS");
    try_env("LATTICE_LOG_ALERTS",          "LOG_ALERTS");
    try_env("LATTICE_LOG_SIGNALS",         "LOG_SIGNALS");
    try_env("LATTICE_STATS_INTERVAL_MS",   "STATS_INTERVAL_MS");
    return cfg;
}

inline lattice::Result<LatticeConfig, std::string>
LatticeConfig::load_from_file(std::string_view path) {
    const std::string path_str(path);
    std::ifstream f(path_str);
    if (!f) {
        return lattice::Result<LatticeConfig, std::string>::err(
            "cannot open config file: " + std::string(path));
    }

    LatticeConfig cfg;
    std::string line;
    int line_no = 0;

    while (std::getline(f, line)) {
        ++line_no;
        const std::string trimmed = detail::trim(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;

        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) continue; // no '=' — skip silently

        std::string key = detail::trim(trimmed.substr(0, eq));
        std::string val = detail::trim(trimmed.substr(eq + 1));

        // Convert key to upper-case for case-insensitive matching
        for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        detail::apply_kv(cfg, key, val); // unknown keys silently skipped
    }
    return lattice::Result<LatticeConfig, std::string>::ok(std::move(cfg));
}

inline std::vector<std::string> LatticeConfig::validate() const {
    std::vector<std::string> errors;

    if (shm_name.empty() || shm_name[0] != '/')
        errors.push_back("shm_name must be non-empty and start with '/'");

    if (!detail::is_power_of_two(shm_capacity))
        errors.push_back("shm_capacity must be a power of two >= 2");

    if (!detail::is_power_of_two(max_tracked_orders))
        errors.push_back("max_tracked_orders must be a power of two >= 2");

    if (z_score_threshold > 0.0)
        errors.push_back("z_score_threshold must be <= 0 (alerts on abnormally fast cancels)");

    if (base_price <= 0.0)
        errors.push_back("base_price must be positive");

    if (order_arrival_rate <= 0.0)
        errors.push_back("order_arrival_rate must be positive");

    if (cancel_rate < 0.0 || cancel_rate > 1.0)
        errors.push_back("cancel_rate must be in [0.0, 1.0]");

    if (modify_rate < 0.0 || modify_rate > 1.0)
        errors.push_back("modify_rate must be in [0.0, 1.0]");

    if (cancel_rate + modify_rate > 2.0)
        errors.push_back("cancel_rate + modify_rate should not exceed 2.0");

    if (num_symbols == 0)
        errors.push_back("num_symbols must be >= 1");

    if (watchdog_timeout_ms == 0)
        errors.push_back("watchdog_timeout_ms must be > 0");

    if (rolling_window_sz == 0)
        errors.push_back("rolling_window_sz must be > 0");

    if (ofi_window_size == 0)
        errors.push_back("ofi_window_size must be > 0");

    if (obi_window_size == 0)
        errors.push_back("obi_window_size must be > 0");

    if (trade_window_size == 0)
        errors.push_back("trade_window_size must be > 0");

    if (stats_interval_ms == 0)
        errors.push_back("stats_interval_ms must be > 0");

    return errors;
}

} // namespace lattice::config
