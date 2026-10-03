#pragma once

#include <lutils/erasure/Interface.hpp>

namespace lutils::erasure::detail {

template <template <class> class I, class T, class Link = I<AbstractRoot>>
struct ValueRoot : Abstract<I> {
    static constexpr auto _te_kind = ModelKind::value;

    template <class... Args>
    explicit ValueRoot(std::in_place_type_t<T>, Args &&...args)
        noexcept(std::is_nothrow_constructible_v<T, Args...>)
        : target_(std::forward<Args>(args)...) {
        static_assert(std::is_nothrow_default_constructible_v<Abstract<I>>,
                      "interfaces must have a nonthrowing default constructor");
    }

    T &_te_value() & noexcept { return target_; }
    T const &_te_value() const & noexcept { return target_; }
    T &&_te_value() && noexcept { return std::move(target_); }
    T const &&_te_value() const && noexcept { return std::move(target_); }

private:
    T target_;
};

template <template <class> class I, class T, class Link = I<AbstractRoot>>
struct ValueModel final : Compose<I, ValueRoot<I, T>> {
    using Base = Compose<I, ValueRoot<I, T>>;
    using Base::Base;
};

template <template <class> class I, class T, class Link = I<AbstractRoot>>
struct ReferenceRoot : Abstract<I> {
    static constexpr auto _te_kind = ModelKind::reference;
    explicit ReferenceRoot(T *target) noexcept : target_(target) {
        static_assert(std::is_nothrow_default_constructible_v<Abstract<I>>,
                      "interfaces must have a nonthrowing default constructor");
    }

    std::remove_const_t<T> &_te_value() noexcept {
        // A const-only adapter still needs well-formed mutable virtual bodies.
        // Such bodies are inaccessible through AnyConstPtr's public interface.
        if constexpr (std::is_const_v<T>) {
            std::terminate();
        } else {
            return *target_;
        }
    }
    std::remove_const_t<T> const &_te_value() const noexcept {
        return *target_;
    }
    T *_te_target() const noexcept { return target_; }

private:
    T *target_;
};

template <template <class> class I, class T, class Link = I<AbstractRoot>>
struct ReferenceModel final : Compose<I, ReferenceRoot<I, T>> {
    using Base = Compose<I, ReferenceRoot<I, T>>;
    using Base::Base;
};

// All virtual bodies of this layout probe are valid: value() takes the
// abstract branch. No fictitious concrete type needs to implement the API.
template <class AbstractInterface>
struct ReferenceLayoutRoot : AbstractInterface { void *target = nullptr; };
template <template <class> class I>
using ReferenceLayout = Compose<I, ReferenceLayoutRoot<Abstract<I>>>;

} // namespace lutils::erasure::detail
