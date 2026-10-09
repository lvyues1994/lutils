#pragma once

#include <functional>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace lutils::expected_detail {

struct VoidValue {};
struct ErrorTag {};
struct CopyTag {};
struct MoveTag {};
struct ConvertTag {};
struct InvokeValueTag {};
struct InvokeErrorTag {};

template <class T> using RemoveCvref = std::remove_cv_t<std::remove_reference_t<T>>;
template <class T> using StoredValue = std::conditional_t<std::is_void_v<T>, VoidValue, T>;

// Placement construction is confined to active-member transitions. It allocates no memory.
template <class T, class... Args> void construct(T *where, Args &&...args) {
    ::new (const_cast<void *>(static_cast<void const volatile *>(where)))
        T(std::forward<Args>(args)...);
}

// C++17 cannot constrain a destructor. Both union specializations need the same
// constructors, but only the second may have a user-provided destructor.
#define LUTILS_EXPECTED_UNION_BODY                                                                 \
    T value;                                                                                       \
    E error;                                                                                       \
    template <class... Args>                                                                       \
    constexpr explicit Union(std::in_place_t, Args &&...args)                                      \
        : value(std::forward<Args>(args)...) {}                                                    \
    template <class... Args>                                                                       \
    constexpr explicit Union(ErrorTag, Args &&...args) : error(std::forward<Args>(args)...) {}     \
    template <class F, class... Args>                                                              \
    constexpr explicit Union(InvokeValueTag, F &&f, Args &&...args)                                \
        : value(std::invoke(std::forward<F>(f), std::forward<Args>(args)...)) {}                   \
    template <class F, class... Args>                                                              \
    constexpr explicit Union(InvokeErrorTag, F &&f, Args &&...args)                                \
        : error(std::invoke(std::forward<F>(f), std::forward<Args>(args)...)) {}                   \
    Union(Union const &) = default;                                                                \
    Union(Union &&) = default;                                                                     \
    Union &operator=(Union const &) = default;                                                     \
    Union &operator=(Union &&) = default;

template <class T, class E,
          bool = std::is_trivially_destructible_v<T> and std::is_trivially_destructible_v<E>>
union Union {
    LUTILS_EXPECTED_UNION_BODY
    ~Union() = default;
};

template <class T, class E> union Union<T, E, false> {
    LUTILS_EXPECTED_UNION_BODY
    ~Union() {}
};
#undef LUTILS_EXPECTED_UNION_BODY

