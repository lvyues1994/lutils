#pragma once

#include <cstddef>
#include <cstdint>
#include <lutils/Result.hpp>
#include <string>
#include <vector>

namespace lutils::image {
enum class Component { Y, Cb, Cr, R, G, B, A };
enum class SampleEncoding { Unsigned, Signed, Float };
struct ComponentDesc {
    Component component;
    std::uint32_t stepX = 1;
    std::uint32_t stepY = 1;
    std::uint32_t valueBits = 8;
    SampleEncoding encoding = SampleEncoding::Unsigned;
};
// Storage bits are numbered LSB-first within each byte, in address order.
struct SampleBits {
    Component component;
    std::uint32_t sampleX;
    std::uint32_t sampleY;
    std::uint32_t storageBit;
    std::uint32_t valueBit;
    std::uint32_t bitCount;
};
struct PlaneFormat {
    std::uint32_t blockWidth;
    std::uint32_t blockHeight;
    std::uint32_t blockBytes;
    std::vector<SampleBits> samples;
};
struct FormatDesc {
    std::string name;
    std::vector<PlaneFormat> planes;
    std::uint32_t widthMultiple = 1;
    std::uint32_t heightMultiple = 1;
    std::vector<ComponentDesc> components;
};
bool operator==(FormatDesc const &a, FormatDesc const &b);
inline bool operator!=(FormatDesc const &a, FormatDesc const &b) { return !(a == b); }

FormatDesc rgba8();
FormatDesc yuyv422();
FormatDesc uyvy422();
FormatDesc nv12();
FormatDesc i420();

enum class Matrix { Unknown, Identity, Bt601, Bt709, Bt2020 };
enum class Range { Unknown, Full, Limited };
enum class ChromaLocation { Unknown, Cosited, Midpoint };
enum class Primaries { Unknown, Bt601_625, Bt709, Bt2020 };
enum class Transfer { Unknown, Bt709, Srgb, Linear };
enum class Scan { Progressive, Interlaced };
struct ColorSpec {
    Matrix matrix = Matrix::Unknown;
    Range range = Range::Unknown;
    ChromaLocation horizontal = ChromaLocation::Unknown;
    ChromaLocation vertical = ChromaLocation::Unknown;
    Primaries primaries = Primaries::Unknown;
    Transfer transfer = Transfer::Unknown;
};
bool operator==(ColorSpec const &a, ColorSpec const &b);
struct FrameDesc {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    FormatDesc format;
    ColorSpec color;
    Scan scan = Scan::Progressive;
};
bool operator==(FrameDesc const &a, FrameDesc const &b);

struct PlaneGeometry {
    std::size_t rowBytes;
    std::size_t rows;
};
Result<std::vector<PlaneGeometry>> geometry(FrameDesc const &desc);
struct ConstPlaneView {
    std::byte const *base = nullptr;
    std::size_t capacity = 0;
    std::size_t row0 = 0;
    std::ptrdiff_t stride = 0;
};
struct PlaneView {
    std::byte *base = nullptr;
    std::size_t capacity = 0;
    std::size_t row0 = 0;
    std::ptrdiff_t stride = 0;
};
struct ConstFrameView {
    FrameDesc desc;
    std::vector<ConstPlaneView> planes;
};
struct FrameView {
    FrameDesc desc;
    std::vector<PlaneView> planes;
};
ConstFrameView readOnly(FrameView const &view);
Result<void> validate(ConstFrameView const &view);
Result<void> validate(FrameView const &view);
// Requires a validated view and y within this plane's row count.
std::byte const *row(ConstPlaneView const &plane, std::size_t y);
std::byte *row(PlaneView const &plane, std::size_t y);

struct HostFrame {
    static Result<HostFrame> create(FrameDesc desc, std::size_t rowAlignment = 1);
    FrameView view();
    ConstFrameView view() const;
    FrameDesc const &description() const noexcept { return desc_; }

  private:
    HostFrame() = default;
    FrameDesc desc_;
    std::vector<std::vector<std::byte>> planes_;
    std::vector<std::size_t> strides_;
};
} // namespace lutils::image
