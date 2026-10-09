#pragma once
#include <cstring>
#include <limits>
#include <lutils/compute/Math.hpp>
#include <tuple>

namespace lutils::compute::kernel {
enum class Channel { R, G, B, A, Min, Max };
enum class NumberKind { UInt, SInt, UNorm, SNorm, Float };
enum class ChannelOrder { R, RG, RGB, BGR, RGBA, BGRA };
constexpr std::size_t channelCount(ChannelOrder order) {
    return order == ChannelOrder::R                                   ? 1
           : order == ChannelOrder::RG                                ? 2
           : order == ChannelOrder::RGB || order == ChannelOrder::BGR ? 3
                                                                      : 4;
}
constexpr std::size_t channelIndex(ChannelOrder order, Channel c) {
    auto i = static_cast<std::size_t>(c);
    if ((order == ChannelOrder::BGR || order == ChannelOrder::BGRA) && i < 3)
        i = 2 - i;
    return i;
}
template <class T, NumberKind K> struct ChannelValue {
    using Type = T;
};
template <class T> struct ChannelValue<T, NumberKind::UNorm> {
    using Type = float;
};
template <class T> struct ChannelValue<T, NumberKind::SNorm> {
    using Type = float;
};
template <class T> T saturated(double value) {
    if constexpr (isShaderFloat<T>) {
        return static_cast<T>(value);
    } else {
        if (std::isnan(value))
            return T{};
        if (value <= static_cast<double>(std::numeric_limits<T>::lowest()))
            return std::numeric_limits<T>::lowest();
        if (value >= static_cast<double>(std::numeric_limits<T>::max()))
            return std::numeric_limits<T>::max();
        return static_cast<T>(value);
    }
}
template <class T, NumberKind K> struct ChannelEncoding {
    using Value = typename ChannelValue<T, K>::Type;
    static Value decode(T raw) { return raw; }
    static T encode(Value value) { return value; }
};
template <class T> struct ChannelEncoding<T, NumberKind::UNorm> {
    using Value = float;
    static float decode(T raw) {
        return static_cast<float>(raw) / static_cast<float>(std::numeric_limits<T>::max());
    }
    static T encode(float value) {
        auto bounded = std::isnan(value) ? 0.0 : std::clamp(static_cast<double>(value), 0.0, 1.0);
        return saturated<T>(
            std::round(bounded * static_cast<double>(std::numeric_limits<T>::max())));
    }
};
template <class T> struct ChannelEncoding<T, NumberKind::SNorm> {
    using Value = float;
    static float decode(T raw) {
        return std::max(-1.0f, static_cast<float>(raw) /
                                   static_cast<float>(std::numeric_limits<T>::max()));
    }
    static T encode(float value) {
        auto bounded = std::isnan(value) ? 0.0 : std::clamp(static_cast<double>(value), -1.0, 1.0);
        return saturated<T>(
            std::round(bounded * static_cast<double>(std::numeric_limits<T>::max())));
    }
};
template <class T, ChannelOrder O, NumberKind K> struct Pixel {
    static constexpr auto order = O;
    static constexpr auto kind = K;
    using Storage = T;
    template <Channel> using ChannelType = typename ChannelValue<T, K>::Type;
    std::array<T, channelCount(O)> data{};
    Pixel() = default;
    template <class... A, std::enable_if_t<sizeof...(A) == channelCount(O), int> = 0>
    explicit Pixel(A... values)
        : data{ChannelEncoding<T, K>::encode(static_cast<ChannelType<Channel::R>>(values))...} {}
    template <Channel C> ChannelType<C> get() const {
        if constexpr (C == Channel::Min)
            return ChannelType<C>{0};
        else if constexpr (C == Channel::Max)
            return ChannelType<C>{1};
        else {
            constexpr auto i = channelIndex(O, C);
            if constexpr (i < channelCount(O))
                return ChannelEncoding<T, K>::decode(data[i]);
            else
                return ChannelType<C>{C == Channel::A ? 1 : 0};
        }
    }
    template <Channel C> void set(ChannelType<C> value) {
        constexpr auto i = channelIndex(O, C);
        if constexpr (i < channelCount(O))
            data[i] = ChannelEncoding<T, K>::encode(value);
    }
};
template <class P, Channel C> struct HasChannel : std::true_type {};
template <class T, ChannelOrder O, NumberKind K, Channel C>
struct HasChannel<Pixel<T, O, K>, C> : std::bool_constant<(channelIndex(O, C) < channelCount(O))> {
};
template <class P, Channel C, class = void> struct IsChannel : std::false_type {};
template <class P, Channel C>
struct IsChannel<P, C,
                 std::void_t<typename P::template ChannelType<C>,
                             decltype(std::declval<P const &>().template get<C>()),
                             decltype(std::declval<P &>().template set<C>(
                                 std::declval<typename P::template ChannelType<C>>()))>>
    : std::is_convertible<decltype(std::declval<P const &>().template get<C>()),
                          typename P::template ChannelType<C>> {};
template <class P>
struct IsPixel : std::conjunction<IsChannel<P, Channel::R>, IsChannel<P, Channel::G>,
                                  IsChannel<P, Channel::B>, IsChannel<P, Channel::A>> {};
template <class Src, class Dst> struct ChannelConverter {
    static Dst apply(Src value) {
        if constexpr (std::is_integral_v<Src> && isShaderFloat<Dst>)
            return static_cast<Dst>(
                std::max(-1.0, static_cast<double>(value) /
                                   static_cast<double>(std::numeric_limits<Src>::max())));
        else if constexpr (isShaderFloat<Src> && std::is_integral_v<Dst>)
            return saturated<Dst>(std::round(static_cast<double>(value) *
                                             static_cast<double>(std::numeric_limits<Dst>::max())));
        else
            return saturated<Dst>(static_cast<double>(value));
    }
};
template <class T> struct ChannelConverter<T, T> {
    static constexpr T apply(T value) { return value; }
};
template <> struct ChannelConverter<std::uint8_t, float> {
    static constexpr float apply(std::uint8_t value) { return static_cast<float>(value) / 255.0f; }
};
// Packed formats expose normalized channels independently of their storage word.
template <unsigned R, unsigned G, unsigned B, unsigned A = 0> struct PackedPixel {
    static_assert(R + G + B + A <= 32 && R < 32 && G < 32 && B < 32 && A < 32);
    using Storage = std::conditional_t<(R + G + B + A <= 16), std::uint16_t, std::uint32_t>;
    Storage bits = 0;
    template <Channel> using ChannelType = float;
    template <Channel C> static constexpr unsigned width() {
        return C == Channel::R   ? R
               : C == Channel::G ? G
               : C == Channel::B ? B
               : C == Channel::A ? A
                                 : 0;
    }
    template <Channel C> static constexpr unsigned shift() {
        return C == Channel::R ? 0 : C == Channel::G ? R : C == Channel::B ? R + G : R + G + B;
    }
    template <Channel C> float get() const {
        constexpr auto n = width<C>();
        if constexpr (n == 0)
            return C == Channel::A || C == Channel::Max ? 1.0f : 0.0f;
        else {
            constexpr auto mask = (1u << n) - 1u;
            return static_cast<float>((bits >> shift<C>()) & mask) / static_cast<float>(mask);
        }
    }
    template <Channel C> void set(float value) {
        constexpr auto n = width<C>();
        if constexpr (n != 0) {
            constexpr auto mask = (1u << n) - 1u;
            auto bounded = std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f);
            auto code = static_cast<std::uint32_t>(
                std::round(static_cast<double>(bounded) * static_cast<double>(mask)));
            bits = static_cast<Storage>((bits & ~(mask << shift<C>())) | (code << shift<C>()));
        }
    }
};
namespace cpu {
#define LUTILS_PIXEL_ALIASES(NAME, ORDER)                                                          \
    using NAME##8 = Pixel<std::uint8_t, ChannelOrder::ORDER, NumberKind::UNorm>;                   \
    using NAME##8UI = Pixel<std::uint8_t, ChannelOrder::ORDER, NumberKind::UInt>;                  \
    using NAME##8I = Pixel<std::int8_t, ChannelOrder::ORDER, NumberKind::SInt>;                    \
    using NAME##8Snorm = Pixel<std::int8_t, ChannelOrder::ORDER, NumberKind::SNorm>;               \
    using NAME##16 = Pixel<std::uint16_t, ChannelOrder::ORDER, NumberKind::UNorm>;                 \
    using NAME##16UI = Pixel<std::uint16_t, ChannelOrder::ORDER, NumberKind::UInt>;                \
    using NAME##16I = Pixel<std::int16_t, ChannelOrder::ORDER, NumberKind::SInt>;                  \
    using NAME##16Snorm = Pixel<std::int16_t, ChannelOrder::ORDER, NumberKind::SNorm>;             \
    using NAME##32F = Pixel<float, ChannelOrder::ORDER, NumberKind::Float>;                        \
    using NAME##16F = Pixel<half, ChannelOrder::ORDER, NumberKind::Float>;
LUTILS_PIXEL_ALIASES(R, R)
LUTILS_PIXEL_ALIASES(RG, RG)
LUTILS_PIXEL_ALIASES(RGB, RGB)
LUTILS_PIXEL_ALIASES(BGR, BGR)
LUTILS_PIXEL_ALIASES(RGBA, RGBA)
LUTILS_PIXEL_ALIASES(BGRA, BGRA)
#undef LUTILS_PIXEL_ALIASES
using RGB565 = PackedPixel<5, 6, 5>;
using RGB10A2 = PackedPixel<10, 10, 10, 2>;
} // namespace cpu
// The list is shared by the shader frontend, CPU codecs and Vulkan format mapping.
// clang-format off
#define LUTILS_IMAGE_FORMATS(X) \
    X(R8, r8, std::uint8_t, UNorm, 1, R8_UNORM) \
    X(RG8, rg8, std::uint8_t, UNorm, 2, R8G8_UNORM) \
    X(RGBA8, rgba8, std::uint8_t, UNorm, 4, R8G8B8A8_UNORM) \
    X(R8Snorm, r8_snorm, std::int8_t, SNorm, 1, R8_SNORM) \
    X(RG8Snorm, rg8_snorm, std::int8_t, SNorm, 2, R8G8_SNORM) \
    X(RGBA8Snorm, rgba8_snorm, std::int8_t, SNorm, 4, R8G8B8A8_SNORM) \
    X(R8UI, r8ui, std::uint8_t, UInt, 1, R8_UINT) \
    X(RG8UI, rg8ui, std::uint8_t, UInt, 2, R8G8_UINT) \
    X(RGBA8UI, rgba8ui, std::uint8_t, UInt, 4, R8G8B8A8_UINT) \
    X(R8I, r8i, std::int8_t, SInt, 1, R8_SINT) \
    X(RG8I, rg8i, std::int8_t, SInt, 2, R8G8_SINT) \
    X(RGBA8I, rgba8i, std::int8_t, SInt, 4, R8G8B8A8_SINT) \
    X(R16, r16, std::uint16_t, UNorm, 1, R16_UNORM) \
    X(RG16, rg16, std::uint16_t, UNorm, 2, R16G16_UNORM) \
    X(RGBA16, rgba16, std::uint16_t, UNorm, 4, R16G16B16A16_UNORM) \
    X(R16Snorm, r16_snorm, std::int16_t, SNorm, 1, R16_SNORM) \
    X(RG16Snorm, rg16_snorm, std::int16_t, SNorm, 2, R16G16_SNORM) \
    X(RGBA16Snorm, rgba16_snorm, std::int16_t, SNorm, 4, R16G16B16A16_SNORM) \
    X(R16UI, r16ui, std::uint16_t, UInt, 1, R16_UINT) \
    X(RG16UI, rg16ui, std::uint16_t, UInt, 2, R16G16_UINT) \
    X(RGBA16UI, rgba16ui, std::uint16_t, UInt, 4, R16G16B16A16_UINT) \
    X(R16I, r16i, std::int16_t, SInt, 1, R16_SINT) \
    X(RG16I, rg16i, std::int16_t, SInt, 2, R16G16_SINT) \
    X(RGBA16I, rgba16i, std::int16_t, SInt, 4, R16G16B16A16_SINT) \
    X(R32UI, r32ui, std::uint32_t, UInt, 1, R32_UINT) \
    X(RG32UI, rg32ui, std::uint32_t, UInt, 2, R32G32_UINT) \
    X(RGBA32UI, rgba32ui, std::uint32_t, UInt, 4, R32G32B32A32_UINT) \
    X(R32I, r32i, std::int32_t, SInt, 1, R32_SINT) \
    X(RG32I, rg32i, std::int32_t, SInt, 2, R32G32_SINT) \
    X(RGBA32I, rgba32i, std::int32_t, SInt, 4, R32G32B32A32_SINT) \
    X(R32F, r32f, float, Float, 1, R32_SFLOAT) \
    X(RG32F, rg32f, float, Float, 2, R32G32_SFLOAT) \
    X(RGBA32F, rgba32f, float, Float, 4, R32G32B32A32_SFLOAT) \
    X(R16F, r16f, half, Float, 1, R16_SFLOAT) \
    X(RG16F, rg16f, half, Float, 2, R16G16_SFLOAT) \
    X(RGBA16F, rgba16f, half, Float, 4, R16G16B16A16_SFLOAT)
