#pragma once

#include <cassert>
#include <exception>
#include <initializer_list>
#include <lutils/detail/ExpectedStorage.hpp>

namespace lutils {

template <class T, class E> struct expected;
template <class E> struct unexpected;

struct unexpect_t {
    explicit unexpect_t() = default;
};
inline constexpr unexpect_t unexpect{};

namespace expected_detail {
template <class T> inline constexpr bool isUnexpected = false;
template <class E> inline constexpr bool isUnexpected<unexpected<E>> = true;
template <class T> inline constexpr bool isExpected = false;
template <class T, class E> inline constexpr bool isExpected<expected<T, E>> = true;
template <class E>
inline constexpr bool validError =
    std::is_object_v<E> and not std::is_array_v<E> and not std::is_const_v<E> and
    not std::is_volatile_v<E> and not isUnexpected<E>;
template <class T>
inline constexpr bool validValue =
    (std::is_void_v<T> or (std::is_object_v<T> and not std::is_array_v<T>)) and
    not std::is_same_v<std::remove_cv_t<T>, std::in_place_t> and
    not std::is_same_v<std::remove_cv_t<T>, unexpect_t> and not isUnexpected<std::remove_cv_t<T>>;

template <class To, class From>
inline constexpr bool convertsFromAny =
    std::is_constructible_v<To, From &> or std::is_constructible_v<To, From const &> or
    std::is_constructible_v<To, From &&> or std::is_constructible_v<To, From const &&> or
    std::is_convertible_v<From &, To> or std::is_convertible_v<From const &, To> or
    std::is_convertible_v<From &&, To> or std::is_convertible_v<From const &&, To>;
} // namespace expected_detail

template <class E> struct unexpected {
    static_assert(expected_detail::validError<E>,
                  "unexpected requires an unqualified object error type");
    static_assert(std::is_nothrow_destructible_v<E>,
                  "unexpected requires a nonthrowing destructor");

    unexpected(unexpected const &) = default;
    unexpected(unexpected &&) = default;
    unexpected &operator=(unexpected const &) = default;
    unexpected &operator=(unexpected &&) = default;

    template <class Err = E,
              std::enable_if_t<
                  not std::is_same_v<expected_detail::RemoveCvref<Err>, unexpected> and
                      not std::is_same_v<expected_detail::RemoveCvref<Err>, std::in_place_t> and
                      std::is_constructible_v<E, Err>,
                  int> = 0>
    constexpr explicit unexpected(Err &&error) noexcept(std::is_nothrow_constructible_v<E, Err>)
        : storedError(std::forward<Err>(error)) {}
    template <class... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
    constexpr explicit unexpected(std::in_place_t, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<E, Args...>)
        : storedError(std::forward<Args>(args)...) {}
    template <
        class U, class... Args,
        std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U> &, Args...>, int> = 0>
    constexpr explicit unexpected(
        std::in_place_t, std::initializer_list<U> il,
        Args &&...args) noexcept(std::is_nothrow_constructible_v<E, std::initializer_list<U> &,
                                                                 Args...>)
        : storedError(il, std::forward<Args>(args)...) {}

    constexpr E &error() & noexcept { return storedError; }
    constexpr E const &error() const & noexcept { return storedError; }
    constexpr E &&error() && noexcept { return std::move(storedError); }
    constexpr E const &&error() const && noexcept { return std::move(storedError); }

    void swap(unexpected &other) noexcept(std::is_nothrow_swappable_v<E>) {
        static_assert(std::is_swappable_v<E>, "unexpected::swap requires a swappable error");
        using std::swap;
        swap(storedError, other.storedError);
    }

  private:
    E storedError;
};
template <class E> unexpected(E) -> unexpected<E>;

template <class E> struct bad_expected_access;
template <> struct bad_expected_access<void> : std::exception {
    char const *what() const noexcept override { return "bad access to lutils::expected"; }

  protected:
    bad_expected_access() noexcept = default;
    bad_expected_access(bad_expected_access const &) = default;
    bad_expected_access(bad_expected_access &&) = default;
    bad_expected_access &operator=(bad_expected_access const &) = default;
    bad_expected_access &operator=(bad_expected_access &&) = default;
    ~bad_expected_access() override = default;
};
template <class E> struct bad_expected_access : bad_expected_access<void> {
    explicit bad_expected_access(E error) : storedError(std::move(error)) {}
    E &error() & noexcept { return storedError; }
    E const &error() const & noexcept { return storedError; }
    E &&error() && noexcept { return std::move(storedError); }
    E const &&error() const && noexcept { return std::move(storedError); }

  private:
    E storedError;
};

// C++17 backport of the C++23 expected API. An empty placeholder
// lets the same lifetime machinery cover all cv-void value types.
template <class T, class E>
struct [[nodiscard]] expected
    : private expected_detail::MoveAssign<expected_detail::StoredValue<T>, E> {
    static_assert(expected_detail::validValue<T>, "expected requires an object or void value type");
    static_assert(expected_detail::validError<E>,
                  "expected requires an unqualified object error type");
    static_assert(std::is_void_v<T> or std::is_nothrow_destructible_v<T>,
                  "expected requires a nonthrowing value destructor");
    static_assert(std::is_nothrow_destructible_v<E>,
                  "expected requires a nonthrowing error destructor");

  private:
    using V = expected_detail::StoredValue<T>;
    using Base = expected_detail::MoveAssign<V, E>;
    template <class, class> friend struct expected;

    template <class U>
    static constexpr bool valueArgument =
        not std::is_void_v<T> and
        not std::is_same_v<expected_detail::RemoveCvref<U>, std::in_place_t> and
        not std::is_same_v<expected_detail::RemoveCvref<U>, expected> and
        not expected_detail::isUnexpected<expected_detail::RemoveCvref<U>> and
        not(std::is_same_v<std::remove_cv_t<T>, bool> and
            expected_detail::isExpected<expected_detail::RemoveCvref<U>>) and
        std::is_constructible_v<T, U>;

    template <class U, class G, class UF, class GF>
    static constexpr bool convertsExpected =
        std::is_constructible_v<E, GF> and
        not expected_detail::convertsFromAny<unexpected<E>, expected<U, G>> and
        ((std::is_void_v<T> and std::is_void_v<U>) or
         (not std::is_void_v<T> and std::is_constructible_v<T, UF> and
          (std::is_same_v<std::remove_cv_t<T>, bool> or
           not expected_detail::convertsFromAny<T, expected<U, G>>)));

  public:
    using value_type = T;
    using error_type = E;
    using unexpected_type = unexpected<E>;
    template <class U> using rebind = expected<U, E>;

    template <class U = T,
              std::enable_if_t<std::is_void_v<U> or std::is_default_constructible_v<U>, int> = 0>
    constexpr expected() noexcept(std::is_nothrow_default_constructible_v<V>)
        : Base(std::in_place) {}
    expected(expected const &) = default;
    expected(expected &&) = default;
    expected &operator=(expected const &) = default;
    expected &operator=(expected &&) = default;
    ~expected() = default;

    // Unlike the non-void overload, [expected.void.assign] requires deletion,
    // rather than removal from overload resolution, when E cannot be moved.
    template <class U = T, std::enable_if_t<std::is_same_v<U, T> and std::is_void_v<T> and
                                                not expected_detail::canMoveAssign<V, E>,
                                            int> = 0>
    expected &operator=(expected &&) = delete;

    // C++17 spells conditional explicit with two mutually exclusive overloads.
    template <class U = T,
              std::enable_if_t<valueArgument<U> and std::is_convertible_v<U, T>, int> = 0>
    constexpr expected(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : Base(std::in_place, std::forward<U>(value)) {}
    template <class U = T,
              std::enable_if_t<valueArgument<U> and not std::is_convertible_v<U, T>, int> = 0>
    constexpr explicit expected(U &&value) noexcept(std::is_nothrow_constructible_v<T, U>)
        : Base(std::in_place, std::forward<U>(value)) {}

    template <
        class U, class G,
        std::enable_if_t<
            convertsExpected<U, G, std::add_lvalue_reference_t<std::add_const_t<U>>, G const &> and
                ((std::is_void_v<T> or
                  std::is_convertible_v<std::add_lvalue_reference_t<std::add_const_t<U>>, T>) and
                 std::is_convertible_v<G const &, E>),
            int> = 0>
    constexpr expected(expected<U, G> const &other) noexcept(
        (std::is_void_v<T> or
         std::is_nothrow_constructible_v<T, std::add_lvalue_reference_t<std::add_const_t<U>>>) and
        std::is_nothrow_constructible_v<E, G const &>)
        : Base(expected_detail::ConvertTag{}, other) {}

    template <
        class U, class G,
        std::enable_if_t<
            convertsExpected<U, G, std::add_lvalue_reference_t<std::add_const_t<U>>, G const &> and
                not((std::is_void_v<T> or
                     std::is_convertible_v<std::add_lvalue_reference_t<std::add_const_t<U>>, T>) and
                    std::is_convertible_v<G const &, E>),
            int> = 0>
    constexpr explicit expected(expected<U, G> const &other) noexcept(
        (std::is_void_v<T> or
         std::is_nothrow_constructible_v<T, std::add_lvalue_reference_t<std::add_const_t<U>>>) and
        std::is_nothrow_constructible_v<E, G const &>)
        : Base(expected_detail::ConvertTag{}, other) {}

    template <class U, class G,
              std::enable_if_t<convertsExpected<U, G, U, G> and
                                   ((std::is_void_v<T> or std::is_convertible_v<U, T>) and
                                    std::is_convertible_v<G, E>),
                               int> = 0>
    constexpr expected(expected<U, G> &&other) noexcept((std::is_void_v<T> or
                                                         std::is_nothrow_constructible_v<T, U>) and
                                                        std::is_nothrow_constructible_v<E, G>)
        : Base(expected_detail::ConvertTag{}, std::move(other)) {}

    template <class U, class G,
              std::enable_if_t<convertsExpected<U, G, U, G> and
                                   not((std::is_void_v<T> or std::is_convertible_v<U, T>) and
                                       std::is_convertible_v<G, E>),
                               int> = 0>
    constexpr explicit expected(expected<U, G> &&other) noexcept(
        (std::is_void_v<T> or std::is_nothrow_constructible_v<T, U>) and
        std::is_nothrow_constructible_v<E, G>)
        : Base(expected_detail::ConvertTag{}, std::move(other)) {}

    template <class G, std::enable_if_t<std::is_constructible_v<E, G const &> and
                                            std::is_convertible_v<G const &, E>,
                                        int> = 0>
    constexpr expected(unexpected<G> const &other) noexcept(
        std::is_nothrow_constructible_v<E, G const &>)
        : Base(expected_detail::ErrorTag{}, other.error()) {}

    template <class G, std::enable_if_t<std::is_constructible_v<E, G const &> and
                                            not std::is_convertible_v<G const &, E>,
                                        int> = 0>
    constexpr explicit expected(unexpected<G> const &other) noexcept(
        std::is_nothrow_constructible_v<E, G const &>)
        : Base(expected_detail::ErrorTag{}, other.error()) {}

    template <class G, std::enable_if_t<
                           std::is_constructible_v<E, G> and std::is_convertible_v<G, E>, int> = 0>
    constexpr expected(unexpected<G> &&other) noexcept(std::is_nothrow_constructible_v<E, G>)
        : Base(expected_detail::ErrorTag{}, std::move(other).error()) {}

    template <class G,
              std::enable_if_t<std::is_constructible_v<E, G> and not std::is_convertible_v<G, E>,
                               int> = 0>
    constexpr explicit expected(unexpected<G> &&other) noexcept(
        std::is_nothrow_constructible_v<E, G>)
        : Base(expected_detail::ErrorTag{}, std::move(other).error()) {}

    template <class... Args, std::enable_if_t<(std::is_void_v<T> and sizeof...(Args) == 0) or
                                                  std::is_constructible_v<T, Args...>,
                                              int> = 0>
    constexpr explicit expected(std::in_place_t tag, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<V, Args...>)
        : Base(tag, std::forward<Args>(args)...) {}
    template <
        class U, class... Args,
        std::enable_if_t<std::is_constructible_v<T, std::initializer_list<U> &, Args...>, int> = 0>
    constexpr explicit expected(
        std::in_place_t tag, std::initializer_list<U> il,
        Args &&...args) noexcept(std::is_nothrow_constructible_v<T, std::initializer_list<U> &,
                                                                 Args...>)
        : Base(tag, il, std::forward<Args>(args)...) {}
    template <class... Args, std::enable_if_t<std::is_constructible_v<E, Args...>, int> = 0>
    constexpr explicit expected(unexpect_t, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<E, Args...>)
        : Base(expected_detail::ErrorTag{}, std::forward<Args>(args)...) {}
    template <
        class U, class... Args,
        std::enable_if_t<std::is_constructible_v<E, std::initializer_list<U> &, Args...>, int> = 0>
    constexpr explicit expected(unexpect_t, std::initializer_list<U> il, Args &&...args) noexcept(
        std::is_nothrow_constructible_v<E, std::initializer_list<U> &, Args...>)
        : Base(expected_detail::ErrorTag{}, il, std::forward<Args>(args)...) {}

    template <
        class U = T,
        std::enable_if_t<not std::is_void_v<T> and
                             not std::is_same_v<expected_detail::RemoveCvref<U>, expected> and
                             not expected_detail::isUnexpected<expected_detail::RemoveCvref<U>> and
                             std::is_constructible_v<T, U> and
                             std::is_assignable_v<std::add_lvalue_reference_t<T>, U> and
                             (std::is_nothrow_constructible_v<T, U> or
                              std::is_nothrow_move_constructible_v<T> or
                              std::is_nothrow_move_constructible_v<E>),
                         int> = 0>
    expected &operator=(U &&value) {
        this->assignValue(std::forward<U>(value));
        return *this;
    }

    template <class G, std::enable_if_t<std::is_constructible_v<E, G const &> and
                                            std::is_assignable_v<E &, G const &> and
                                            (std::is_nothrow_constructible_v<E, G const &> or
                                             std::is_nothrow_move_constructible_v<V> or
                                             std::is_nothrow_move_constructible_v<E>),
                                        int> = 0>
    expected &operator=(unexpected<G> const &other) {
        this->assignError(other.error());
        return *this;
    }

    template <class G,
              std::enable_if_t<std::is_constructible_v<E, G> and std::is_assignable_v<E &, G> and
                                   (std::is_nothrow_constructible_v<E, G> or
                                    std::is_nothrow_move_constructible_v<V> or
                                    std::is_nothrow_move_constructible_v<E>),
                               int> = 0>
    expected &operator=(unexpected<G> &&other) {
        this->assignError(std::move(other).error());
        return *this;
    }

    template <class... Args, std::enable_if_t<(std::is_void_v<T> and sizeof...(Args) == 0) or
                                                  std::is_nothrow_constructible_v<T, Args...>,
                                              int> = 0>
    decltype(auto) emplace(Args &&...args) noexcept {
        return emplaceValue(std::forward<Args>(args)...);
    }
    template <class U, class... Args,
              std::enable_if_t<
                  std::is_nothrow_constructible_v<T, std::initializer_list<U> &, Args...>, int> = 0>
    std::add_lvalue_reference_t<T> emplace(std::initializer_list<U> il, Args &&...args) noexcept {
        return emplaceValue(il, std::forward<Args>(args)...);
    }

    template <class U = V,
              std::enable_if_t<std::is_same_v<U, V> and std::is_swappable_v<U> and
                                   std::is_swappable_v<E> and std::is_move_constructible_v<U> and
                                   std::is_move_constructible_v<E> and
                                   (std::is_nothrow_move_constructible_v<U> or
                                    std::is_nothrow_move_constructible_v<E>),
                               int> = 0>
    void swap(expected &other) noexcept(std::is_nothrow_move_constructible_v<V> and
                                        std::is_nothrow_move_constructible_v<E> and
                                        std::is_nothrow_swappable_v<V> and
                                        std::is_nothrow_swappable_v<E>) {
        if (this == std::addressof(other))
            return;
        using std::swap;
        if (this->hasValue and other.hasValue)
            swap(this->data.value, other.data.value);
        else if (not this->hasValue and not other.hasValue)
            swap(this->data.error, other.data.error);
        else if (this->hasValue)
            swapValueError(other);
        else
            other.swapValueError(*this);
    }

    constexpr explicit operator bool() const noexcept { return this->hasValue; }
    constexpr bool has_value() const noexcept { return this->hasValue; }

    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr U *operator->() noexcept {
        assert(has_value());
        return std::addressof(this->data.value);
    }
    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr U const *operator->() const noexcept {
        assert(has_value());
        return std::addressof(this->data.value);
    }

    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) operator*() & noexcept {
        assert(has_value());
        if constexpr (not std::is_void_v<T>)
            return (this->data.value);
    }
    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) value() & {
        static_assert(std::is_copy_constructible_v<E>,
                      "value() requires a copyable error and construction from this qualifier");
        if (not has_value())
            throw bad_expected_access<E>(static_cast<E const &>(this->data.error));
        if constexpr (not std::is_void_v<T>)
            return (this->data.value);
    }
    constexpr E &error() & noexcept {
        assert(not has_value());
        return (this->data.error);
    }

    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) operator*() const & noexcept {
        assert(has_value());
        if constexpr (not std::is_void_v<T>)
            return (this->data.value);
    }
    constexpr decltype(auto) value() const & {
        static_assert(std::is_copy_constructible_v<E>,
                      "value() requires a copyable error and construction from this qualifier");
        if (not has_value())
            throw bad_expected_access<E>(static_cast<E const &>(this->data.error));
        if constexpr (not std::is_void_v<T>)
            return (this->data.value);
    }
    constexpr E const &error() const & noexcept {
        assert(not has_value());
        return (this->data.error);
    }

    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) operator*() && noexcept {
        assert(has_value());
        if constexpr (not std::is_void_v<T>)
            return std::move(this->data.value);
    }
    constexpr decltype(auto) value() && {
        static_assert(std::is_copy_constructible_v<E> and std::is_constructible_v<E, E &&>,
                      "value() requires a copyable error and construction from this qualifier");
        if (not has_value())
            throw bad_expected_access<E>(std::move(this->data.error));
        if constexpr (not std::is_void_v<T>)
            return std::move(this->data.value);
    }
    constexpr E &&error() && noexcept {
        assert(not has_value());
        return std::move(this->data.error);
    }

    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) operator*() const && noexcept {
        assert(has_value());
        if constexpr (not std::is_void_v<T>)
            return std::move(this->data.value);
    }
    template <class U = T,
              std::enable_if_t<std::is_same_v<U, T> and not std::is_void_v<U>, int> = 0>
    constexpr decltype(auto) value() const && {
        static_assert(std::is_copy_constructible_v<E> and std::is_constructible_v<E, E const &&>,
                      "value() requires a copyable error and construction from this qualifier");
        if (not has_value())
            throw bad_expected_access<E>(std::move(this->data.error));
        if constexpr (not std::is_void_v<T>)
            return std::move(this->data.value);
    }
    constexpr E const &&error() const && noexcept {
        assert(not has_value());
        return std::move(this->data.error);
    }

    template <class U = T, std::enable_if_t<std::is_same_v<U, T> and std::is_void_v<U>, int> = 0>
    constexpr void operator*() const noexcept {
        assert(has_value());
    }

    template <class U = std::remove_cv_t<T>, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and not std::is_void_v<Q>, int> = 0>
    constexpr T value_or(U &&fallback) const & {
        static_assert(std::is_copy_constructible_v<T> and std::is_convertible_v<U, T>,
                      "value_or requires a copyable value and convertible fallback");
        return has_value() ? this->data.value : static_cast<T>(std::forward<U>(fallback));
    }
    template <class U = std::remove_cv_t<T>, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and not std::is_void_v<Q>, int> = 0>
    constexpr T value_or(U &&fallback) && {
        static_assert(std::is_move_constructible_v<T> and std::is_convertible_v<U, T>,
                      "value_or requires a movable value and convertible fallback");
        return has_value() ? std::move(this->data.value)
                           : static_cast<T>(std::forward<U>(fallback));
    }
    template <class G = E> constexpr E error_or(G &&fallback) const & {
        static_assert(std::is_copy_constructible_v<E> and std::is_convertible_v<G, E>,
                      "error_or requires a copyable error and convertible fallback");
        return has_value() ? static_cast<E>(std::forward<G>(fallback)) : this->data.error;
    }
    template <class G = E> constexpr E error_or(G &&fallback) && {
        static_assert(std::is_move_constructible_v<E> and std::is_convertible_v<G, E>,
                      "error_or requires a movable error and convertible fallback");
        return has_value() ? static_cast<E>(std::forward<G>(fallback))
                           : std::move(this->data.error);
    }

    template <class F, class Q = E,
              std::enable_if_t<std::is_same_v<Q, E> and
                                   std::is_constructible_v<Q, std::add_lvalue_reference_t<E>>,
                               int> = 0>
    constexpr auto and_then(F &&f) & {
        return andThen(*this, std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<
                  std::is_same_v<Q, E> and
                      std::is_constructible_v<Q, std::add_lvalue_reference_t<std::add_const_t<E>>>,
                  int> = 0>
    constexpr auto and_then(F &&f) const & {
        return andThen(*this, std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<std::is_same_v<Q, E> and
                                   std::is_constructible_v<Q, std::add_rvalue_reference_t<E>>,
                               int> = 0>
    constexpr auto and_then(F &&f) && {
        return andThen(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<
                  std::is_same_v<Q, E> and
                      std::is_constructible_v<Q, std::add_rvalue_reference_t<std::add_const_t<E>>>,
                  int> = 0>
    constexpr auto and_then(F &&f) const && {
        return andThen(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<Q, std::add_lvalue_reference_t<T>>),
                               int> = 0>
    constexpr auto or_else(F &&f) & {
        return orElse(*this, std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<
                                        Q, std::add_lvalue_reference_t<std::add_const_t<T>>>),
                               int> = 0>
    constexpr auto or_else(F &&f) const & {
        return orElse(*this, std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<Q, std::add_rvalue_reference_t<T>>),
                               int> = 0>
    constexpr auto or_else(F &&f) && {
        return orElse(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<
                                        Q, std::add_rvalue_reference_t<std::add_const_t<T>>>),
                               int> = 0>
    constexpr auto or_else(F &&f) const && {
        return orElse(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<std::is_same_v<Q, E> and
                                   std::is_constructible_v<Q, std::add_lvalue_reference_t<E>>,
                               int> = 0>
    constexpr auto transform(F &&f) & {
        return transformValue(*this, std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<
                  std::is_same_v<Q, E> and
                      std::is_constructible_v<Q, std::add_lvalue_reference_t<std::add_const_t<E>>>,
                  int> = 0>
    constexpr auto transform(F &&f) const & {
        return transformValue(*this, std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<std::is_same_v<Q, E> and
                                   std::is_constructible_v<Q, std::add_rvalue_reference_t<E>>,
                               int> = 0>
    constexpr auto transform(F &&f) && {
        return transformValue(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = E,
              std::enable_if_t<
                  std::is_same_v<Q, E> and
                      std::is_constructible_v<Q, std::add_rvalue_reference_t<std::add_const_t<E>>>,
                  int> = 0>
    constexpr auto transform(F &&f) const && {
        return transformValue(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<Q, std::add_lvalue_reference_t<T>>),
                               int> = 0>
    constexpr auto transform_error(F &&f) & {
        return transformError(*this, std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<
                                        Q, std::add_lvalue_reference_t<std::add_const_t<T>>>),
                               int> = 0>
    constexpr auto transform_error(F &&f) const & {
        return transformError(*this, std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<Q, std::add_rvalue_reference_t<T>>),
                               int> = 0>
    constexpr auto transform_error(F &&f) && {
        return transformError(std::move(*this), std::forward<F>(f));
    }

    template <class F, class Q = T,
              std::enable_if_t<std::is_same_v<Q, T> and
                                   (std::is_void_v<Q> or
                                    std::is_constructible_v<
                                        Q, std::add_rvalue_reference_t<std::add_const_t<T>>>),
                               int> = 0>
    constexpr auto transform_error(F &&f) const && {
        return transformError(std::move(*this), std::forward<F>(f));
    }

  private:
    template <class... Args> decltype(auto) emplaceValue(Args &&...args) noexcept {
        this->destroy();
        expected_detail::construct(std::addressof(this->data.value), std::forward<Args>(args)...);
        this->hasValue = true;
        if constexpr (not std::is_void_v<T>)
            return (this->data.value);
    }

    template <class F, class... Args>
    constexpr explicit expected(expected_detail::InvokeValueTag tag, F &&f, Args &&...args)
        : Base(tag, std::forward<F>(f), std::forward<Args>(args)...) {}
    template <class F, class... Args>
    constexpr explicit expected(expected_detail::InvokeErrorTag tag, F &&f, Args &&...args)
        : Base(tag, std::forward<F>(f), std::forward<Args>(args)...) {}

    void swapValueError(expected &other) {
        if constexpr (std::is_nothrow_move_constructible_v<E>) {
            expected_detail::crossSwap(
                std::addressof(this->data.value), std::addressof(other.data.error),
                std::addressof(other.data.value), std::addressof(this->data.error));
        } else {
            expected_detail::crossSwap(
                std::addressof(other.data.error), std::addressof(this->data.value),
                std::addressof(this->data.error), std::addressof(other.data.value));
        }
        this->hasValue = false;
        other.hasValue = true;
    }

    template <class Self, class F> static constexpr decltype(auto) invokeValue(Self &&self, F &&f) {
        if constexpr (std::is_void_v<T>)
            return std::invoke(std::forward<F>(f));
        else
            return std::invoke(std::forward<F>(f), std::forward<Self>(self).data.value);
    }

    template <class Self, class F> static constexpr auto andThen(Self &&self, F &&f) {
        using R = expected_detail::RemoveCvref<decltype(invokeValue(std::forward<Self>(self),
                                                                    std::forward<F>(f)))>;
        static_assert(expected_detail::isExpected<R>, "and_then must return lutils::expected");
        static_assert(std::is_same_v<typename R::error_type, E>,
                      "and_then must preserve error_type");
        if (self.has_value())
            return invokeValue(std::forward<Self>(self), std::forward<F>(f));
        return R(unexpect, std::forward<Self>(self).data.error);
    }

    template <class Self, class F> static constexpr auto orElse(Self &&self, F &&f) {
        using R = expected_detail::RemoveCvref<
            std::invoke_result_t<F, decltype((std::forward<Self>(self).data.error))>>;
        static_assert(expected_detail::isExpected<R>, "or_else must return lutils::expected");
        static_assert(std::is_same_v<typename R::value_type, T>,
                      "or_else must preserve value_type");
        if (not self.has_value())
            return std::invoke(std::forward<F>(f), std::forward<Self>(self).data.error);
        if constexpr (std::is_void_v<T>)
            return R();
        else
            return R(std::in_place, std::forward<Self>(self).data.value);
    }

    template <class Self, class F> static constexpr auto transformValue(Self &&self, F &&f) {
        using U =
            std::remove_cv_t<decltype(invokeValue(std::forward<Self>(self), std::forward<F>(f)))>;
        static_assert(expected_detail::validValue<U>, "transform must return an object or void");
        using R = expected<U, E>;
        if (not self.has_value())
            return R(unexpect, std::forward<Self>(self).data.error);
        if constexpr (std::is_void_v<U>) {
            invokeValue(std::forward<Self>(self), std::forward<F>(f));
            return R();
        } else if constexpr (std::is_void_v<T>) {
            return R(expected_detail::InvokeValueTag{}, std::forward<F>(f));
        } else {
            return R(expected_detail::InvokeValueTag{}, std::forward<F>(f),
                     std::forward<Self>(self).data.value);
        }
    }

    template <class Self, class F> static constexpr auto transformError(Self &&self, F &&f) {
        using G = std::remove_cv_t<
            std::invoke_result_t<F, decltype((std::forward<Self>(self).data.error))>>;
        static_assert(expected_detail::validError<G>,
                      "transform_error must return an unqualified object");
        using R = expected<T, G>;
        if (not self.has_value())
            return R(expected_detail::InvokeErrorTag{}, std::forward<F>(f),
                     std::forward<Self>(self).data.error);
        if constexpr (std::is_void_v<T>)
            return R();
        else
            return R(std::in_place, std::forward<Self>(self).data.value);
    }
};

template <class E, class G>
constexpr bool operator==(unexpected<E> const &left, unexpected<G> const &right) {
    return left.error() == right.error();
}
template <class E, class G>
constexpr bool operator!=(unexpected<E> const &left, unexpected<G> const &right) {
    return not(left == right);
}
template <class E, std::enable_if_t<std::is_swappable_v<E>, int> = 0>
void swap(unexpected<E> &left, unexpected<E> &right) noexcept(noexcept(left.swap(right))) {
    left.swap(right);
}

template <class T, class E, class U, class G,
          std::enable_if_t<std::is_void_v<T> == std::is_void_v<U>, int> = 0>
constexpr bool operator==(expected<T, E> const &left, expected<U, G> const &right) {
    if (left.has_value() != right.has_value())
        return false;
    if (not left.has_value())
        return left.error() == right.error();
    if constexpr (std::is_void_v<T>)
        return true;
    else
        return left.operator*() == right.operator*();
}
template <class T, class E, class U, class G,
          std::enable_if_t<std::is_void_v<T> == std::is_void_v<U>, int> = 0>
constexpr bool operator!=(expected<T, E> const &left, expected<U, G> const &right) {
    return not(left == right);
}
template <class T, class E, class U,
          std::enable_if_t<not std::is_void_v<T> and not expected_detail::isUnexpected<U>, int> = 0>
constexpr bool operator==(expected<T, E> const &left, U const &right) {
    return left.has_value() and left.operator*() == right;
}
template <class T, class E, class U,
          std::enable_if_t<not std::is_void_v<T> and not expected_detail::isUnexpected<U>, int> = 0>
constexpr bool operator==(U const &left, expected<T, E> const &right) {
    return right == left;
}
template <class T, class E, class U,
          std::enable_if_t<not std::is_void_v<T> and not expected_detail::isUnexpected<U>, int> = 0>
constexpr bool operator!=(expected<T, E> const &left, U const &right) {
    return not(left == right);
}
template <class T, class E, class U,
          std::enable_if_t<not std::is_void_v<T> and not expected_detail::isUnexpected<U>, int> = 0>
constexpr bool operator!=(U const &left, expected<T, E> const &right) {
    return not(right == left);
}
template <class T, class E, class G>
constexpr bool operator==(expected<T, E> const &left, unexpected<G> const &right) {
    return not left.has_value() and left.error() == right.error();
}
template <class T, class E, class G>
constexpr bool operator==(unexpected<G> const &left, expected<T, E> const &right) {
    return right == left;
}
template <class T, class E, class G>
constexpr bool operator!=(expected<T, E> const &left, unexpected<G> const &right) {
    return not(left == right);
}
template <class T, class E, class G>
constexpr bool operator!=(unexpected<G> const &left, expected<T, E> const &right) {
    return not(right == left);
}
template <class T, class E>
auto swap(expected<T, E> &left, expected<T, E> &right) noexcept(noexcept(left.swap(right)))
    -> decltype(left.swap(right)) {
    left.swap(right);
}

} // namespace lutils
