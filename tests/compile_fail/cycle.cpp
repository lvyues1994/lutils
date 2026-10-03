#include <lutils/erasure.hpp>

namespace te = lutils::erasure;
template <class Model>
struct ICycle : te::Interface<ICycle, Model, te::Extends<ICycle>> {
    using ICycle::Interface::Interface;
};
te::Any<ICycle> object;
