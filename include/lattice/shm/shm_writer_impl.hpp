#pragma once
// Implementation of ShmWriter<T,N> — included by shm_writer.hpp only.

#include "lattice/shm/shm_writer.hpp"
#include "lattice/obs/pipeline_stats.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lattice::shm {

template <typename T, std::size_t N>
ShmWriter<T,N>::ShmWriter(std::string_view shm_name) {
    open_and_map(shm_name);
    if (layout_) {
        init_header();
    }
}

template <typename T, std::size_t N>
ShmWriter<T,N>::~ShmWriter() {
    unmap_and_close();
    if (!name_.empty()) {
        ::shm_unlink(name_.c_str()); // writer owns lifetime
    }
}

template <typename T, std::size_t N>
bool ShmWriter<T,N>::try_write(const T& item) noexcept {
    if (stats_) stats_->packets_received.fetch_add(1, std::memory_order_relaxed);

    std::atomic_ref<uint64_t> widx(layout_->write_idx);
    std::atomic_ref<uint64_t> ridx(layout_->read_idx);

    const uint64_t w = widx.load(std::memory_order_relaxed);
    const uint64_t r = ridx.load(std::memory_order_acquire);

    if ((w - r) >= N) {
        if (stats_) stats_->packets_dropped.fetch_add(1, std::memory_order_relaxed);
        return false; // full
    }

    layout_->slots[w & ShmLayout<T,N>::kMask] = item;
    widx.store(w + 1, std::memory_order_release);
    if (stats_) stats_->packets_processed.fetch_add(1, std::memory_order_relaxed);
    return true;
}

template <typename T, std::size_t N>
void ShmWriter<T,N>::write_blocking(const T& item) noexcept {
    while (!try_write(item)) { /* spin */ }
}

template <typename T, std::size_t N>
void ShmWriter<T,N>::open_and_map(std::string_view name) noexcept {
    name_ = std::string(name);

    // O_CREAT: create fresh or attach to existing (writer restart zeroes via init_header)
    fd_ = ::shm_open(name_.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ == -1) {
        err_ = (errno == EACCES || errno == EPERM)
                   ? ShmError::PermissionDenied
                   : ShmError::OpenFailed;
        return;
    }

    const std::size_t region_size = sizeof(ShmLayout<T,N>);
    if (::ftruncate(fd_, static_cast<off_t>(region_size)) == -1) {
        err_ = ShmError::TruncateFailed;
        ::close(fd_);
        fd_ = -1;
        return;
    }

    void* addr = ::mmap(nullptr, region_size,
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) {
        err_ = (errno == EACCES || errno == EPERM)
                   ? ShmError::PermissionDenied
                   : ShmError::MapFailed;
        ::close(fd_);
        fd_ = -1;
        return;
    }

    layout_ = static_cast<ShmLayout<T,N>*>(addr);
}

template <typename T, std::size_t N>
bool ShmWriter<T,N>::is_healthy() const noexcept {
    if (!layout_ || fd_ == -1) return false;
    struct ::stat st{};
    return ::fstat(fd_, &st) == 0;
}

template <typename T, std::size_t N>
void ShmWriter<T,N>::init_header() noexcept {
    // Zero the entire region first (handles stale data on restart)
    std::memset(layout_, 0, sizeof(ShmLayout<T,N>));

    // Initialise indices via atomic_ref with release semantics so the reader
    // sees fully initialised state after acquiring write_idx.
    std::atomic_ref<uint64_t>(layout_->write_idx).store(0, std::memory_order_release);
    std::atomic_ref<uint64_t>(layout_->read_idx).store(0, std::memory_order_release);

    // Write header last so the reader only sees kMagic after indices are ready.
    layout_->hdr.element_size = static_cast<uint32_t>(sizeof(T));
    layout_->hdr.capacity     = static_cast<uint32_t>(N);
    layout_->hdr.version      = ShmLayout<T,N>::kVersion;
    // Publish magic with a release store (readers acquire on it)
    __atomic_store_n(&layout_->hdr.magic, ShmLayout<T,N>::kMagic, __ATOMIC_RELEASE);
}

template <typename T, std::size_t N>
void ShmWriter<T,N>::unmap_and_close() noexcept {
    if (layout_) {
        ::munmap(layout_, sizeof(ShmLayout<T,N>));
        layout_ = nullptr;
    }
    if (fd_ != -1) {
        ::close(fd_);
        fd_ = -1;
    }
}

} // namespace lattice::shm
