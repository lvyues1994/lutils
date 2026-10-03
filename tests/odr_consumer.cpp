#include "odr_interface.hpp"

namespace odr_test {
namespace {
// Deliberately reuse the producer's local names: each TU must retain its own
// interface identity, vtables and RTTI, including under GCC LTO and UBSan.
template <class Model>
struct ILocal : te::Interface<ILocal, Model, te::Extends<te::ICopyable>> {
    using ILocal::Interface::Interface;
    virtual int read() const { return te::value(*this).read(); }
};
struct LocalTarget { int read() const { return 22; } };
}
}

int main() {
    using namespace odr_test;
    auto counter = makeCounter();
    auto copy = counter;
    increment(te::AnyPtr<ICounter>{&counter});
    auto readonly = te::AnyConstPtr<ICounter>{&counter};
    auto local = te::Any<ILocal>{LocalTarget{}};
    // Compare object addresses: some ABI libraries compare RTTI by mangled
    // name, which can be identical for unrelated anonymous-namespace types.
    if (&typeid(local) == &localOwnerTypeFromProducer()) { return 2; }
    auto localView = te::AnyConstPtr<ILocal>{&local};
    return readonly->read() == 18 and copy.read() == 17 and
           localView->read() == 22 and localCounterFromProducer() == 11 ? 0 : 1;
}
