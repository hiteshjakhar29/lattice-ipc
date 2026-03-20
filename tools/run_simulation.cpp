// run_simulation.cpp — feeds MarketSimulator events through SignalEngine + AnomalyDetector.
//
// Usage:
//   ./build/tools/run_simulation [seconds]   (default: 5)
//
// Every 100 ms of simulated time, prints a per-symbol signal snapshot.
// At the end, reports total events processed, signals computed, and alerts fired.

#include "lattice/sim/market_simulator.hpp"
#include "lattice/signals/signal_engine.hpp"
#include "lattice/anomaly/anomaly_detector.hpp"
#include "lattice/anomaly/spoof_alert.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv) {
    const double run_seconds = (argc > 1) ? std::atof(argv[1]) : 5.0;
    const uint64_t run_ns = static_cast<uint64_t>(run_seconds * 1e9);

    lattice::sim::SimConfig cfg;
    cfg.num_symbols = 4;
    lattice::sim::MarketSimulator sim(cfg);

    // One SignalEngine + AnomalyDetector per symbol
    std::vector<lattice::signals::SignalEngine>     engines(cfg.num_symbols);
    std::vector<lattice::anomaly::AnomalyDetector>  detectors(cfg.num_symbols);

    uint64_t total_events    = 0;
    uint64_t total_alerts    = 0;
    uint64_t next_report_ns  = 100'000'000ULL; // first snapshot at 100 ms simulated

    std::printf("=== lattice-ipc MarketSimulator — %.1f s simulated time ===\n\n",
                run_seconds);

    lattice::anomaly::SpoofAlert alert{};

    while (sim.current_ns() < run_ns) {
        const lattice::FeedEvent ev = sim.next();
        const uint32_t sym = ev.src_port;

        engines[sym].process(ev);
        ++total_events;

        if (detectors[sym].process(ev, alert)) {
            ++total_alerts;
        }

        // Print per-symbol snapshots every 100 ms of simulated time
        if (sim.current_ns() >= next_report_ns) {
            std::printf("── t = %6.3f s ────────────────────────────────\n",
                        static_cast<double>(sim.current_ns()) / 1e9);
            for (uint32_t s = 0; s < cfg.num_symbols; ++s) {
                const auto& snap = engines[s].last_snapshot();
                std::printf("  sym%u  mid=%.4f  spread=%.4f  obi=%+.3f  ofi=%+.3f  "
                            "vamp=%.4f  tfi=%.3f\n",
                            s,
                            snap.mid_price,
                            snap.spread,
                            snap.obi,
                            snap.ofi,
                            snap.vamp,
                            snap.tfi);
            }
            std::printf("\n");
            next_report_ns += 100'000'000ULL;
        }
    }

    // Final summary
    std::printf("=== Summary ===\n");
    std::printf("  Simulated time  : %.3f s\n",
                static_cast<double>(sim.current_ns()) / 1e9);
    std::printf("  Events processed: %llu\n",
                static_cast<unsigned long long>(total_events));
    std::printf("    adds           : %llu\n",
                static_cast<unsigned long long>(sim.adds_generated()));
    std::printf("    cancels        : %llu\n",
                static_cast<unsigned long long>(sim.cancels_generated()));
    std::printf("    modifies       : %llu\n",
                static_cast<unsigned long long>(sim.modifies_generated()));
    std::printf("  Anomaly alerts  : %llu\n",
                static_cast<unsigned long long>(total_alerts));
    for (uint32_t s = 0; s < cfg.num_symbols; ++s) {
        std::printf("    sym%u alerts: %llu  (tracked: %llu)\n",
                    s,
                    static_cast<unsigned long long>(detectors[s].alerts_fired()),
                    static_cast<unsigned long long>(detectors[s].orders_tracked()));
    }

    return 0;
}
