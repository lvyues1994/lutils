#pragma once

#include <lutils/erasure/Storage.hpp>

namespace lutils::erasure {
namespace detail {

template <template <class> class I, class T, class... Args>
inline constexpr bool canOwn =
    std::is_same_v<T, std::decay_t<T>> and
    not std::is_base_of_v<ErasedTag, T> and
    std::is_constructible_v<T, Args...> and
    std::is_nothrow_destructible_v<T> and
    (not canCopy<I> or std::is_copy_constructible_v<T>);

template <template <class> class I, class Policy, class Link = I<AbstractRoot>>
struct OwnerRoot : AbstractRoot,
                   CopyControl<canCopy<I>>, MoveControl<canMove<I>> {
    static constexpr auto _te_kind = ModelKind::proxy;
    using _te_interface = InterfaceTag<I>;

    OwnerRoot() noexcept = default;
    template <class T, class... Args,
              std::enable_if_t<canOwn<I, T, Args...>, int> = 0>
    explicit OwnerRoot(std::in_place_type_t<T>, Args &&...args) {
        storage_.template emplace<T>(std::forward<Args>(args)...);
    }
    template <class T, class D = std::decay_t<T>,
              std::enable_if_t<canOwn<I, D, T &&>, int> = 0>
    explicit OwnerRoot(T &&object)
        : OwnerRoot(std::in_place_type<D>, std::forward<T>(object)) {}

    Abstract<I> &_te_value() & noexcept { return storage_.get(); }
    Abstract<I> const &_te_value() const & noexcept { return storage_.get(); }
    Abstract<I> &&_te_value() && noexcept { return std::move(storage_.get()); }
    Abstract<I> const &&_te_value() const && noexcept {
        return std::move(storage_.get());
    }
    OwnerStorage<I, Policy> &_te_storage() noexcept { return storage_; }
    bool _te_empty() const noexcept { return storage_.empty(); }

private:
    OwnerStorage<I, Policy> storage_;
};
} // namespace detail

template <template <class> class I, class Storage = HeapStorage,
          class Link = I<detail::AbstractRoot>>
struct Any final : detail::Compose<I, detail::OwnerRoot<I, Storage>> {
    using Base = detail::Compose<I, detail::OwnerRoot<I, Storage>>;
    using Base::Base;
    Any() noexcept = default;
    Any(Any const &) = default;
    Any(Any &&) noexcept = default;
    Any &operator=(Any const &) = default;
    Any &operator=(Any &&) noexcept = default;
};

template <template <class> class I, class Storage>
bool empty(Any<I, Storage> const &object) noexcept { return object._te_empty(); }

template <template <class> class I, class Storage>
void reset(Any<I, Storage> &object) noexcept { object._te_storage().reset(); }

template <class T, template <class> class I, class Storage, class... Args,
          std::enable_if_t<detail::canOwn<I, T, Args...>, int> = 0>
T &emplace(Any<I, Storage> &object, Args &&...args) {
    return object._te_storage().template emplace<T>(std::forward<Args>(args)...);
}

template <template <class> class I, class Storage,
          std::enable_if_t<detail::canMove<I>, int> = 0>
void swap(Any<I, Storage> &left, Any<I, Storage> &right) noexcept {
    left._te_storage().swap(right._te_storage());
}
} // namespace lutils::erasure