// Exactly one member is alive at each public operation boundary. The discriminant
// is updated only after construction succeeds. Union prvalues also let converting
// constructors initialize immovable alternatives without a transient empty state.
template <class T, class E> struct Storage {
    template <class... Args>
    constexpr explicit Storage(std::in_place_t tag, Args &&...args)
        : data(tag, std::forward<Args>(args)...), hasValue(true) {}
    template <class... Args>
    constexpr explicit Storage(ErrorTag tag, Args &&...args)
        : data(tag, std::forward<Args>(args)...), hasValue(false) {}
    template <class... Args>
    constexpr explicit Storage(InvokeValueTag tag, Args &&...args)
        : data(tag, std::forward<Args>(args)...), hasValue(true) {}
    template <class... Args>
    constexpr explicit Storage(InvokeErrorTag tag, Args &&...args)
        : data(tag, std::forward<Args>(args)...), hasValue(false) {}
    constexpr Storage(CopyTag, Storage const &other)
        : data(other.hasValue ? Union<T, E>(std::in_place, other.data.value)
                              : Union<T, E>(ErrorTag{}, other.data.error)),
          hasValue(other.hasValue) {}
    constexpr Storage(MoveTag, Storage &&other)
        : data(other.hasValue ? Union<T, E>(std::in_place, std::move(other.data.value))
                              : Union<T, E>(ErrorTag{}, std::move(other.data.error))),
          hasValue(other.hasValue) {}
    template <class Other>
    constexpr Storage(ConvertTag, Other &&other)
        : data(other.has_value() ? convertedValue(std::forward<Other>(other))
                                 : Union<T, E>(ErrorTag{}, std::forward<Other>(other).error())),
          hasValue(other.has_value()) {}

    void destroy() noexcept {
        if (hasValue)
            data.value.~T();
        else
            data.error.~E();
    }

    template <class Arg> void assignValue(Arg &&arg) {
        if (hasValue)
            data.value = std::forward<Arg>(arg);
        else {
            reinitialize(std::addressof(data.value), std::addressof(data.error),
                         std::forward<Arg>(arg));
            hasValue = true;
        }
    }
    template <class Arg> void assignError(Arg &&arg) {
        if (not hasValue)
            data.error = std::forward<Arg>(arg);
        else {
            reinitialize(std::addressof(data.error), std::addressof(data.value),
                         std::forward<Arg>(arg));
            hasValue = false;
        }
    }
    template <class Other> void assign(Other &&other) {
        if (other.hasValue)
            assignValue(std::forward<Other>(other).data.value);
        else
            assignError(std::forward<Other>(other).data.error);
    }

    Union<T, E> data;
    bool hasValue;

  private:
    template <class Other> static constexpr Union<T, E> convertedValue(Other &&other) {
        if constexpr (std::is_same_v<T, VoidValue>)
            return Union<T, E>(std::in_place);
        else
            return Union<T, E>(std::in_place, std::forward<Other>(other).operator*());
    }

    template <class New, class Old, class... Args>
    static void reinitialize(New *next, Old *old, Args &&...args) {
        if constexpr (std::is_nothrow_constructible_v<New, Args...>) {
            old->~Old();
            expected_detail::construct(next, std::forward<Args>(args)...);
        } else if constexpr (std::is_nothrow_move_constructible_v<New>) {
            New temporary(std::forward<Args>(args)...);
            old->~Old();
            expected_detail::construct(next, std::move(temporary));
        } else {
            static_assert(std::is_nothrow_move_constructible_v<Old>);
            Old temporary(std::move(*old));
            old->~Old();
            try {
                expected_detail::construct(next, std::forward<Args>(args)...);
            } catch (...) {
                expected_detail::construct(old, std::move(temporary));
                throw;
            }
        }
    }
};

template <class T, class E,
          bool = std::is_trivially_destructible_v<T> and std::is_trivially_destructible_v<E>>
struct Destroy : Storage<T, E> {
    using Storage<T, E>::Storage;
};
template <class T, class E> struct Destroy<T, E, false> : Storage<T, E> {
    using Storage<T, E>::Storage;
    Destroy(Destroy const &) = default;
    Destroy(Destroy &&) = default;
    Destroy &operator=(Destroy const &) = default;
    Destroy &operator=(Destroy &&) = default;
    ~Destroy() { this->destroy(); }
};

// Separate layers make the C++17 type traits reflect deletion and triviality.
// A constructor template cannot substitute for a conditional copy constructor.
template <class T, class E>
inline constexpr int copyMode =
    not(std::is_copy_constructible_v<T> and std::is_copy_constructible_v<E>)                    ? 0
    : (std::is_trivially_copy_constructible_v<T> and std::is_trivially_copy_constructible_v<E>) ? 1
                                                                                                : 2;
template <class T, class E, int = copyMode<T, E>> struct Copy : Destroy<T, E> {
    using Destroy<T, E>::Destroy;
};
template <class T, class E> struct Copy<T, E, 0> : Destroy<T, E> {
    using Destroy<T, E>::Destroy;
    Copy(Copy const &) = delete;
    Copy(Copy &&) = default;
    Copy &operator=(Copy const &) = default;
    Copy &operator=(Copy &&) = default;
};
template <class T, class E> struct Copy<T, E, 2> : Destroy<T, E> {
    using Base = Destroy<T, E>;
    using Base::Base;
    constexpr Copy(Copy const &other) noexcept(std::is_nothrow_copy_constructible_v<T> and
                                               std::is_nothrow_copy_constructible_v<E>)
        : Base(CopyTag{}, other) {}
    Copy(Copy &&) = default;
    Copy &operator=(Copy const &) = default;
    Copy &operator=(Copy &&) = default;
};

template <class T, class E>
inline constexpr int moveMode =
    not(std::is_move_constructible_v<T> and std::is_move_constructible_v<E>)                    ? 0
    : (std::is_trivially_move_constructible_v<T> and std::is_trivially_move_constructible_v<E>) ? 1
                                                                                                : 2;
