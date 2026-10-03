#pragma once

#include <lutils/erasure/detail/BorrowStorage.hpp>
#include <cstddef>

namespace lutils::erasure {
namespace detail {
template <class T, template <class> class I, class = void>
struct CanBorrow : std::bool_constant<std::is_object_v<T> and
                                    not std::is_volatile_v<T> and
                                    not std::is_base_of_v<ErasedTag, T>> {};
template <class T, template <class> class I>
struct CanBorrow<T, I, std::void_t<typename T::_te_interface>> {
    template <template <class> class Source>
    static constexpr bool check(InterfaceTag<Source>) {
        return extendsInterface<Source, I>;
    }
    static constexpr bool value = not std::is_volatile_v<T> and
        check(typename T::_te_interface{});
};
} // namespace detail

template <template <class> class I, bool Const,
          class Link = I<detail::AbstractRoot>>
struct BasicAnyPtr {
    using Element = std::conditional_t<Const, detail::Abstract<I> const,
                                      detail::Abstract<I>>;

    BasicAnyPtr() noexcept = default;
    BasicAnyPtr(std::nullptr_t) noexcept {}

    template <class T, std::enable_if_t<
        detail::CanBorrow<T, I>::value and (Const or not std::is_const_v<T>),
        int> = 0>
    explicit BasicAnyPtr(T *target) noexcept {
        if (not target) { return; }
        if constexpr (std::is_base_of_v<detail::ErasedTag, T>) {
            if (target->_te_empty()) { return; }
            if constexpr (Const) { std::as_const(*target)._te_bind(storage_); }
            else { target->_te_bind(storage_); }
        } else if constexpr (Const) {
            storage_.bind(static_cast<T const *>(target));
        } else {
            storage_.bind(target);
        }
    }

    template <template <class> class Source, bool SourceConst,
              std::enable_if_t<detail::extendsInterface<Source, I> and
                               (Const or not SourceConst), int> = 0>
    explicit BasicAnyPtr(BasicAnyPtr<Source, SourceConst> const &source) noexcept {
        if (not source) { return; }
        if constexpr (Const) { std::as_const(*source)._te_bind(storage_); }
        else { source->_te_bind(storage_); }
    }

    Element *operator->() const noexcept { return storage_.get(); }
    Element &operator*() const noexcept { return *storage_.get(); }
    explicit operator bool() const noexcept { return not storage_.empty(); }
    void _te_reset() noexcept { storage_.reset(); }

private:
    detail::BorrowStorage<I> storage_;
};

template <template <class> class I>
using AnyPtr = BasicAnyPtr<I, false>;
template <template <class> class I>
using AnyConstPtr = BasicAnyPtr<I, true>;

template <template <class> class I, bool Const>
bool empty(BasicAnyPtr<I, Const> const &pointer) noexcept { return not pointer; }
template <template <class> class I, bool Const>
void reset(BasicAnyPtr<I, Const> &pointer) noexcept { pointer._te_reset(); }
template <template <class> class I, bool Const>
void swap(BasicAnyPtr<I, Const> &left, BasicAnyPtr<I, Const> &right) noexcept {
    std::swap(left, right);
}
} // namespace lutils::erasure
