#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace lutils::compute::kernel {
using U32 = std::uint32_t;
using I32 = std::int32_t;
struct Invocation {
    U32 x;
    U32 y;
    U32 z;
};
struct ReadBuffer {
    explicit ReadBuffer(U32 const *data, std::size_t size) : data_(data), size_(size) {}
    U32 load(U32 index) const {
        if (index >= size_)
            throw std::out_of_range("kernel buffer read");
        return data_[index];
    }

  private:
    U32 const *data_;
    std::size_t size_;
};
struct WriteBuffer {
    explicit WriteBuffer(U32 *data, std::size_t size) : data_(data), size_(size) {}
    void store(U32 index, U32 value) const {
        if (index >= size_)
            throw std::out_of_range("kernel buffer write");
        data_[index] = value;
    }

  private:
    U32 *data_;
    std::size_t size_;
};
} // namespace lutils::compute::kernel
