#include "odr_interface.hpp"

namespace odr_test {
namespace {
struct Counter {
    int n = 17;
    int read() const { return n; }
    void write(int value) { n = value; }
};
// Exercise model construction before main, including constant operation tables.
Owner const initial{Counter{}};

template <class Model>
struct ILocal : te::Interface<ILocal, Model, te::Extends<te::ICopyable>> {
    using ILocal::Interface::Interface;
    virtual int read() const { return te::value(*this).read(); }
};
struct LocalTarget { int read() const { return 11; } };
}

Owner makeCounter() { return initial; }
void increment(te::AnyPtr<ICounter> counter) {
    counter->write(counter->read() + 1);
}
int localCounterFromProducer() {
    auto counter = te::Any<ILocal>{LocalTarget{}};
    auto view = te::AnyPtr<ILocal>{&counter};
    auto copy = view;
    return copy->read();
}
std::type_info const &localOwnerTypeFromProducer() {
    return typeid(te::Any<ILocal>);
}
} // namespace odr_test