template <class T, class E, int = moveMode<T, E>> struct Move : Copy<T, E> {
    using Copy<T, E>::Copy;
};
template <class T, class E> struct Move<T, E, 0> : Copy<T, E> {
    using Copy<T, E>::Copy;
    Move(Move const &) = default;
    Move(Move &&) = delete;
    Move &operator=(Move const &) = default;
    Move &operator=(Move &&) = default;
};
template <class T, class E> struct Move<T, E, 2> : Copy<T, E> {
    using Base = Copy<T, E>;
    using Base::Base;
    Move(Move const &) = default;
    constexpr Move(Move &&other) noexcept(std::is_nothrow_move_constructible_v<T> and
                                          std::is_nothrow_move_constructible_v<E>)
        : Base(MoveTag{}, std::move(other)) {}
    Move &operator=(Move const &) = default;
    Move &operator=(Move &&) = default;
};

template <class T, class E>
inline constexpr bool canCopyAssign =
    std::is_copy_constructible_v<T> and std::is_copy_constructible_v<E> and
    std::is_copy_assignable_v<T> and std::is_copy_assignable_v<E> and
    (std::is_nothrow_move_constructible_v<T> or std::is_nothrow_move_constructible_v<E>);
template <class T, class E, bool = canCopyAssign<T, E>> struct CopyAssign : Move<T, E> {
    using Move<T, E>::Move;
    CopyAssign(CopyAssign const &) = default;
    CopyAssign(CopyAssign &&) = default;
    CopyAssign &operator=(CopyAssign const &other) {
        this->assign(other);
        return *this;
    }
    CopyAssign &operator=(CopyAssign &&) = default;
};
template <class T, class E> struct CopyAssign<T, E, false> : Move<T, E> {
    using Move<T, E>::Move;
    CopyAssign(CopyAssign const &) = default;
    CopyAssign(CopyAssign &&) = default;
    CopyAssign &operator=(CopyAssign const &) = delete;
    CopyAssign &operator=(CopyAssign &&) = default;
};

template <class T, class E>
inline constexpr bool canMoveAssign =
    std::is_move_constructible_v<T> and std::is_move_constructible_v<E> and
    std::is_move_assignable_v<T> and std::is_move_assignable_v<E> and
    (std::is_nothrow_move_constructible_v<T> or std::is_nothrow_move_constructible_v<E>);
template <class T, class E, bool = canMoveAssign<T, E>> struct MoveAssign : CopyAssign<T, E> {
    using CopyAssign<T, E>::CopyAssign;
    MoveAssign(MoveAssign const &) = default;
    MoveAssign(MoveAssign &&) = default;
    MoveAssign &operator=(MoveAssign const &) = default;
    MoveAssign &operator=(MoveAssign &&other) noexcept(std::is_nothrow_move_constructible_v<T> and
                                                       std::is_nothrow_move_constructible_v<E> and
                                                       std::is_nothrow_move_assignable_v<T> and
                                                       std::is_nothrow_move_assignable_v<E>) {
        this->assign(std::move(other));
        return *this;
    }
};
template <class T, class E> struct MoveAssign<T, E, false> : CopyAssign<T, E> {
    using CopyAssign<T, E>::CopyAssign;
    MoveAssign(MoveAssign const &) = default;
    MoveAssign(MoveAssign &&) = default;
    MoveAssign &operator=(MoveAssign const &) = default;
    MoveAssign &operator=(MoveAssign &&) = delete;
};

// The caller supplies a nothrow-movable B. On failure A remains alive (possibly
// moved from), and B is restored, so both enclosing expected objects stay valid.
template <class A, class B> void crossSwap(A *a, B *b, A *newA, B *newB) {
    static_assert(std::is_nothrow_move_constructible_v<B>);
    B saved(std::move(*b));
    b->~B();
    if constexpr (std::is_nothrow_move_constructible_v<A>) {
        expected_detail::construct(newA, std::move(*a));
    } else {
        try {
            expected_detail::construct(newA, std::move(*a));
        } catch (...) {
            expected_detail::construct(b, std::move(saved));
            throw;
        }
    }
    a->~A();
    expected_detail::construct(newB, std::move(saved));
}

} // namespace lutils::expected_detail
