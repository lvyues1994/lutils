#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <lutils/compute/Half.hpp>
#include <stdexcept>
#include <type_traits>

namespace lutils::compute::kernel {
using uint = std::uint32_t;
template <class T, std::size_t N> struct Components;
template <class T> struct Components<T, 2> {
    T x{}, y{};
};
template <class T> struct Components<T, 3> {
    T x{}, y{}, z{};
};
template <class T> struct Components<T, 4> {
    T x{}, y{}, z{}, w{};
};
struct Swizzle {
    char const *text;
    std::size_t size;
};
constexpr Swizzle operator""_sw(char const *text, std::size_t size) { return {text, size}; }
template <class T> struct SwizzleValue {
    std::array<T, 4> values{};
    std::size_t size = 0;
    constexpr operator T() const {
        if (size != 1)
            throw std::invalid_argument("scalar swizzle requires one component");
        return values[0];
    }
};
template <class T, std::size_t N> struct Vec : Components<T, N> {
    static_assert(N >= 2 && N <= 4, "vectors have two to four components");
    using Value = T;
    static constexpr std::size_t dimensions = N;
    constexpr Vec() = default;
    explicit constexpr Vec(T value) {
        for (std::size_t i = 0; i < N; ++i)
            (*this)[i] = value;
    }
    template <class... A, std::enable_if_t<sizeof...(A) == N, int> = 0> constexpr Vec(A... values) {
        T const a[N]{static_cast<T>(values)...};
        for (std::size_t i = 0; i < N; ++i)
            (*this)[i] = a[i];
    }
    template <class U> explicit constexpr Vec(Vec<U, N> const &v) {
        for (std::size_t i = 0; i < N; ++i)
            (*this)[i] = static_cast<T>(v[i]);
    }
    template <class U> constexpr Vec(SwizzleValue<U> const &v) {
        if (v.size != N)
            throw std::invalid_argument("swizzle component count");
        for (std::size_t i = 0; i < N; ++i)
            (*this)[i] = static_cast<T>(v.values[i]);
    }
    template <std::size_t M, std::enable_if_t<M + 1 == N, int> = 0>
    constexpr Vec(Vec<T, M> const &v, T tail) {
        for (std::size_t i = 0; i < M; ++i)
            (*this)[i] = v[i];
        (*this)[M] = tail;
    }
    constexpr T &operator[](std::size_t i) {
        if (i == 0)
            return this->x;
        if (i == 1)
            return this->y;
        if constexpr (N >= 3) {
            if (i == 2)
                return this->z;
        }
        if constexpr (N == 4) {
            if (i == 3)
                return this->w;
        }
        throw std::out_of_range("vector component");
    }
    constexpr T const &operator[](std::size_t i) const {
        if (i == 0)
            return this->x;
        if (i == 1)
            return this->y;
        if constexpr (N >= 3) {
            if (i == 2)
                return this->z;
        }
        if constexpr (N == 4) {
            if (i == 3)
                return this->w;
        }
        throw std::out_of_range("vector component");
    }
    constexpr SwizzleValue<T> operator[](Swizzle sw) const {
        if (sw.size < 1 || sw.size > 4)
            throw std::invalid_argument("swizzle size");
        SwizzleValue<T> out{{}, sw.size};
        for (std::size_t i = 0; i < sw.size; ++i) {
            auto c = sw.text[i];
            std::size_t j = c == 'x' || c == 'r' || c == 's'   ? 0u
                            : c == 'y' || c == 'g' || c == 't' ? 1u
                            : c == 'z' || c == 'b' || c == 'p' ? 2u
                            : c == 'w' || c == 'a' || c == 'q' ? 3u
                                                               : 4u;
            out.values[i] = (*this)[j];
        }
        return out;
    }
};
#define LUTILS_VECTOR_BINARY(OP)                                                                   \
    template <class T, std::size_t N>                                                              \
    constexpr Vec<T, N> operator OP(Vec<T, N> a, Vec<T, N> const &b) {                             \
        for (std::size_t i = 0; i < N; ++i)                                                        \
            a[i] = static_cast<T>(a[i] OP b[i]);                                                   \
        return a;                                                                                  \
    }                                                                                              \
    template <class T, std::size_t N> constexpr Vec<T, N> operator OP(Vec<T, N> a, T b) {          \
        return a OP Vec<T, N>(b);                                                                  \
    }                                                                                              \
    template <class T, std::size_t N> constexpr Vec<T, N> operator OP(T a, Vec<T, N> b) {          \
        return Vec<T, N>(a) OP b;                                                                  \
    }
LUTILS_VECTOR_BINARY(+)
LUTILS_VECTOR_BINARY(-)
LUTILS_VECTOR_BINARY(*)
LUTILS_VECTOR_BINARY(/)
#undef LUTILS_VECTOR_BINARY
#define LUTILS_VECTOR_ASSIGN(OP)                                                                   \
    template <class T, std::size_t N>                                                              \
    constexpr Vec<T, N> &operator OP(Vec<T, N> &a, Vec<T, N> const &b) {                           \
        for (std::size_t i = 0; i < N; ++i)                                                        \
            a[i] OP b[i];                                                                          \
        return a;                                                                                  \
    }
LUTILS_VECTOR_ASSIGN(+=)
LUTILS_VECTOR_ASSIGN(-=)
LUTILS_VECTOR_ASSIGN(*=)
LUTILS_VECTOR_ASSIGN(/=)
#undef LUTILS_VECTOR_ASSIGN
template <class T, std::size_t N> constexpr Vec<T, N> operator-(Vec<T, N> a) {
    for (std::size_t i = 0; i < N; ++i)
        a[i] = -a[i];
    return a;
}
template <class T, std::size_t N>
constexpr bool operator==(Vec<T, N> const &a, Vec<T, N> const &b) {
    for (std::size_t i = 0; i < N; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}
template <class T, std::size_t N>
constexpr bool operator!=(Vec<T, N> const &a, Vec<T, N> const &b) {
    return !(a == b);
}
template <class T, std::size_t N> constexpr T dot(Vec<T, N> const &a, Vec<T, N> const &b) {
    T out{};
    for (std::size_t i = 0; i < N; ++i)
        out += a[i] * b[i];
    return out;
}
template <class T> constexpr Vec<T, 3> cross(Vec<T, 3> const &a, Vec<T, 3> const &b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
template <class T, std::size_t N> T length(Vec<T, N> const &a) {
    if constexpr (std::is_same_v<T, half>)
        return half(std::sqrt(static_cast<float>(dot(a, a))));
    else
        return std::sqrt(dot(a, a));
}
template <class T, std::size_t N> Vec<T, N> normalize(Vec<T, N> const &a) { return a / length(a); }
template <class T, std::size_t N> Vec<T, N> reflect(Vec<T, N> const &a, Vec<T, N> const &n) {
    return a - T{2} * dot(n, a) * n;
}
template <class T> constexpr T min(T a, T b) { return a < b ? a : b; }
template <class T> constexpr T max(T a, T b) { return a > b ? a : b; }
template <class T, std::size_t N> constexpr Vec<T, N> min(Vec<T, N> a, Vec<T, N> const &b) {
    for (std::size_t i = 0; i < N; ++i)
        a[i] = min(a[i], b[i]);
    return a;
}
template <class T, std::size_t N> constexpr Vec<T, N> max(Vec<T, N> a, Vec<T, N> const &b) {
    for (std::size_t i = 0; i < N; ++i)
        a[i] = max(a[i], b[i]);
    return a;
}
template <class T, std::size_t N> constexpr Vec<T, N> min(Vec<T, N> a, T b) {
    return min(a, Vec<T, N>{b});
}
template <class T, std::size_t N> constexpr Vec<T, N> max(Vec<T, N> a, T b) {
    return max(a, Vec<T, N>{b});
}
template <class T, std::size_t N> constexpr Vec<T, N> clamp(Vec<T, N> a, T low, T high) {
    return min(max(a, low), high);
}
template <class T> constexpr T clamp(T a, T low, T high) { return min(max(a, low), high); }
template <class T> T sqrt(T a) { return std::sqrt(a); }
template <class T> T abs(T a) { return std::abs(a); }
template <class T> T pow(T a, T b) { return std::pow(a, b); }
template <class T> T floor(T a) { return std::floor(a); }
template <class T> T ceil(T a) { return std::ceil(a); }
template <class T> T sin(T a) { return std::sin(a); }
template <class T> T cos(T a) { return std::cos(a); }
#define LUTILS_HALF_MATH(NAME)                                                                     \
    inline half NAME(half a) { return half(std::NAME(static_cast<float>(a))); }
LUTILS_HALF_MATH(sqrt)
LUTILS_HALF_MATH(abs)
LUTILS_HALF_MATH(floor)
LUTILS_HALF_MATH(ceil)
LUTILS_HALF_MATH(sin)
LUTILS_HALF_MATH(cos)
#undef LUTILS_HALF_MATH
inline half pow(half a, half b) {
    return half(std::pow(static_cast<float>(a), static_cast<float>(b)));
}
#define LUTILS_VECTOR_MATH(NAME)                                                                   \
    template <class T, std::size_t N> Vec<T, N> NAME(Vec<T, N> a) {                                \
        for (std::size_t i = 0; i < N; ++i)                                                        \
            a[i] = NAME(a[i]);                                                                     \
        return a;                                                                                  \
    }
LUTILS_VECTOR_MATH(sqrt)
LUTILS_VECTOR_MATH(abs)
LUTILS_VECTOR_MATH(floor)
LUTILS_VECTOR_MATH(ceil)
LUTILS_VECTOR_MATH(sin)
LUTILS_VECTOR_MATH(cos)
#undef LUTILS_VECTOR_MATH
template <class T, std::size_t N> Vec<T, N> pow(Vec<T, N> a, Vec<T, N> const &b) {
    for (std::size_t i = 0; i < N; ++i)
        a[i] = pow(a[i], b[i]);
    return a;
}
using vec2 = Vec<float, 2>;
using vec3 = Vec<float, 3>;
using vec4 = Vec<float, 4>;
using ivec2 = Vec<std::int32_t, 2>;
using ivec3 = Vec<std::int32_t, 3>;
using ivec4 = Vec<std::int32_t, 4>;
using uvec2 = Vec<uint, 2>;
using uvec3 = Vec<uint, 3>;
using uvec4 = Vec<uint, 4>;
using dvec2 = Vec<double, 2>;
using dvec3 = Vec<double, 3>;
using dvec4 = Vec<double, 4>;
using f16vec2 = Vec<half, 2>;
using f16vec3 = Vec<half, 3>;
using f16vec4 = Vec<half, 4>;
inline thread_local uvec3 gl_GlobalInvocationID{};
inline thread_local uvec3 gl_LocalInvocationID{};
inline thread_local uvec3 gl_WorkGroupID{};
inline thread_local uvec3 gl_NumWorkGroups{};
inline thread_local uvec3 gl_WorkGroupSize{1u};
inline thread_local uint gl_LocalInvocationIndex = 0;
} // namespace lutils::compute::kernel
