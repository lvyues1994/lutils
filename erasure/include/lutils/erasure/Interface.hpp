#pragma once

#include <exception>
#include <type_traits>
#include <utility>

namespace lutils::erasure {

namespace detail {
template <template <class> class I> struct InterfaceTag {};
template <class... T> struct TypeList {};

enum class ModelKind { abstract, value, reference, proxy };
struct ErasedTag {};

struct AbstractRoot : ErasedTag {
    static constexpr auto _te_kind = ModelKind::abstract;
    virtual ~AbstractRoot() = default;
    void _te_bind() const = delete;
};

// Link carries the interface's linkage through implementation templates.
// GCC 13 can otherwise emit anonymous-interface specializations as weak
// symbols, merge unrelated translation units, or crash during instantiation.
template <template <class> class I, class Link = I<AbstractRoot>>
struct BorrowStorage;

template <template <class> class I, class Self>
void bindInterface(Self &self, BorrowStorage<I> &out) noexcept;
} // namespace detail

template <template <class> class... I>
using Extends = detail::TypeList<detail::InterfaceTag<I>...>;

// The abstract case must retain Self's static interface type. Its forwarding
// bodies are instantiated, but only concrete model overrides may execute.
template <class Self>
decltype(auto) value(Self &&self) noexcept {
    if constexpr (std::remove_reference_t<Self>::_te_kind ==
                  detail::ModelKind::abstract) {
        std::terminate();
        return std::forward<Self>(self);
    } else {
        return std::forward<Self>(self)._te_value();
    }
}

template <template <class> class I, class Model, class Bases = Extends<>,
          class Link = I<detail::AbstractRoot>>
struct Interface : Model {
    using Model::Model;
    using Model::_te_bind;
    using _te_bases = Bases;

    virtual void _te_bind(detail::BorrowStorage<I> &out) noexcept {
        detail::bindInterface<I>(*this, out);
    }
    virtual void _te_bind(detail::BorrowStorage<I> &out) const noexcept {
        detail::bindInterface<I>(*this, out);
    }
};

namespace detail {
template <class T, class List> struct Contains;
template <class T, class... U>
struct Contains<T, TypeList<U...>>
    : std::bool_constant<(std::is_same_v<T, U> or ...)> {};

template <class List, class T> struct Append;
template <class... U, class T>
struct Append<TypeList<U...>, T> { using type = TypeList<U..., T>; };

template <class Inputs, class Done, class Active> struct Expand;
template <class Tag, class Done, class Active,
          bool Complete = Contains<Tag, Done>::value,
          bool Cycle = Contains<Tag, Active>::value>
struct Visit;

template <class Done, class Active>
struct Expand<TypeList<>, Done, Active> { using type = Done; };
template <class Tag, class... Rest, class Done, class Active>
struct Expand<TypeList<Tag, Rest...>, Done, Active> {
    using next = typename Visit<Tag, Done, Active>::type;
    using type = typename Expand<TypeList<Rest...>, next, Active>::type;
};
template <class Tag, class Done, class Active, bool Cycle>
struct Visit<Tag, Done, Active, true, Cycle> { using type = Done; };
template <class Tag, class Done, class Active>
struct Visit<Tag, Done, Active, false, true> {
    static_assert(not Contains<Tag, Active>::value,
                  "cyclic erasure interface extension");
    using type = Done;
};
template <template <class> class I, class Done, class Active>
struct Visit<InterfaceTag<I>, Done, Active, false, false> {
    using bases = typename I<AbstractRoot>::_te_bases;
    using active = typename Append<Active, InterfaceTag<I>>::type;
    using expanded = typename Expand<bases, Done, active>::type;
    using type = typename Append<expanded, InterfaceTag<I>>::type;
};

template <template <class> class I>
using LinearInterfaces =
    typename Visit<InterfaceTag<I>, TypeList<>, TypeList<>>::type;

template <class List, class Root> struct Fold;
template <class Root>
struct Fold<TypeList<>, Root> { using type = Root; };
template <template <class> class I, class... Rest, class Root>
struct Fold<TypeList<InterfaceTag<I>, Rest...>, Root> {
    using type = typename Fold<TypeList<Rest...>, I<Root>>::type;
};
template <template <class> class I, class Root>
using Compose = typename Fold<LinearInterfaces<I>, Root>::type;
template <template <class> class I>
using Abstract = Compose<I, AbstractRoot>;

template <template <class> class Derived, template <class> class Base>
inline constexpr bool extendsInterface =
    Contains<InterfaceTag<Base>, LinearInterfaces<Derived>>::value;
} // namespace detail
} // namespace lutils::erasure
