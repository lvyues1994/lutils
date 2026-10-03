#pragma once

#include <lutils/erasure/Interface.hpp>

namespace lutils::erasure {

template <class Model>
struct IMovable : Interface<IMovable, Model> {
    using IMovable::Interface::Interface;
};

template <class Model>
struct ICopyable : Interface<ICopyable, Model, Extends<IMovable>> {
    using ICopyable::Interface::Interface;
};

namespace detail {
template <template <class> class I>
inline constexpr bool canCopy = extendsInterface<I, ICopyable>;
template <template <class> class I>
inline constexpr bool canMove = extendsInterface<I, IMovable>;

template <bool Enabled> struct CopyControl {};
template <> struct CopyControl<false> {
    CopyControl() = default;
    CopyControl(CopyControl const &) = delete;
    CopyControl &operator=(CopyControl const &) = delete;
    CopyControl(CopyControl &&) = default;
    CopyControl &operator=(CopyControl &&) = default;
};
template <bool Enabled> struct MoveControl {};
template <> struct MoveControl<false> {
    MoveControl() = default;
    MoveControl(MoveControl const &) = default;
    MoveControl &operator=(MoveControl const &) = default;
    MoveControl(MoveControl &&) = delete;
    MoveControl &operator=(MoveControl &&) = delete;
};
} // namespace detail
} // namespace lutils::erasure
