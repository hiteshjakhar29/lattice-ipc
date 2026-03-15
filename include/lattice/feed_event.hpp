#pragma once

// Local copy of the FeedEvent struct used by lattice-ipc.
// Byte-for-byte identical to ultrafast::FeedEvent so no conversion is needed
// when consuming events produced by the ultrafast-feed pipeline.

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lattice {

struct FeedEvent {
    uint64_t inject_ns   = 0;  ///< CLOCK_MONOTONIC ns written by sender (from payload[0..7])
    uint64_t receive_ns  = 0;  ///< CLOCK_MONOTONIC ns recorded in rx_burst loop
    uint32_t src_ip      = 0;  ///< Network byte order
    uint16_t src_port    = 0;  ///< Host byte order
    uint16_t dst_port    = 0;  ///< Host byte order
    uint16_t payload_len = 0;  ///< Number of valid bytes in payload[]
    uint8_t  _pad[6]     = {}; ///< Explicit padding for predictable layout

    static constexpr std::size_t kMaxPayload = 64;
    uint8_t payload[kMaxPayload] = {};
};

static_assert(std::is_trivially_copyable_v<FeedEvent>);
static_assert(sizeof(FeedEvent) == 96);

} // namespace lattice
