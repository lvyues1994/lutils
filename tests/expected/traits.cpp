#include "Test.hpp"

using ex::expected;
using ex::unexpect;
using ex::unexpected;

using Scalar = expected<int, int>;
using Empty = expected<void, int>;
using MoveOnly = expected<std::unique_ptr<int>, int>;
using MoveError = expected<int, std::unique_ptr<int>>;
using Fixed = expected<Immovable, int>;
using FixedError = expected<int, Immovable>;

static_assert(std::is_trivially_copy_constructible_v<Scalar>);
static_assert(std::is_trivially_move_constructible_v<Scalar>);
static_assert(std::is_trivially_destructible_v<Scalar>);
static_assert(std::is_trivially_copy_constructible_v<Empty>);
static_assert(std::is_trivially_move_constructible_v<Empty>);
static_assert(std::is_trivially_destructible_v<Empty>);
static_assert(not std::is_trivially_destructible_v<expected<std::string, int>>);
static_assert(not std::is_trivially_destructible_v<expected<void, std::string>>);
static_assert(not std::is_copy_constructible_v<MoveOnly>);
static_assert(not std::is_copy_assignable_v<MoveOnly>);
static_assert(std::is_nothrow_move_constructible_v<MoveOnly>);
static_assert(std::is_nothrow_move_assignable_v<MoveOnly>);
static_assert(not std::is_copy_constructible_v<MoveError>);
static_assert(not std::is_copy_assignable_v<MoveError>);
static_assert(std::is_nothrow_move_constructible_v<MoveError>);
static_assert(std::is_nothrow_move_assignable_v<MoveError>);
static_assert(not std::is_copy_constructible_v<Fixed>);
static_assert(not std::is_move_constructible_v<Fixed>);
static_assert(not std::is_copy_assignable_v<Fixed>);
static_assert(not std::is_move_assignable_v<Fixed>);
static_assert(not std::is_copy_constructible_v<FixedError>);
static_assert(not std::is_move_constructible_v<FixedError>);
static_assert(not std::is_copy_assignable_v<FixedError>);
static_assert(not std::is_move_assignable_v<FixedError>);
static_assert(std::is_default_constructible_v<FixedError>);
static_assert(not std::is_default_constructible_v<Fixed>);
static_assert(std::is_move_constructible_v<expected<CopyOnly, int>>);
static_assert(std::is_move_assignable_v<expected<CopyOnly, int>>);
static_assert(not std::is_move_assignable_v<expected<void, CopyOnly>>);
using VoidCopyOnly = expected<void, CopyOnly>;
struct DerivedVoid : VoidCopyOnly {};
struct ConvertsVoid {
    operator VoidCopyOnly() const { return {}; }
};
static_assert(not std::is_assignable_v<VoidCopyOnly &, DerivedVoid &&>);
static_assert(not std::is_assignable_v<VoidCopyOnly &, ConvertsVoid &&>);
static_assert(not std::is_copy_assignable_v<expected<ThrowingMove, ThrowingMove>>);
static_assert(not std::is_move_assignable_v<expected<ThrowingMove, ThrowingMove>>);
static_assert(std::is_copy_assignable_v<expected<void, ThrowingMove>>);
static_assert(std::is_move_assignable_v<expected<void, ThrowingMove>>);
static_assert(not std::is_nothrow_move_constructible_v<expected<int, ThrowingMove>>);
static_assert(not std::is_nothrow_move_assignable_v<expected<int, ThrowingMove>>);
static_assert(not std::is_copy_assignable_v<expected<int const, int>>);
static_assert(not std::is_move_assignable_v<expected<int const, int>>);
static_assert(std::is_constructible_v<expected<Explicit, int>, int>);
static_assert(not std::is_convertible_v<int, expected<Explicit, int>>);
static_assert(std::is_convertible_v<int, expected<Implicit, int>>);
static_assert(not std::is_convertible_v<unexpected<int>, expected<int, Explicit>>);
static_assert(std::is_convertible_v<unexpected<int>, expected<int, Implicit>>);
static_assert(not std::is_convertible_v<expected<int, int>, expected<Explicit, int>>);
static_assert(std::is_convertible_v<expected<int, int>, expected<Implicit, int>>);
static_assert(not std::is_convertible_v<expected<void, int>, expected<void, Explicit>>);
static_assert(std::is_convertible_v<expected<void, int>, expected<void, Implicit>>);
static_assert(not std::is_constructible_v<expected<void, int>, Scalar>);
static_assert(not std::is_constructible_v<Scalar, Empty>);
struct ErrorFromExpected {
    ErrorFromExpected(int);
    ErrorFromExpected(Empty const &);
};
static_assert(not std::is_constructible_v<expected<void, ErrorFromExpected>, Empty const &>);
static_assert(not std::is_constructible_v<expected<void, ErrorFromExpected>, Empty &&>);
static_assert(not std::is_convertible_v<Scalar, bool>);
static_assert(std::is_same_v<Scalar::rebind<long>, expected<long, int>>);
static_assert(std::is_same_v<expected<void const, int>::value_type, void const>);

