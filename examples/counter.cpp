#include <lutils/erasure.hpp>
#include <iostream>

namespace te = lutils::erasure;

template <class Model>
struct IReadCounter : te::Interface<IReadCounter, Model> {
    using IReadCounter::Interface::Interface;
    virtual int value() const { return te::value(*this).value(); }
};

template <class Model>
struct ICounter : te::Interface<ICounter, Model,
                               te::Extends<IReadCounter, te::ICopyable>> {
    using ICounter::Interface::Interface;
    virtual void setValue(int value) { te::value(*this).setValue(value); }
};

struct Counter {
    explicit Counter(int value) : value_(value) {}
    int value() const { return value_; }
    void setValue(int value) { value_ = value; }
private:
    int value_;
};

int main() {
    auto counter = te::Any<ICounter>{Counter{10}};
    counter.setValue(20);
    auto copy = counter;
    copy.setValue(30);

    auto borrowed = te::AnyPtr<ICounter>{&counter};
    borrowed->setValue(40);
    auto readOnly = te::AnyConstPtr<IReadCounter>{borrowed};
    te::reset(borrowed);

    auto external = Counter{50};
    auto externalView = te::AnyPtr<ICounter>{&external};
    externalView->setValue(60);

    std::cout << "owner=" << counter.value()
              << ", copy=" << copy.value()
              << ", readonly=" << readOnly->value()
              << ", external=" << external.value() << '\n';
}
