#pragma once

#include <cstdint>

namespace lattice::shm {

/// Typed error codes for shared-memory channel operations.
enum class ShmError : uint8_t {
    None            = 0,  ///< No error
    OpenFailed      = 1,  ///< shm_open() returned -1
    TruncateFailed  = 2,  ///< ftruncate() failed (writer only)
    MapFailed       = 3,  ///< mmap() returned MAP_FAILED
    BadMagic        = 4,  ///< header.magic != ShmLayout::kMagic
    VersionMismatch = 5,  ///< header.version != ShmLayout::kVersion
    SizeMismatch    = 6,  ///< capacity or element_size mismatch
};

[[nodiscard]] inline const char* to_string(ShmError e) noexcept {
    switch (e) {
        case ShmError::None:            return "None";
        case ShmError::OpenFailed:      return "OpenFailed";
        case ShmError::TruncateFailed:  return "TruncateFailed";
        case ShmError::MapFailed:       return "MapFailed";
        case ShmError::BadMagic:        return "BadMagic";
        case ShmError::VersionMismatch: return "VersionMismatch";
        case ShmError::SizeMismatch:    return "SizeMismatch";
    }
    return "Unknown";
}

} // namespace lattice::shm
