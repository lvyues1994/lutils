#include <lutils/erasure.hpp>

namespace te = lutils::erasure;
template <class Model>
struct IMoveOnly : te::Interface<IMoveOnly, Model, te::Extends<te::IMovable>> {
    using IMoveOnly::Interface::Interface;
};
void copy() {
    auto owner = te::Any<IMoveOnly>{42};
    auto invalid = owner;
    (void)invalid;
}
