#pragma once

#include "lattice/obs/latency_histogram.hpp"
#include "lattice/obs/queue_depth_tracker.hpp"

#include <atomic>
#include <cstdint>

namespace lattice::obs {

/// Aggregated production stats for one pipeline instance.
///
/// Counters are plain atomics that any module can increment directly.
/// Latency histograms are filled by callers bracketing the relevant operation.
/// QueueDepthTracker is updated by ShmReader on each successful read.
///
/// print_report() writes a formatted table to stdout (defined in
/// src/obs/pipeline_stats.cpp — not for the hot path).
struct PipelineStats {
    // ── Packet counters ───────────────────────────────────────────────────────
    std::atomic<uint64_t> packets_received{0};   ///< Total try_write calls
    std::atomic<uint64_t> packets_dropped{0};    ///< Dropped (ring full)
    std::atomic<uint64_t> packets_processed{0};  ///< Successfully written

    // ── Signal / anomaly ──────────────────────────────────────────────────────
    std::atomic<uint64_t> signal_computations{0};
    std::atomic<uint64_t> anomaly_alerts_fired{0};

    // ── Latency histograms ────────────────────────────────────────────────────
    LatencyHistogram shm_write_latency;      ///< ns elapsed in try_write hot path
    LatencyHistogram signal_compute_latency; ///< ns elapsed in SignalEngine::process
    LatencyHistogram anomaly_check_latency;  ///< ns elapsed in AnomalyDetector::process

    // ── Queue depth ───────────────────────────────────────────────────────────
    QueueDepthTracker shm_ring_depth;        ///< Ring fill level sampled after each read

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void reset() noexcept;

    /// Print a formatted stats table to stdout.
    /// Not for the hot path — uses printf internally.
    void print_report() const;
};

} // namespace lattice::obs
