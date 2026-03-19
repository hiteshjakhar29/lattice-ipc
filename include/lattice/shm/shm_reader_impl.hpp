#pragma once
// Implementation of ShmReader<T,N> — included by shm_reader.hpp only.

#include "lattice/shm/shm_reader.hpp"
#include "lattice/obs/pipeline_stats.hpp"

#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace lattice::shm {

template <typename T, std::size_t N>
ShmReader<T,N>::ShmReader(std::string_view shm_name) {
    name_ = std::string(shm_name);

    fd_ = ::shm_open(name_.c_str(), O_RDWR, 0600);
    if (fd_ == -1) {
        err_ = ShmError::OpenFailed;
        return;
    }

    void* addr = ::mmap(nullptr, sizeof(ShmLayout<T,N>),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) {
        err_ = ShmError::MapFailed;
        ::close(fd_);
        fd_ = -1;
        return;
    }

    layout_ = static_cast<ShmLayout<T,N>*>(addr);
    err_    = validate_header();
    if (err_ != ShmError::None) {
        ::munmap(layout_, sizeof(ShmLayout<T,N>));
        layout_ = nullptr;
    }
}

template <typename T, std::size_t N>
ShmReader<T,N>::~ShmReader() {
    unmap_and_close();
}

template <typename T, std::size_t N>
bool ShmReader<T,N>::try_read(T& out) noexcept {
    std::atomic_ref<uint64_t> ridx(layout_->read_idx);
    std::atomic_ref<uint64_t> widx(layout_->write_idx);

    const uint64_t r = ridx.load(std::memory_order_relaxed);
    const uint64_t w = widx.load(std::memory_order_acquire);

    if (r == w) {
        return false; // empty
    }

    out = layout_->slots[r & ShmLayout<T,N>::kMask];
    ridx.store(r + 1, std::memory_order_release);

    if (stats_) {
        // depth = items remaining after consuming this one
        stats_->shm_ring_depth.record(static_cast<std::size_t>(w - (r + 1)));
    }
    return true;
}

template <typename T, std::size_t N>
bool ShmReader<T,N>::reattach() noexcept {
    if (!layout_) return false;
    err_ = validate_header();
    if (err_ != ShmError::None) return false;

    // Skip events that accumulated during reader downtime — stale in HFT context
    const uint64_t w = std::atomic_ref<uint64_t>(layout_->write_idx)
                           .load(std::memory_order_acquire);
    std::atomic_ref<uint64_t>(layout_->read_idx).store(w, std::memory_order_release);
    return true;
}

template <typename T, std::size_t N>
ShmError ShmReader<T,N>::validate_header() noexcept {
    // Acquire load on magic to synchronise with writer's release store
    const uint64_t magic = __atomic_load_n(&layout_->hdr.magic, __ATOMIC_ACQUIRE);
    if (magic != ShmLayout<T,N>::kMagic)            return ShmError::BadMagic;
    if (layout_->hdr.version != ShmLayout<T,N>::kVersion) return ShmError::VersionMismatch;
    if (layout_->hdr.capacity != static_cast<uint32_t>(N)) return ShmError::SizeMismatch;
    if (layout_->hdr.element_size != static_cast<uint32_t>(sizeof(T)))
        return ShmError::SizeMismatch;
    return ShmError::None;
}

template <typename T, std::size_t N>
void ShmReader<T,N>::unmap_and_close() noexcept {
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
