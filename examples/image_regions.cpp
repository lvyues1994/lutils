#include <iostream>
#include <lutils/image/ImageResource.hpp>
#include <stdexcept>

namespace im = lutils::image;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
void take(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
}
int main() {
    im::FrameDesc description{640, 480, im::nv12(), {}, im::Scan::Progressive};
    auto input = take(im::HostFrame::create(description));
    auto cropPlan = take(im::RegionPlan::crop(description, {64, 48, 320, 240}));
    auto cropped = take(im::HostFrame::create(cropPlan.destination()));
    take(im::crop(im::readOnly(input.view()), {64, 48, 320, 240}, cropped.view()));

    auto output = take(im::HostFrame::create(description));
    im::Background background{
        {im::Component::Y, 16}, {im::Component::Cb, 128}, {im::Component::Cr, 128}};
    auto padPlan =
        take(im::RegionPlan::pad(cropped.description(), description, {160, 120}, background));
    auto source = take(im::ImageResource::borrow(cropped.view()));
    auto destination = take(im::ImageResource::borrow(output.view()));
    auto executor = im::createCpuRegionExecutor();
    take(executor->run(padPlan, source, destination));
    std::cout << "NV12: cropped 320x240, placed at (160,120) in a 640x480 background\n";
}
