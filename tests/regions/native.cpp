#include "Fixture.hpp"

using namespace regions_test;
struct Mapping final : co::NativeImageMapping {
    std::shared_ptr<std::vector<std::byte>> storage;
    std::vector<co::NativePlane> views;
    bool write = false;
    std::vector<co::NativePlane> const &planes() const noexcept override { return views; }
    lutils::Result<std::byte *> writablePlane(std::size_t p) override {
        if (!write || p >= views.size())
            return lutils::Error{lutils::ErrorCode::Bounds, "invalid plane"};
        return const_cast<std::byte *>(views[p].data);
    }
    lutils::Result<void> finish() override {
        views.clear();
        return {};
    }
};
struct Native final : co::NativeImage {
    co::NativeImageDesc desc{4, 4, co::NativeImageFormat::Rgba8};
    std::shared_ptr<std::vector<std::byte>> storage =
        std::make_shared<std::vector<std::byte>>(80, std::byte{0xa5});
    std::vector<co::NativePlane> planes{{storage->data(), 20, 4, 76}};
    bool writeOnly = false;
    co::NativeImageDesc description() const noexcept override { return desc; }
    bool writable() const noexcept override { return true; }
    bool aliases(co::NativeImage const &other) const noexcept override { return this == &other; }
    lutils::Result<std::unique_ptr<co::NativeImageMapping>> map(co::Access access) override {
        if (writeOnly && access != co::Access::Write)
            return lutils::Error{lutils::ErrorCode::Unsupported, "write-only allocation"};
        auto mapping = std::make_unique<Mapping>();
        mapping->storage = storage;
        mapping->views = planes;
        mapping->write = access != co::Access::Read;
        return std::unique_ptr<co::NativeImageMapping>{std::move(mapping)};
    }
    lutils::Result<co::FileDescriptor> exportSyncFile(co::Access) const override {
        return co::FileDescriptor{};
    }
    lutils::Result<void> importSyncFile(int, co::Access) const override { return {}; }
};
int main() {
    auto native = std::make_shared<Native>();
    auto original = native->planes;
    auto input = take(im::ImageResource::native(native));
    auto output = take(im::HostFrame::create(input.description()));
    auto destination = take(im::ImageResource::borrow(output.view()));
    auto plan = take(im::RegionPlan::crop(input.description(), {0, 0, 4, 4}));
    auto executor = im::createCpuRegionExecutor();
    ok(executor->run(plan, input, destination));
    native->planes.clear();
    check(!executor->run(plan, input, destination));
    native->planes = {original[0], original[0]};
    check(!executor->run(plan, input, destination));
    native->planes = original;
    for (auto invalid : std::vector<co::NativePlane>{{nullptr, 20, 4, 76},
                                                     {original[0].data, 20, 0, 76},
                                                     {original[0].data, 0, 4, 76},
                                                     {original[0].data, 20, SIZE_MAX, 76},
                                                     {original[0].data, SIZE_MAX, 4, 76},
                                                     {original[0].data, 20, 4, 1},
                                                     {original[0].data, 15, 4, 76},
                                                     {original[0].data, 20, 4, 75}}) {
        native->planes = {invalid};
        check(!executor->run(plan, input, destination));
    }
    native->planes = original;
    native->writeOnly = true;
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 16; ++x)
            im::row(output.view().planes[0], y)[x] = static_cast<std::byte>(y * 16 + x);
    ok(executor->run(plan, destination, input));
    for (std::size_t y = 0; y < 4; ++y)
        for (std::size_t x = 0; x < 20; ++x)
            check((*native->storage)[y * 20 + x] ==
                  (x < 16 ? static_cast<std::byte>(y * 16 + x) : std::byte{0xa5}));
    native->desc.format = static_cast<co::NativeImageFormat>(99);
    check(!im::ImageResource::native(native));
    native->desc = {3, 4, co::NativeImageFormat::Yuv420};
    check(!im::ImageResource::native(native));
    std::cout << "native mapping bounds, write-only output and padding passed\n";
}
