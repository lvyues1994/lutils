#include "regions/Fixture.hpp"

using namespace regions_test;
int main() {
    auto executor = im::createCpuRegionExecutor();
    for (auto const &spec : formats())
        for (bool reverse : {false, true})
            for (unsigned offset = 0; offset < 4; ++offset) {
                Storage src(spec, 12, 10, offset, offset, reverse);
                Storage cropped(spec, 8, 6, 3 - offset, 3 - offset, !reverse);
                Storage padded(spec, 16, 12, offset, offset, !reverse);
                src.fill(spec);
                auto original = src.bytes;
                im::Rectangle rectangle{2, 2, 8, 6};
                auto crop = take(im::RegionPlan::crop(src.desc, rectangle));
                auto oracle = expected(spec, src, cropped, rectangle, {});
                ok(crop.run(im::readOnly(src.view()), cropped.view()));
                check(cropped.bytes == oracle && src.bytes == original);
                auto pad = take(im::RegionPlan::pad(src.desc, padded.desc, {2, 2}, spec.color));
                oracle = expected(spec, src, padded, {0, 0, 12, 10}, {2, 2});
                ok(executor->run(pad, src.host(), padded.host()));
                check(padded.bytes == oracle && src.bytes == original);
            }
    auto spec = formats()[3];
    Storage src(spec, 8, 8, 1, 3), dst(spec, 12, 10, 1, 3);
    src.fill(spec);
    auto original = dst.bytes;
    for (auto rectangle : {im::Rectangle{1, 0, 4, 4},
                           {0, 1, 4, 4},
                           {0, 0, 3, 4},
                           {0, 0, 4, 3},
                           {8, 0, 2, 2},
                           {UINT32_MAX, 0, 2, 2},
                           {0, 0, 0, 0}})
        check(!im::RegionPlan::crop(src.desc, rectangle));
    check(!im::RegionPlan::pad(src.desc, dst.desc, {1, 0}, spec.color));
    check(!im::RegionPlan::pad(src.desc, dst.desc, {0, 3}, spec.color));
    auto bad = spec.color;
    bad[0].value = 256;
    check(!im::RegionPlan::pad(src.desc, dst.desc, {}, bad));
    bad = spec.color;
    bad[1] = bad[0];
    check(!im::RegionPlan::pad(src.desc, dst.desc, {}, bad));
    auto desc = dst.desc;
    desc.color.range = im::Range::Full;
    check(!im::RegionPlan::pad(src.desc, desc, {}, spec.color));
    auto plan = take(im::RegionPlan::crop(src.desc, {0, 0, 8, 8}));
    check(!plan.run(im::readOnly(src.view()), src.view()));
    check(!executor->run(plan, src.host(), src.host()));
    auto pad = take(im::RegionPlan::pad(src.desc, dst.desc, {}, spec.color));
    auto view = dst.view();
    view.planes[0].capacity = 2;
    check(!pad.run(im::readOnly(src.view()), view));
    check(
        !executor->run(pad, src.host(), take(im::ImageResource::borrow(im::readOnly(dst.view())))));
    check(dst.bytes == original);
    // Separate allocations remain valid, as do odd canvas edges filled with background.
    auto small = take(im::HostFrame::create(src.desc));
    auto outputDesc = dst.desc;
    outputDesc.width = 13;
    outputDesc.height = 11;
    auto output = take(im::HostFrame::create(outputDesc));
    ok(im::pad(im::readOnly(small.view()), {2, 2}, spec.color, output.view()));
    auto sharedPlan = take(im::RegionPlan::pad(src.desc, outputDesc, {2, 2}, spec.color));
    ok(executor->run(sharedPlan, take(im::ImageResource::borrow(small.view())),
                     take(im::ImageResource::borrow(output.view()))));
    std::cout << "CPU regions: five formats, strides, backgrounds, bounds and aliases passed\n";
}
