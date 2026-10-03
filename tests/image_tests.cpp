#include <cstdlib>
#include <iostream>
#include <limits>
#include <lutils/image/Frame.hpp>
using namespace lutils;
namespace im = lutils::image;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #x "\n";                              \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)
int main() {
    im::FrameDesc desc{3, 3, im::nv12(), {}, im::Scan::Progressive};
    auto g = im::geometry(desc);
    CHECK(g);
    CHECK(g.value()[0].rowBytes == 3 && g.value()[0].rows == 3);
    CHECK(g.value()[1].rowBytes == 4 && g.value()[1].rows == 2);
    auto frame = im::HostFrame::create(desc, 8);
    CHECK(frame);
    CHECK(im::validate(frame.value().view()));
    auto view = frame.value().view();
    view.planes[0].stride = -8;
    view.planes[0].row0 = 16;
    CHECK(im::validate(view));
    CHECK(im::row(view.planes[0], 2) == view.planes[0].base);
    view.planes[0].capacity = 19;
    CHECK(im::validate(view));
    view.planes[0].capacity = 18;
    CHECK(!im::validate(view));
    view.planes[0].capacity = 24;
    view.planes[0].row0 = 15;
    CHECK(!im::validate(view));
    view.planes[0].row0 = 0;
    view.planes[0].stride = std::numeric_limits<std::ptrdiff_t>::min();
    CHECK(!im::validate(view));
    view = frame.value().view();
    view.planes.pop_back();
    CHECK(!im::validate(view));
    desc.format = im::yuyv422();
    CHECK(!im::geometry(desc));
    desc.format = im::uyvy422();
    CHECK(!im::geometry(desc));
    CHECK(desc.format != im::yuyv422());
    desc.width = 2;
    CHECK(im::geometry(desc));
    desc.width = 0;
    CHECK(!im::geometry(desc));
    desc.height = 0;
    CHECK(im::HostFrame::create(desc));
    desc.width = 2;
    desc.height = 2;
    auto bad = desc;
    bad.color.horizontal = static_cast<im::ChromaLocation>(99);
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.scan = static_cast<im::Scan>(-1);
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.format.planes[0].samples[1].storageBit = 0;
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.format.planes[0].samples[0].bitCount = 7;
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.format.components[0].stepX = 0;
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.format.planes[0].samples.pop_back();
    CHECK(!im::geometry(bad));
    bad = desc;
    bad.format.components.push_back(bad.format.components[0]);
    CHECK(!im::geometry(bad));
    CHECK(!im::HostFrame::create(desc, 0));
    CHECK(!im::HostFrame::create(desc, std::numeric_limits<std::size_t>::max()));
    // Custom big-endian 16-bit samples: two byte slices build one value.
    im::FormatDesc be{
        "BE16",
        {{1, 1, 2, {{im::Component::Y, 0, 0, 0, 8, 8}, {im::Component::Y, 0, 0, 8, 0, 8}}}},
        1,
        1,
        {{im::Component::Y, 1, 1, 16, im::SampleEncoding::Unsigned}}};
    CHECK(im::geometry({5, 3, be, {}, im::Scan::Progressive}));
    be.planes[0].samples[1].valueBit = 8;
    CHECK(!im::geometry({5, 3, be, {}, im::Scan::Progressive}));
    // Distinct logical planes in a single allocation.
    std::vector<std::byte> bytes(17);
    desc = {3, 3, im::nv12(), {}, im::Scan::Progressive};
    im::FrameView shared{desc,
                         {{bytes.data(), bytes.size(), 0, 3}, {bytes.data(), bytes.size(), 9, 4}}};
    CHECK(im::validate(shared));
    shared.planes[1].row0 = 0;
    CHECK(!im::validate(shared));
    CHECK(im::validate(im::readOnly(shared)));
    auto copy = frame.value();
    CHECK(copy.view().planes[0].base != frame.value().view().planes[0].base);
    std::cout << "image contracts passed\n";
}
