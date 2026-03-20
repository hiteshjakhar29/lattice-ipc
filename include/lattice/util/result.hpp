#pragma once
// Minimal std::expected-like Result<T,E> for C++20.
// Mirrors the std::expected<T,E> interface so migration to C++23 is mechanical.

#include <utility>
#include <variant>

namespace lattice {

template <typename T, typename E>
class Result {
    // Private tagged constructors — use the static factories below.
    explicit Result(std::in_place_index_t<0>, T v)
        : data_(std::in_place_index<0>, std::move(v)) {}
    explicit Result(std::in_place_index_t<1>, E e)
        : data_(std::in_place_index<1>, std::move(e)) {}

    std::variant<T, E> data_;

public:
    /// Construct a success result.
    [[nodiscard]] static Result ok(T value) {
        return Result(std::in_place_index<0>, std::move(value));
    }
    /// Construct an error result.
    [[nodiscard]] static Result err(E error) {
        return Result(std::in_place_index<1>, std::move(error));
    }

    [[nodiscard]] bool has_value()           const noexcept { return data_.index() == 0; }
    [[nodiscard]] explicit operator bool()   const noexcept { return has_value(); }

    [[nodiscard]] T&       value()       &  { return std::get<0>(data_); }
    [[nodiscard]] const T& value() const &  { return std::get<0>(data_); }
    [[nodiscard]] T        value()       && { return std::get<0>(std::move(data_)); }

    [[nodiscard]] E&       error()       &  { return std::get<1>(data_); }
    [[nodiscard]] const E& error() const &  { return std::get<1>(data_); }

    [[nodiscard]] T value_or(T fallback) const & {
        return has_value() ? std::get<0>(data_) : std::move(fallback);
    }
};

} // namespace lattice
