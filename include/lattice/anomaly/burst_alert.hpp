#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::anomaly {

/// Emitted by BurstDetector when an Add order's qty exceeds
/// mean_qty + z_threshold * stddev_qty for that symbol's historical order sizes.
struct BurstAlert {
    uint16_t symbol_id  = 0;
    uint8_t  _pad[2]    = {};
    uint32_t qty        = 0;   ///< The anomalous order quantity
    uint64_t order_id   = 0;
    double   mean_qty   = 0.0;
    double   stddev_qty = 0.0;
    double   z_score    = 0.0;
};

static_assert(std::is_trivially_copyable_v<BurstAlert>);

} // namespace lattice::anomaly
