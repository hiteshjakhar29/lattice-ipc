#pragma once

#include <cstdint>
#include <type_traits>

namespace lattice::anomaly {

/// A large order being tracked for potential spoofing detection.
struct PendingOrder {
    uint64_t order_id  = 0;
    double   price     = 0.0;
    uint32_t qty       = 0;
    uint8_t  _pad[4]   = {};  ///< Explicit padding to reach 32 bytes
    uint64_t placed_ns = 0;

    [[nodiscard]] bool is_valid() const noexcept { return order_id != 0; }
    void               invalidate() noexcept    { order_id = 0; }
};

static_assert(std::is_trivially_copyable_v<PendingOrder>);
static_assert(sizeof(PendingOrder) == 32);

} // namespace lattice::anomaly
