#pragma once

#include <string>
#include <utility>
#include <variant>

namespace lutils {
enum class ErrorCode { InvalidArgument, Overflow, Unsupported, Bounds, Device, Compilation };
struct Error {
    ErrorCode code;
    std::string message;
};

template <class T> struct [[nodiscard]] Result {
    Result(T value) : state_(std::move(value)) {}
    Result(Error error) : state_(std::move(error)) {}
    explicit operator bool() const noexcept { return std::holds_alternative<T>(state_); }
    T &value() & { return std::get<T>(state_); }
    T const &value() const & { return std::get<T>(state_); }
    T &&value() && { return std::get<T>(std::move(state_)); }
    Error const &error() const { return std::get<Error>(state_); }

  private:
    std::variant<T, Error> state_;
};

template <> struct [[nodiscard]] Result<void> {
    Result() = default;
    Result(Error error) : state_(std::move(error)) {}
    explicit operator bool() const noexcept {
        return std::holds_alternative<std::monostate>(state_);
    }
    Error const &error() const { return std::get<Error>(state_); }

  private:
    std::variant<std::monostate, Error> state_;
};
} // namespace lutils
