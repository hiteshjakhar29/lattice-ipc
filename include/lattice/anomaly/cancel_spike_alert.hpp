#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::anomaly {

/// Emitted by CancelSpikeDetector when cancellation rate for a symbol exceeds
/// mean + z_threshold * stddev for that symbol's historical cancel rate.
struct CancelSpikeAlert {
    uint16_t symbol_id       = 0;
    uint8_t  _pad[6]         = {};
    double   cancels_per_sec = 0.0; ///< Rate that triggered the alert
    double   mean_rate       = 0.0; ///< Historical mean rate
    double   stddev_rate     = 0.0; ///< Historical stddev
    double   z_score         = 0.0; ///< (rate - mean) / stddev
};

static_assert(std::is_trivially_copyable_v<CancelSpikeAlert>);

} // namespace lattice::anomaly
