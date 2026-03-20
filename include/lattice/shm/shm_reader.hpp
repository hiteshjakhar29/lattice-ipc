#pragma once

#include "lattice/shm/shm_error.hpp"
#include "lattice/shm/shm_layout.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>

namespace lattice::obs { struct PipelineStats; }

namespace lattice::shm {

/// Consumer side of a shared-memory SPSC channel.
///
/// Attaches to an existing POSIX shared-memory object created by ShmWriter,
/// validates the header (magic, version, capacity, element_size), and exposes
/// try_read() for the hot path.
///
/// Thread safety: try_read() must be called from a single consumer thread only.
template <typename T, std::size_t N>
class ShmReader {
public:
    explicit ShmReader(std::string_view shm_name);
    ~ShmReader();

    ShmReader(const ShmReader&)            = delete;
    ShmReader& operator=(const ShmReader&) = delete;
    ShmReader(ShmReader&&)                 = delete;
    ShmReader& operator=(ShmReader&&)      = delete;

    /// Non-blocking read. Returns false if ring is empty.
    /// Hot path — no heap allocation, no system calls.
    [[nodiscard]] bool try_read(T& out) noexcept;

    /// Re-validate header after writer restart detection. Resets read position
    /// to current write_idx (skips stale events — correct for HFT).
    [[nodiscard]] bool reattach() noexcept;

    [[nodiscard]] bool      is_attached()  const noexcept { return layout_ != nullptr; }
    [[nodiscard]] ShmError  last_error()   const noexcept { return err_; }
    [[nodiscard]] std::string_view name()  const noexcept { return name_; }

    /// Returns true if the segment is still mapped, the fd is open, and the
    /// header is still valid.  One fstat(2) + header read — safe to call from
    /// a monitor thread; do not call on every event.
    [[nodiscard]] bool is_healthy() const noexcept;

    /// Attach an optional stats collector. Queue depth is sampled after each
    /// successful read. Relaxed atomics keep hot-path overhead negligible.
    void set_stats(lattice::obs::PipelineStats* s) noexcept { stats_ = s; }

private:
    ShmError validate_header() const noexcept;
    void     unmap_and_close() noexcept;

    int               fd_     = -1;
    ShmLayout<T,N>*   layout_ = nullptr;
    ShmError          err_    = ShmError::None;
    std::string       name_;
    lattice::obs::PipelineStats* stats_ = nullptr;
};

} // namespace lattice::shm

#include "lattice/shm/shm_reader_impl.hpp"
