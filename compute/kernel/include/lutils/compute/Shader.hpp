#pragma once
#include <array>
#include <limits>
#include <lutils/compute/Pixel.hpp>
#include <vector>

#if defined(__clang__)
#define LUTILS_KERNEL [[clang::annotate("kernel")]]
#else
#define LUTILS_KERNEL
#endif

namespace lutils::compute {
namespace kernel {
enum class Dim { D1 = 1, D2 = 2, D3 = 3 };
template <Dim> struct DimTraits;
template <> struct DimTraits<Dim::D1> {
    using IndexType = std::int32_t;
    static std::size_t product(IndexType n) {
        if (n < 0)
            throw std::invalid_argument("negative resource extent");
        return static_cast<std::size_t>(n);
    }
    static std::size_t flatten(IndexType i, IndexType n) {
        if (i < 0 || i >= n)
            throw std::out_of_range("resource coordinate");
        return static_cast<std::size_t>(i);
    }
};
template <std::size_t N> struct VectorDimTraits {
    using IndexType = Vec<std::int32_t, N>;
    static std::size_t product(IndexType n) {
        std::size_t size = 1;
        for (std::size_t i = 0; i < N; ++i) {
            auto d = DimTraits<Dim::D1>::product(n[i]);
            if (d && size > std::numeric_limits<std::size_t>::max() / d)
                throw std::overflow_error("resource extent overflow");
            size *= d;
        }
        return size;
    }
    static std::size_t flatten(IndexType p, IndexType n) {
        (void)product(n);
        std::size_t out = 0;
        for (std::size_t i = N; i-- > 0;)
            out = out * static_cast<std::size_t>(n[i]) + DimTraits<Dim::D1>::flatten(p[i], n[i]);
        return out;
    }
};
template <> struct DimTraits<Dim::D2> : VectorDimTraits<2> {};
template <> struct DimTraits<Dim::D3> : VectorDimTraits<3> {};
} // namespace kernel
template <class T, kernel::Dim D = kernel::Dim::D1> struct BufferResource;
namespace kernel {
template <class T, uint Slot> struct BufferBinding {
    static constexpr uint slot = Slot;
    using Value = T;
    void attach(BufferResource<T> *resource) { resource_ = resource; }
    BufferResource<T> *resource() const { return resource_; }
    void cpuView(T *data, std::size_t count) {
        data_ = data;
        count_ = count;
    }
    T &operator[](std::size_t i) const {
        if (!data_ || i >= count_)
            throw std::out_of_range("unbound buffer or buffer index");
        return data_[i];
    }

  private:
    BufferResource<T> *resource_ = nullptr;
    T *data_ = nullptr;
    std::size_t count_ = 0;
};
template <class T, uint Location> struct Uniform {
    static constexpr uint location = Location;
    using Value = T;
    T value{};
    Uniform() = default;
    Uniform(T v) : value(v) {}
    operator T() const { return value; }
    Uniform &operator=(T v) {
        value = v;
        return *this;
    }
};
template <class T> struct IsUniform : std::false_type {};
template <class T, uint S> struct IsUniform<Uniform<T, S>> : std::true_type {};
#define LUTILS_UNIFORM_OP(OP)                                                                      \
    template <class T, uint S, class U, std::enable_if_t<!IsUniform<U>::value, int> = 0>           \
    auto operator OP(Uniform<T, S> const &a, U const &b)->decltype(a.value OP b) {                 \
        return a.value OP b;                                                                       \
    }                                                                                              \
    template <class T, uint S, class U, std::enable_if_t<!IsUniform<U>::value, int> = 0>           \
    auto operator OP(U const &a, Uniform<T, S> const &b)->decltype(a OP b.value) {                 \
        return a OP b.value;                                                                       \
    }
#define LUTILS_UNIFORM_PAIR(OP)                                                                    \
    template <class T, uint A, class U, uint B>                                                    \
    auto operator OP(Uniform<T, A> const &a, Uniform<U, B> const &b) {                             \
        return a.value OP b.value;                                                                 \
    }
LUTILS_UNIFORM_PAIR(+)
LUTILS_UNIFORM_PAIR(-)
LUTILS_UNIFORM_PAIR(*)
LUTILS_UNIFORM_PAIR(/)
#undef LUTILS_UNIFORM_PAIR
LUTILS_UNIFORM_OP(+)
LUTILS_UNIFORM_OP(-)
LUTILS_UNIFORM_OP(*)
LUTILS_UNIFORM_OP(/)
#undef LUTILS_UNIFORM_OP

template <ImageFormat G, Dim D, class P, uint Slot> struct ImageBinding {
    static_assert(IsPixel<P>::value, "external pixels require ChannelType/get/set for R,G,B,A");
    using Index = typename DimTraits<D>::IndexType;
    using Vector = typename GPUFormatTraits<G>::VectorType;
    static constexpr uint slot = Slot;
    static constexpr auto format = G;
    static constexpr auto dimensions = D;
    void attach(BufferResource<P, D> *resource) { resource_ = resource; }
    BufferResource<P, D> *resource() const { return resource_; }
    void cpuView(void *data, Index extent) {
        data_ = static_cast<unsigned char *>(data);
        extent_ = extent;
    }
    Vector load(Index coordinate) const {
        if (!data_)
            throw std::out_of_range("unbound image");
        return GPUFormatTraits<G>::load(data_ + DimTraits<D>::flatten(coordinate, extent_) *
                                                    GPUFormatTraits<G>::bytes);
    }
    void store(Index coordinate, Vector value) const {
        if (!data_)
            throw std::out_of_range("unbound image");
        GPUFormatTraits<G>::store(
            data_ + DimTraits<D>::flatten(coordinate, extent_) * GPUFormatTraits<G>::bytes, value);
    }
    Index extent() const { return extent_; }

  private:
    BufferResource<P, D> *resource_ = nullptr;
    unsigned char *data_ = nullptr;
    Index extent_{};
};
template <ImageFormat G, Dim D, class P, uint S>
auto imageLoad(ImageBinding<G, D, P, S> const &image, typename DimTraits<D>::IndexType coordinate) {
    return image.load(coordinate);
}
template <ImageFormat G, Dim D, class P, uint S>
void imageStore(ImageBinding<G, D, P, S> const &image, typename DimTraits<D>::IndexType coordinate,
                typename GPUFormatTraits<G>::VectorType value) {
    image.store(coordinate, value);
}
template <ImageFormat G, Dim D, class P, uint S>
auto imageSize(ImageBinding<G, D, P, S> const &image) {
    return image.extent();
}
} // namespace kernel
} // namespace lutils::compute