// clang-format on
enum class ImageFormat {
#define LUTILS_ENUM(N, G, T, K, C, V) N,
    LUTILS_IMAGE_FORMATS(LUTILS_ENUM)
#undef LUTILS_ENUM
};
namespace gpu {
#define LUTILS_NAME(N, G, T, K, C, V) inline constexpr auto N = ImageFormat::N;
LUTILS_IMAGE_FORMATS(LUTILS_NAME)
#undef LUTILS_NAME
} // namespace gpu
struct FormatInfo {
    char const *glsl;
    NumberKind kind;
    std::size_t channels;
    std::size_t bytes;
};
inline FormatInfo formatInfo(ImageFormat format) {
    switch (format) {
#define LUTILS_INFO(N, G, T, K, C, V)                                                              \
    case ImageFormat::N:                                                                           \
        return { #G, NumberKind::K, C, sizeof(T) * C };
        LUTILS_IMAGE_FORMATS(LUTILS_INFO)
#undef LUTILS_INFO
    }
    throw std::invalid_argument("unknown image format");
}
template <NumberKind K> struct ImageValue {
    using Type = float;
};
template <> struct ImageValue<NumberKind::UInt> {
    using Type = std::uint32_t;
};
template <> struct ImageValue<NumberKind::SInt> {
    using Type = std::int32_t;
};
template <class T, NumberKind K, std::size_t N> struct FormatCodec {
    using ChannelType = typename ImageValue<K>::Type;
    using VectorType = Vec<ChannelType, 4>;
    static constexpr auto kind = K;
    static constexpr std::size_t bytes = sizeof(T) * N;
    static VectorType load(void const *ptr) {
        std::array<T, N> raw{};
        std::memcpy(raw.data(), ptr, bytes);
        VectorType result{ChannelType{0}, ChannelType{0}, ChannelType{0}, ChannelType{1}};
        for (std::size_t i = 0; i < N; ++i)
            result[i] = static_cast<ChannelType>(ChannelEncoding<T, K>::decode(raw[i]));
        return result;
    }
    static void store(void *ptr, VectorType const &value) {
        std::array<T, N> raw{};
        for (std::size_t i = 0; i < N; ++i) {
            using V = typename ChannelValue<T, K>::Type;
            raw[i] = ChannelEncoding<T, K>::encode(saturated<V>(static_cast<double>(value[i])));
        }
        std::memcpy(ptr, raw.data(), bytes);
    }
};
template <ImageFormat> struct GPUFormatTraits;
#define LUTILS_TRAITS(N, G, T, K, C, V)                                                            \
    template <> struct GPUFormatTraits<ImageFormat::N> : FormatCodec<T, NumberKind::K, C> {};
LUTILS_IMAGE_FORMATS(LUTILS_TRAITS)
#undef LUTILS_TRAITS
} // namespace lutils::compute::kernel
