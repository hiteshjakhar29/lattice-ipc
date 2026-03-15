#pragma once

// FeedEvent is defined in the ultrafast-feed project.
// For portability we re-declare just the parts we need, or include the header
// directly if the include path is set up.  In CI and tests we rely on the
// local copy embedded in this project.

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace lattice::signals {

/// Payload encoding written into FeedEvent::payload[] by the upstream producer.
///
/// Layout (little-endian):
///   byte  0     : EventType (uint8_t)
///   byte  1     : side — 1=bid, 0=ask (uint8_t)
///   bytes 2..7  : reserved / padding
///   bytes 8..15 : order_id (uint64_t, LE)
///   bytes 16..23: price    (double, IEEE 754 LE)
///   bytes 24..27: qty      (uint32_t, LE)
enum class EventType : uint8_t {
    Unknown = 0,
    Add     = 1,
    Modify  = 2,
    Cancel  = 3,
    Trade   = 4,
};

struct DecodedEvent {
    EventType type     = EventType::Unknown;
    bool      is_bid   = false;
    uint64_t  order_id = 0;
    double    price    = 0.0;
    uint32_t  qty      = 0;
};

/// Decode the first 28 bytes of a FeedEvent payload into a DecodedEvent.
/// Zero-allocation, no exceptions. Safe even if payload_len < 28 (returns Unknown type).
template <typename FeedEventT>
[[nodiscard]] inline DecodedEvent decode(const FeedEventT& ev) noexcept {
    static_assert(std::is_trivially_copyable_v<FeedEventT>);
    DecodedEvent d;
    if (ev.payload_len < 28) return d;

    const uint8_t* p = ev.payload;
    d.type   = static_cast<EventType>(p[0]);
    d.is_bid = (p[1] != 0);
    std::memcpy(&d.order_id, p + 8,  sizeof(uint64_t));
    std::memcpy(&d.price,    p + 16, sizeof(double));
    std::memcpy(&d.qty,      p + 24, sizeof(uint32_t));
    return d;
}

/// Build a FeedEvent payload for testing.
template <typename FeedEventT>
[[nodiscard]] inline FeedEventT make_feed_event(
        EventType type, bool is_bid,
        uint64_t order_id, double price, uint32_t qty) noexcept {
    FeedEventT ev{};
    ev.payload_len = 28;
    ev.payload[0]  = static_cast<uint8_t>(type);
    ev.payload[1]  = is_bid ? uint8_t{1} : uint8_t{0};
    std::memcpy(ev.payload + 8,  &order_id, sizeof(uint64_t));
    std::memcpy(ev.payload + 16, &price,    sizeof(double));
    std::memcpy(ev.payload + 24, &qty,      sizeof(uint32_t));
    return ev;
}

} // namespace lattice::signals
