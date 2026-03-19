#pragma once

#include "lattice/shm/shm_error.hpp"
#include "lattice/shm/shm_layout.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <string_view>

namespace lattice::obs { struct PipelineStats; }

namespace lattice::shm {

/// Producer side of a shared-memory SPSC channel.
///
/// Creates (or re-creates on restart) a POSIX shared-memory object, sizes it
/// to sizeof(ShmLayout<T,N>), maps it, initialises the header, and exposes
/// try_write() for the hot path.
///
/// Ownership: the writer owns the shm object lifetime. Its destructor calls
/// shm_unlink(). In production, start the writer process first.
///
/// Thread safety: try_write() must be called from a single producer thread only.
template <typename T, std::size_t N>
class ShmWriter {
public:
    explicit ShmWriter(std::string_view shm_name);
    ~ShmWriter();

    ShmWriter(const ShmWriter&)            = delete;
    ShmWriter& operator=(const ShmWriter&) = delete;
    ShmWriter(ShmWriter&&)                 = delete;
    ShmWriter& operator=(ShmWriter&&)      = delete;

    /// Non-blocking write. Returns false if ring is full.
    /// Hot path — no heap allocation, no system calls.
    [[nodiscard]] bool try_write(const T& item) noexcept;

    /// Spin until space is available then write. NOT for the hot path.
    void write_blocking(const T& item) noexcept;

    [[nodiscard]] bool     is_open()     const noexcept { return layout_ != nullptr; }
    [[nodiscard]] ShmError last_error()  const noexcept { return err_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    /// Attach an optional stats collector.  Counter updates use relaxed atomics
    /// so there is negligible overhead on the hot path.
    void set_stats(lattice::obs::PipelineStats* s) noexcept { stats_ = s; }

private:
    void open_and_map(std::string_view name) noexcept;
    void init_header() noexcept;
    void unmap_and_close() noexcept;

    int               fd_     = -1;
    ShmLayout<T,N>*   layout_ = nullptr;
    ShmError          err_    = ShmError::None;
    std::string       name_;
    lattice::obs::PipelineStats* stats_ = nullptr;
};

} // namespace lattice::shm

// ── Template implementation ────────────────────────────────────────────────────
#include "lattice/shm/shm_writer_impl.hpp"
