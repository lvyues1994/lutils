#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace lutils::compute::kernel {
namespace detail {
struct ExecutionContext {
    virtual ~ExecutionContext() = default;
    virtual void synchronize() = 0;
    virtual std::mutex &atomicMutex() = 0;
};
inline thread_local ExecutionContext *executionContext = nullptr;
template <class T, class F> T atomic(T &value, F update) {
    static_assert(std::is_same_v<T, std::uint32_t> || std::is_same_v<T, std::int32_t>,
                  "shader atomics require int32 or uint32");
    if (!executionContext)
        throw std::logic_error("atomic operation outside CPU dispatch");
    std::lock_guard<std::mutex> lock(executionContext->atomicMutex());
    auto old = value;
    value = update(old);
    return old;
}
} // namespace detail
template <class T, std::size_t N> struct SharedArray {
    static_assert(N > 0, "shared arrays require positive size");
    T &operator[](std::size_t i) const {
        if (!data_ || i >= N)
            throw std::out_of_range("shared array index");
        return data_[i];
    }
    void cpuView(T *data) { data_ = data; }

  private:
    T *data_ = nullptr;
};
inline void barrier() {
    if (!detail::executionContext)
        throw std::logic_error("barrier outside CPU dispatch");
    detail::executionContext->synchronize();
}
template <class T> T atomicAdd(T &value, T operand) {
    return detail::atomic(value, [=](T old) {
        auto bits = static_cast<std::uint32_t>(old) + static_cast<std::uint32_t>(operand);
        T result;
        std::memcpy(&result, &bits, sizeof(T));
        return result;
    });
}
#define LUTILS_ATOMIC(NAME, EXPR)                                                                  \
    template <class T> T NAME(T &value, T operand) {                                               \
        return detail::atomic(value, [=](T old) { return static_cast<T>(EXPR); });                 \
    }
LUTILS_ATOMIC(atomicMin, old < operand ? old : operand)
LUTILS_ATOMIC(atomicMax, old > operand ? old : operand)
LUTILS_ATOMIC(atomicAnd, old &operand)
LUTILS_ATOMIC(atomicOr, old | operand)
LUTILS_ATOMIC(atomicXor, old ^ operand)
#undef LUTILS_ATOMIC
template <class T> T atomicExchange(T &value, T operand) {
    return detail::atomic(value, [=](T) { return operand; });
}
template <class T> T atomicCompSwap(T &value, T compare, T operand) {
    return detail::atomic(value, [=](T old) { return old == compare ? operand : old; });
}
} // namespace lutils::compute::kernel
