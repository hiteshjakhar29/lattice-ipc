#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::anomaly {

/// Emitted by AnomalyDetector when a large order is cancelled unusually fast.
struct SpoofAlert {
    uint64_t order_id     = 0;
    double   price        = 0.0;
    uint32_t qty          = 0;
    uint8_t  _pad[4]      = {};
    uint64_t placed_ns    = 0;    ///< CLOCK_MONOTONIC ns when Add was observed
    uint64_t cancelled_ns = 0;    ///< CLOCK_MONOTONIC ns when Cancel was observed
    double   z_score      = 0.0;  ///< Sigma below mean cancellation latency (negative = fast)
};

static_assert(std::is_trivially_copyable_v<SpoofAlert>);

} // namespace lattice::anomaly
