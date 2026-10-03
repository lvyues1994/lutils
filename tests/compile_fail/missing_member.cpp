#include <lutils/erasure.hpp>

namespace te = lutils::erasure;
template <class Model>
struct IOperation : te::Interface<IOperation, Model> {
    using IOperation::Interface::Interface;
    virtual void missingOperation() { te::value(*this).missingOperation(); }
};
struct Incompatible {};
te::Any<IOperation> object{Incompatible{}};
