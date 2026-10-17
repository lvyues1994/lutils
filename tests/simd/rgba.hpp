// Included by image.cpp after the shared checks.
void rgbaConversion(std::uint32_t width, std::uint32_t height, bool reversed, s::Target target) {
    im::FrameDesc desc{width,
                       height,
                       im::nv12(),
                       {im::Matrix::Bt601, im::Range::Limited, im::ChromaLocation::Cosited,
                        im::ChromaLocation::Midpoint}};
    auto outDesc = desc;
    outDesc.format = im::rgba8();
    outDesc.color = {im::Matrix::Identity, im::Range::Full};
    std::size_t ys = width + 7, cs = 2 * ((std::size_t(width) + 1) / 2) + 11,
                os = 4 * std::size_t(width) + 13;
    std::size_t chromaRows = (std::size_t(height) + 1) / 2;
    std::vector<std::byte> input(ys * height + cs * chromaRows + 2, std::byte{0xa5});
    std::vector<std::byte> output(os * height + 2, std::byte{0x5a});
    auto plane = [&](std::size_t start, std::size_t stride, std::size_t rows) {
        return im::PlaneView{input.data(), input.size(),
                             start + (reversed ? (rows - 1) * stride : 0),
                             (reversed ? -1 : 1) * static_cast<std::ptrdiff_t>(stride)};
    };
    im::FrameView source{desc, {plane(1, ys, height), plane(1 + ys * height, cs, chromaRows)}};
    im::FrameView dest{outDesc,
                       {{output.data(), output.size(), 1, static_cast<std::ptrdiff_t>(os)}}};
    std::uint32_t state = 0x84abcd;
    auto random = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return std::byte(state & 255);
    };
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x)
            im::row(source.planes[0], y)[x] = random();
    for (std::size_t y = 0; y < chromaRows; ++y)
        for (std::size_t x = 0; x < 2 * ((std::size_t(width) + 1) / 2); ++x)
            im::row(source.planes[1], y)[x] = random();
    auto expected = output, original = input;
    auto quantize = [](int v) { return std::byte(std::clamp((v + 128) / 256, 0, 255)); };
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
            int c = std::to_integer<int>(im::row(source.planes[0], y)[x]) - 16;
            int d = std::to_integer<int>(im::row(source.planes[1], y / 2)[2 * (x / 2)]) - 128;
            int e = std::to_integer<int>(im::row(source.planes[1], y / 2)[2 * (x / 2) + 1]) - 128;
            auto p = 1 + y * os + 4 * x;
            expected[p] = quantize(298 * c + 409 * e);
            expected[p + 1] = quantize(298 * c - 100 * d - 208 * e);
            expected[p + 2] = quantize(298 * c + 516 * d);
            expected[p + 3] = std::byte{255};
        }
    auto plan = take(im::CpuConversionPlan::prepare(desc, outDesc, target));
    take(plan.run(im::readOnly(source), dest));
    check(input == original && output == expected);
    take(im::convertNv12ToRgba(im::readOnly(source), dest, target));
    check(output == expected);
    auto invalid = desc;
    invalid.color.range = im::Range::Full;
    check(!im::CpuConversionPlan::prepare(invalid, outDesc, target));
    invalid = desc;
    invalid.color.matrix = im::Matrix::Bt709;
    check(!im::CpuConversionPlan::prepare(invalid, outDesc, target));
    invalid = outDesc;
    invalid.color.transfer = im::Transfer::Linear;
    check(!im::CpuConversionPlan::prepare(desc, invalid, target));
    auto bad = dest;
    bad.planes[0].capacity = 1;
    check(!plan.run(im::readOnly(source), bad));
    bad = dest;
    bad.desc.scan = im::Scan::Interlaced;
    check(!plan.run(im::readOnly(source), bad));
    check(output == expected);
}
