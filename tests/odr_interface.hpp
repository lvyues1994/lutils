#pragma once

#include <lutils/erasure.hpp>
#include <typeinfo>

namespace odr_test {
namespace te = lutils::erasure;

template <class Model>
struct ICounter : te::Interface<ICounter, Model, te::Extends<te::ICopyable>> {
    using ICounter::Interface::Interface;
    virtual int read() const { return te::value(*this).read(); }
    virtual void write(int n) { te::value(*this).write(n); }
};

using Owner = te::Any<ICounter, te::SmallBufferStorage<64>>;
Owner makeCounter();
void increment(te::AnyPtr<ICounter> counter);
int localCounterFromProducer();
std::type_info const &localOwnerTypeFromProducer();
} // namespace odr_test
