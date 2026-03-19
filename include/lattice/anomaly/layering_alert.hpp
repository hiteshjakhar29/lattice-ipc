#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::anomaly {

/// Emitted by LayeringDetector when >count_threshold large orders appear on the
/// same side of a single symbol within the layering time window.
struct LayeringAlert {
    uint16_t symbol_id       = 0;
    uint8_t  is_bid          = 0;   ///< 1 = bid side, 0 = ask side
    uint8_t  _pad[5]         = {};
    uint32_t order_count     = 0;   ///< Number of large orders seen in window
    uint64_t window_start_ns = 0;   ///< Timestamp of oldest order in window
    uint64_t triggered_ns    = 0;   ///< Timestamp when alert fired
};

static_assert(std::is_trivially_copyable_v<LayeringAlert>);

} // namespace lattice::anomaly