static_assert(std::is_same_v<decltype(*std::declval<Scalar &>()), int &>);
static_assert(std::is_same_v<decltype(*std::declval<Scalar const &>()), int const &>);
static_assert(std::is_same_v<decltype(*std::declval<Scalar &&>()), int &&>);
static_assert(std::is_same_v<decltype(*std::declval<Scalar const &&>()), int const &&>);
static_assert(std::is_same_v<decltype(std::declval<Scalar &>().value()), int &>);
static_assert(std::is_same_v<decltype(std::declval<Scalar const &>().value()), int const &>);
static_assert(std::is_same_v<decltype(std::declval<Scalar &&>().value()), int &&>);
static_assert(std::is_same_v<decltype(std::declval<Scalar const &&>().value()), int const &&>);
static_assert(std::is_same_v<decltype(std::declval<Scalar &>().error()), int &>);
static_assert(std::is_same_v<decltype(std::declval<Scalar const &>().error()), int const &>);
static_assert(std::is_same_v<decltype(std::declval<Scalar &&>().error()), int &&>);
static_assert(std::is_same_v<decltype(std::declval<Scalar const &&>().error()), int const &&>);
static_assert(std::is_same_v<decltype(*std::declval<Empty const &>()), void>);
static_assert(std::is_same_v<decltype(std::declval<Empty &>().value()), void>);
static_assert(std::is_same_v<decltype(std::declval<unexpected<int> &>().error()), int &>);
static_assert(
    std::is_same_v<decltype(std::declval<unexpected<int> const &&>().error()), int const &&>);
static_assert(noexcept(*std::declval<Scalar &>()));
static_assert(noexcept(std::declval<Scalar &>().error()));
static_assert(noexcept(std::declval<Scalar &>().swap(std::declval<Scalar &>())));
static_assert(not noexcept(std::declval<expected<int, ThrowingMove> &>().swap(
    std::declval<expected<int, ThrowingMove> &>())));

template <class X, class = void> inline constexpr bool canEmplaceInt = false;
template <class X>
inline constexpr bool canEmplaceInt<X, std::void_t<decltype(std::declval<X &>().emplace(1))>> =
    true;
struct ThrowingConstructor {
    explicit ThrowingConstructor(int) noexcept(false) {}
};
static_assert(canEmplaceInt<Scalar>);
static_assert(canEmplaceInt<Fixed>);
static_assert(not canEmplaceInt<expected<ThrowingConstructor, int>>);
static_assert(not canEmplaceInt<Empty>);

struct MutatingError {
    MutatingError() = default;
    MutatingError(MutatingError &) {}
    MutatingError(MutatingError const &) = delete;
};
struct ToInt {
    int operator()(int) const { return 2; }
};
template <class X, class = void> inline constexpr bool canTransform = false;
template <class X>
inline constexpr bool canTransform<X, std::void_t<decltype(std::declval<X>().transform(ToInt{}))>> =
    true;
static_assert(canTransform<expected<int, MutatingError> &>);
static_assert(not canTransform<expected<int, MutatingError> const &>);
static_assert(not canTransform<expected<int, MutatingError> &&>);

constexpr Scalar zero{};
constexpr Scalar value{12};
constexpr Scalar error{unexpect, 9};
constexpr Scalar copied = value;
constexpr Empty empty{};
constexpr expected<void const, int> cvEmpty{};
constexpr unexpected<int> u{4};
struct Literal {
    constexpr explicit Literal(int value_) : value(value_) {}
    constexpr Literal(Literal const &other) : value(other.value) {}
    constexpr Literal(Literal &&other) : value(other.value) {}
    int value;
};
constexpr expected<Literal, int> literal{std::in_place, 5};
constexpr auto literalCopy = literal;
constexpr auto literalMove = [] {
    expected<Literal, int> x{std::in_place, 6};
    return expected<Literal, int>(std::move(x));
}();
constexpr expected<long, long> converted{value};
constexpr expected<long, long> convertedError{error};
static_assert(literalCopy->value == 5 and literalMove->value == 6);
static_assert(*converted == 12 and convertedError.error() == 9);
static_assert(zero.has_value() and *zero == 0);
static_assert(copied.value() == 12 and value == 12 and 12 == value);
static_assert(not error and error.error() == 9 and error == unexpected{9});
static_assert(value.value_or(3) == 12 and error.value_or(3) == 3);
static_assert(value.error_or(3) == 3 and error.error_or(3) == 9);
static_assert(empty.has_value() and cvEmpty == empty);
static_assert(u.error() == 4 and u != unexpected{5});

int main() {
    expected<CopyOnly, int> first;
    expected<CopyOnly, int> copy = std::move(first);
    first = std::move(copy);
    expected<void, CopyOnly> voidFirst;
    expected<void, CopyOnly> voidCopy = std::move(voidFirst);
    voidFirst = voidCopy;
    CHECK(first and voidFirst);
    std::cout << "expected traits passed\n";
}
