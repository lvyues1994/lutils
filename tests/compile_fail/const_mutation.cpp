#include <lutils/erasure.hpp>

namespace te = lutils::erasure;
template <class Model>
struct ICounter : te::Interface<ICounter, Model> {
    using ICounter::Interface::Interface;
    virtual void setValue(int n) { te::value(*this).setValue(n); }
};
struct Counter { void setValue(int) {} };
void mutate() {
    auto const target = Counter{};
    auto view = te::AnyConstPtr<ICounter>{&target};
    view->setValue(1);
}
