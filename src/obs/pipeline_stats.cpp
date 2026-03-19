#include "lattice/obs/pipeline_stats.hpp"

#include <cinttypes>
#include <cstdio>

namespace lattice::obs {

// ── helpers ──────────────────────────────────────────────────────────────────

static const char* ns_to_label(uint64_t ns) {
    if (ns == 0)           return "  <no data>";
    if (ns < 1'000)        return "     <1 µs ";
    if (ns < 5'000)        return "   1–5 µs  ";
    if (ns < 10'000)       return "  5–10 µs  ";
    if (ns < 50'000)       return " 10–50 µs  ";
    if (ns < 100'000)      return "50–100 µs  ";
    if (ns < 500'000)      return "100–500 µs ";
    if (ns < 1'000'000)    return "500 µs–1 ms";
    return                        "    >1 ms  ";
}

static void print_hist(const char* name, const LatencyHistogram& h) {
    std::printf("  %-24s  p50=%-12s p99=%-12s p999=%-12s  n=%" PRIu64 "\n",
                name,
                ns_to_label(h.p50()),
                ns_to_label(h.p99()),
                ns_to_label(h.p999()),
                h.total());
}

// ── PipelineStats ─────────────────────────────────────────────────────────────

void PipelineStats::reset() noexcept {
    packets_received.store(0, std::memory_order_relaxed);
    packets_dropped.store(0, std::memory_order_relaxed);
    packets_processed.store(0, std::memory_order_relaxed);
    signal_computations.store(0, std::memory_order_relaxed);
    anomaly_alerts_fired.store(0, std::memory_order_relaxed);
    shm_write_latency.reset();
    signal_compute_latency.reset();
    anomaly_check_latency.reset();
    shm_ring_depth.reset();
}

void PipelineStats::print_report() const {
    const uint64_t rx  = packets_received.load(std::memory_order_relaxed);
    const uint64_t tx  = packets_processed.load(std::memory_order_relaxed);
    const uint64_t drp = packets_dropped.load(std::memory_order_relaxed);
    const double   drop_pct = (rx > 0) ? 100.0 * static_cast<double>(drp) /
                                          static_cast<double>(rx) : 0.0;

    std::printf("┌─────────────────────────────────────────────────────────────┐\n");
    std::printf("│                   lattice-ipc pipeline stats                │\n");
    std::printf("├─────────────────────────────────────────────────────────────┤\n");
    std::printf("│ packets received   : %12" PRIu64 "                          │\n", rx);
    std::printf("│ packets processed  : %12" PRIu64 "                          │\n", tx);
    std::printf("│ packets dropped    : %12" PRIu64 "  (%.2f%%)               │\n", drp, drop_pct);
    std::printf("│ signal computations: %12" PRIu64 "                          │\n",
                signal_computations.load(std::memory_order_relaxed));
    std::printf("│ anomaly alerts     : %12" PRIu64 "                          │\n",
                anomaly_alerts_fired.load(std::memory_order_relaxed));
    std::printf("├─────────────────────────────────────────────────────────────┤\n");
    std::printf("│ latency histograms                                          │\n");
    print_hist("shm_write",        shm_write_latency);
    print_hist("signal_compute",   signal_compute_latency);
    print_hist("anomaly_check",    anomaly_check_latency);
    std::printf("├─────────────────────────────────────────────────────────────┤\n");
    std::printf("│ shm ring depth                                              │\n");
    std::printf("│  current=%" PRIu64 "  min=%" PRIu64 "  max=%" PRIu64
                "  mean=%.1f  overflows=%" PRIu64 "\n",
                shm_ring_depth.current(),
                shm_ring_depth.min_depth(),
                shm_ring_depth.max_depth(),
                shm_ring_depth.mean_depth(),
                shm_ring_depth.overflows());
    std::printf("└─────────────────────────────────────────────────────────────┘\n");
}

} // namespace lattice::obs
