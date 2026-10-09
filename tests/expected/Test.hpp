#pragma once

#ifdef LUTILS_EXPECTED_USE_STD
#include <expected>
namespace ex = std;
#else
#include <lutils/Expected.hpp>
namespace ex = lutils;
#endif

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#define CHECK(...)                                                                                 \
    do {                                                                                           \
        if (not(__VA_ARGS__)) {                                                                    \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #__VA_ARGS__ "\n";                    \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

struct Immovable {
    explicit Immovable(int value_) noexcept : value(value_) {}
    Immovable(Immovable const &) = delete;
    Immovable(Immovable &&) = delete;
    Immovable &operator=(Immovable const &) = delete;
    Immovable &operator=(Immovable &&) = delete;
    int value;
};
struct CopyOnly {
    CopyOnly() = default;
    CopyOnly(CopyOnly const &) = default;
    CopyOnly(CopyOnly &&) = delete;
    CopyOnly &operator=(CopyOnly const &) = default;
    CopyOnly &operator=(CopyOnly &&) = delete;
};
struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove const &) = default;
    ThrowingMove(ThrowingMove &&) noexcept(false) {}
    ThrowingMove &operator=(ThrowingMove const &) = default;
    ThrowingMove &operator=(ThrowingMove &&) noexcept(false) { return *this; }
};
struct Explicit {
    explicit Explicit(int value_) noexcept : value(value_) {}
    int value;
};
struct Implicit {
    Implicit(int value_) noexcept : value(value_) {}
    int value;
};
