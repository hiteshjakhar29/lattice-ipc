#pragma once

#include "lattice/shm/shm_writer.hpp"
#include "lattice/shm/shm_reader.hpp"

#include <string>
#include <string_view>

namespace lattice::shm {

/// RAII helper wiring a writer and reader to the same shm object.
///
/// Primarily used for integration tests and single-binary deployments.
/// In production, ShmWriter and ShmReader are instantiated in separate processes.
///
/// Destruction order: reader is destroyed before writer (writer calls shm_unlink).
template <typename T, std::size_t N>
class ShmChannel {
public:
    explicit ShmChannel(std::string_view shm_name)
        : name_(shm_name)
        , writer_(shm_name)
        , reader_(shm_name)
    {}

    // Reader must be destroyed before writer; destructor order is member declaration
    // order (reader_ after writer_) — so declare writer first.
    ~ShmChannel() = default;

    ShmChannel(const ShmChannel&)            = delete;
    ShmChannel& operator=(const ShmChannel&) = delete;

    [[nodiscard]] ShmWriter<T,N>& writer() noexcept { return writer_; }
    [[nodiscard]] ShmReader<T,N>& reader() noexcept { return reader_; }

    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    [[nodiscard]] bool is_ready() const noexcept {
        return writer_.is_open() && reader_.is_attached();
    }

private:
    std::string      name_;
    ShmWriter<T,N>   writer_;
    ShmReader<T,N>   reader_;
};

} // namespace lattice::shm
