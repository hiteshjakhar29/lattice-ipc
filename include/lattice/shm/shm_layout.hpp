#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lattice::shm {

/// Raw memory layout stamped directly at the base address of the mmap'd region.
///
/// This struct contains no pointers (meaningless across process address spaces)
/// and no C++ objects with non-trivial constructors. All fields are initialised
/// explicitly by ShmWriter::init_header() via memset + targeted stores.
///
/// The write and read indices are plain uint64_t accessed via std::atomic_ref
/// (C++20) — the correct tool for atomic access to non-atomic storage, designed
/// explicitly for shared-memory scenarios.
///
/// Index semantics (monotonically increasing, never wrapping):
///   full  : write_idx - read_idx >= N
///   empty : write_idx == read_idx
///   slot  : idx & (N - 1)
///
/// Cache-line layout (64 bytes each):
///   [  0.. 63] Header (magic, version, capacity, element_size, padding)
///   [ 64..127] write_idx  — producer's cache line
///   [128..191] read_idx   — consumer's cache line
///   [192.....] slots[N]   — ring data
///
/// Template parameters:
///   T — element type; must be trivially copyable.
///   N — ring capacity; must be a power of two.
template <typename T, std::size_t N>
struct ShmLayout {
    static_assert(N >= 2 && (N & (N - 1)) == 0,
        "ShmLayout: N must be a power of two and >= 2");
    static_assert(std::is_trivially_copyable_v<T>,
        "ShmLayout: T must be trivially copyable");

    static constexpr uint64_t   kMagic   = 0x4C41545449434500ULL; // "LATTICE\0"
    static constexpr uint32_t   kVersion = 1;
    static constexpr std::size_t kMask   = N - 1;

    // ── Header — fits in one 64-byte cache line ────────────────────────────────
    struct Header {
        uint64_t magic        = 0;
        uint32_t version      = 0;
        uint32_t capacity     = 0;
        uint32_t element_size = 0;
        uint8_t  _pad[44]     = {};
    };
    static_assert(sizeof(Header) == 64, "ShmLayout::Header must be exactly 64 bytes");

    Header hdr;

    // ── Indices — each on its own 64-byte cache line ───────────────────────────
    alignas(64) uint64_t write_idx = 0; ///< Producer advances (release store)
    alignas(64) uint64_t read_idx  = 0; ///< Consumer advances (release store)

    // ── Ring slots ─────────────────────────────────────────────────────────────
    alignas(64) T slots[N];
};

} // namespace lattice::shm
