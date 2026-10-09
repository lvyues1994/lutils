#include "Test.hpp"
#include <cstdint>
#include <functional>
#include <vector>

using ex::expected;
using ex::unexpect;
using ex::unexpected;

namespace adversarial {
struct Error {};
// Associated-namespace operators must not intercept library-internal observers.
template <class T> T &operator*(ex::expected<T, Error> &) = delete;
template <class T> T const &operator*(ex::expected<T, Error> const &) = delete;
template <class T> T &&operator*(ex::expected<T, Error> &&) = delete;
template <class T> T const &&operator*(ex::expected<T, Error> const &&) = delete;
template <class T, class... Args> void construct(T *, Args &&...) = delete;
} // namespace adversarial

namespace {
struct alignas(128) Addressed {
    int value{42};
    Addressed *operator&() { return nullptr; }
    Addressed const *operator&() const { return nullptr; }
    int get() const { return value; }
};

struct ErrorCopy {
    explicit ErrorCopy(int value_) : value(value_) {}
    ErrorCopy(ErrorCopy &) : value(-1) {}
    ErrorCopy(ErrorCopy const &other) : value(other.value) {}
    ErrorCopy(ErrorCopy &&other) noexcept : value(std::exchange(other.value, 0)) {}
    int value;
};

struct NoConstMove {
    NoConstMove() = default;
    NoConstMove(NoConstMove const &) = default;
    NoConstMove(NoConstMove &&) = default;
    NoConstMove(NoConstMove const &&) = delete;
};

struct ListValue {
    ListValue(std::initializer_list<int> values, int bias = 0) noexcept : sum(bias) {
        for (int n : values)
            sum += n;
    }
    int sum;
};

void construction() {
    expected<int, int> same = 7;
    CHECK(same == 7 and 7 == same);
    same = unexpected{8};
    CHECK(same == unexpected{8} and unexpected{8} == same);
    CHECK(same != 7 and 7 != same);
    CHECK(same == expected<long, long>(unexpect, 8));
    CHECK(expected<void, int>() != unexpected{8});
    expected<void, int> inner;
    expected<expected<void, int>, int> outer(std::in_place);
    CHECK(outer == inner and inner == outer);
    CHECK(not(outer != inner) and not(inner != outer));

    expected<std::vector<int>, std::vector<int>> list(std::in_place, {1, 2, 3});
    CHECK(list->size() == 3);
    expected<std::vector<int>, std::vector<int>> errors(unexpect, {4, 5});
    CHECK(errors.error() == std::vector<int>({4, 5}));
    unexpected<std::vector<int>> u(std::in_place, {6, 7});
    errors = u;
    CHECK(errors.error() == std::vector<int>({6, 7}));
    errors = unexpected(std::vector<int>{9});
    CHECK(errors.error().front() == 9);
    expected<ListValue, int> emplaced(unexpect, 1);
    CHECK(emplaced.emplace({1, 2, 3}, 4).sum == 10);
    CHECK(emplaced.emplace({5, 6}).sum == 11);

    expected<Immovable, Immovable> fixed(std::in_place, 5);
    CHECK(fixed->value == 5);
    CHECK(fixed.emplace(6).value == 6);
    expected<Immovable, Immovable> fixedError(unexpect, 8);
    CHECK(fixedError.error().value == 8);
    CHECK(fixedError.emplace(9).value == 9);
    expected<Addressed, int> address;
    CHECK(address.operator->() == std::addressof(*address));
    CHECK(address->get() == 42);
    CHECK(reinterpret_cast<std::uintptr_t>(address.operator->()) % alignof(Addressed) == 0);

    expected<bool, std::string> no = expected<int, char const *>(0);
    expected<bool, std::string> yes = expected<int, char const *>(4);
    expected<bool, std::string> failed = expected<int, char const *>(unexpect, "error");
    CHECK(no.has_value() and not *no);
    CHECK(yes.has_value() and *yes);
    CHECK(not failed and failed.error() == "error");
    expected<bool, char const *> const cb{false};
    expected<bool, std::string> copiedBool{cb};
    CHECK(copiedBool.has_value() and not *copiedBool);

    expected<int const, int> constValue{1};
    CHECK(constValue.emplace(2) == 2);
    expected<void const, std::string> voidError = expected<void, char const *>(unexpect, "failed");
    CHECK(not voidError and voidError.error() == "failed");
    voidError.emplace();
    voidError.value();
    *voidError;
    CHECK(voidError.has_value());
}

void assignmentsAndSwap() {
    using Result = expected<std::string, std::string>;
    Result a{"one"}, b{"two"};
    a = b;
    CHECK(a == "two");
    b = unexpected{std::string("error")};
    a = b;
    CHECK(not a and a.error() == "error");
    b = "value";
    a = b;
    CHECK(a == "value");
    auto &alias = a;
    a = alias;
    a = std::move(alias);
    CHECK(a.has_value());

    for (bool leftValue : {false, true}) {
        for (bool rightValue : {false, true}) {
            Result left = leftValue ? Result("left") : Result(unexpect, "left");
            Result right = rightValue ? Result("right") : Result(unexpect, "right");
            auto oldLeft = left;
            auto oldRight = right;
            using std::swap;
            swap(left, right);
            CHECK(left == oldRight and right == oldLeft);
            left.swap(left);
            CHECK(left == oldRight);
            left = std::move(right);
            CHECK(left == oldLeft and right.has_value() == leftValue);
        }
    }
    expected<void, std::string> good;
    expected<void, std::string> bad(unexpect, "void error");
    good.swap(bad);
    CHECK(not good and good.error() == "void error" and bad);
    good = bad;
    CHECK(good);
    bad = unexpected{std::string("again")};
    good = std::move(bad);
    CHECK(not good and not bad and good.error() == "again");

    unexpected<int> u{1}, v{2};
    using std::swap;
    swap(u, v);
    CHECK(u.error() == 2 and v.error() == 1);
}

template <class R> void checkedFailure(R &&result, int error) {
    try {
        std::forward<R>(result).value();
        CHECK(false);
    } catch (ex::bad_expected_access<int> const &e) {
        CHECK(e.error() == error);
        CHECK(std::string(e.what()).size() > 0);
    }
}

void observers() {
    expected<void, NoConstMove> const voidValue;
    std::move(voidValue).value();
    expected<int, int> value{5};
    expected<int, int> error(unexpect, 9);
    expected<int, int> const ce(unexpect, 9);
    checkedFailure(error, 9);
    checkedFailure(ce, 9);
    checkedFailure(std::move(error), 9);
    checkedFailure(std::move(ce), 9);
    expected<void, int> ve(unexpect, 11);
    expected<void, int> const cve(unexpect, 11);
    checkedFailure(ve, 11);
    checkedFailure(cve, 11);
    checkedFailure(std::move(ve), 11);
    checkedFailure(std::move(cve), 11);

    expected<int, ErrorCopy> copyError(unexpect, 17);
    try {
        (void)copyError.value();
        CHECK(false);
    } catch (ex::bad_expected_access<ErrorCopy> const &e) {
        CHECK(e.error().value == 17); // value() copies from const E&, even on a mutable object.
    }
    CHECK(value.value_or(7) == 5 and error.value_or(7) == 7);
    CHECK(value.error_or(7) == 7 and error.error_or(7) == 9);
    expected<std::string, std::string> text = "ok";
    CHECK(text.value_or(std::string{}) == "ok" and text.error_or({}).empty());
#ifndef LUTILS_EXPECTED_USE_STD
    CHECK(text.value_or({}) == "ok"); // LWG3886; libc++18 predates this correction.
#endif
    expected<std::unique_ptr<int>, int> moveValue(std::in_place, std::make_unique<int>(42));
    auto ptr = std::move(moveValue).value_or(nullptr);
    CHECK(*ptr == 42 and moveValue and not *moveValue);
    expected<int, std::unique_ptr<int>> moveError(unexpect, std::make_unique<int>(13));
    auto errorPtr = std::move(moveError).error_or(nullptr);
    CHECK(*errorPtr == 13 and not moveError and not moveError.error());
}

struct Category {
    int operator()(int &) const { return 1; }
    int operator()(int const &) const { return 2; }
    int operator()(int &&) const { return 3; }
    int operator()(int const &&) const { return 4; }
};
struct ChainCategory {
    expected<int, int> operator()(int &v) const { return Category{}(v); }
    expected<int, int> operator()(int const &v) const { return Category{}(v); }
    expected<int, int> operator()(int &&v) const { return Category{}(std::move(v)); }
    expected<int, int> operator()(int const &&v) const { return Category{}(std::move(v)); }
};

struct CallableCategory {
    int operator()(int) & { return 1; }
    int operator()(int) && { return 2; }
};

template <class R> void checkCategories(R &&good, R &&bad, int category) {
    CHECK(*std::forward<R>(good).transform(Category{}) == category);
    CHECK(*std::forward<R>(good).and_then(ChainCategory{}) == category);
    CHECK(std::forward<R>(bad).transform_error(Category{}).error() == category);
    CHECK(*std::forward<R>(bad).or_else(ChainCategory{}) == category);
}

void monadic() {
    expected<int, adversarial::Error> guarded{7};
    auto guardedNext = guarded.transform([](int n) { return n + 1; });
    expected<long, adversarial::Error> guardedConversion{guarded};
    CHECK(guardedNext.value() == 8 and guardedConversion.value() == 7);
    guarded = unexpected<adversarial::Error>{std::in_place};
    guarded = 9;
    CHECK(guarded.value() == 9);

    expected<int, int> good{7}, bad{unexpect, 9};
    CallableCategory callable;
    CHECK(*good.transform(callable) == 1);
    CHECK(*good.transform(std::move(callable)) == 2);
    expected<int, int> const cg{7}, cb{unexpect, 9};
    checkCategories(good, bad, 1);
    checkCategories(cg, cb, 2);
    checkCategories(std::move(good), std::move(bad), 3);
    checkCategories(std::move(cg), std::move(cb), 4);

    int calls{};
    auto fail = [&calls](int) -> expected<int, int> {
        ++calls;
        return unexpected{44};
    };
    CHECK(bad.and_then(fail).error() == 9 and calls == 0);
    CHECK(good.and_then(fail).error() == 44 and calls == 1);
    auto recover = [&calls](int) -> expected<int, std::string> {
        ++calls;
        return 10;
    };
    CHECK(*good.or_else(recover) == 7 and calls == 1);
    CHECK(*bad.or_else(recover) == 10 and calls == 2);
    auto touch = [&calls](int) { ++calls; };
    CHECK(not bad.transform(touch) and calls == 2);
    CHECK(good.transform(touch) and calls == 3);
    auto translate = [&calls](int) {
        ++calls;
        return std::string("mapped");
    };
    CHECK(*good.transform_error(translate) == 7 and calls == 3);
    CHECK(bad.transform_error(translate).error() == "mapped" and calls == 4);

    auto fixed = good.transform([](int x) { return Immovable(x + 1); });
    auto fixedError = bad.transform_error([](int x) { return Immovable(x + 2); });
    CHECK(fixed->value == 8 and fixedError.error().value == 11);

    expected<int, std::unique_ptr<int>> moveError{7};
    auto next = std::move(moveError).and_then(
        [](int x) -> expected<long, std::unique_ptr<int>> { return long(x + 1); });
    CHECK(*next == 8);
    expected<int, std::unique_ptr<int>> failed(unexpect, std::make_unique<int>(14));
    auto propagated = std::move(failed).transform([](int x) { return x + 1; });
    CHECK(not propagated and *propagated.error() == 14);
    auto recovered =
        std::move(propagated).or_else([](std::unique_ptr<int> e) -> expected<int, int> {
            return *e;
        });
    CHECK(*recovered == 14);

    expected<Addressed, int> object;
    CHECK(*object.transform(&Addressed::get) == 42);
    expected<std::reference_wrapper<Addressed>, int> reference(std::ref(*object));
    CHECK(*reference.transform(&Addressed::get) == 42);
    expected<std::unique_ptr<Addressed>, int> owner(std::in_place, std::make_unique<Addressed>());
    CHECK(*owner.transform(&Addressed::get) == 42);
    // A member-data pointer returns a reference, so use and_then for an expected member.
    struct Member {
        expected<int, int> result{12};
    };
    expected<Member, int> member;
    CHECK(*member.and_then(&Member::result) == 12);
}

template <class R> void checkVoid(R &&good, R &&bad) {
    CHECK(*std::forward<R>(good).transform([] { return 5; }) == 5);
    CHECK(std::forward<R>(good).transform([] {}) == expected<void, int>());
    CHECK(*std::forward<R>(good).and_then([] { return expected<int, int>(6); }) == 6);
    CHECK(std::forward<R>(bad).and_then([] { return expected<int, int>(6); }).error() == 9);
    CHECK(not std::forward<R>(bad).transform([] { return 5; }));
    CHECK(std::forward<R>(bad).or_else([](int) { return expected<void, long>(); }));
    CHECK(std::forward<R>(good).or_else([](int) { return expected<void, long>(unexpect, 1); }));
    CHECK(std::forward<R>(bad).transform_error([](int x) { return x + 1; }).error() == 10);
    CHECK(std::forward<R>(good).transform_error([](int x) { return x + 1; }));
}

void voidMonadic() {
    expected<void, int> good, bad{unexpect, 9};
    expected<void, int> const cg, cb{unexpect, 9};
    checkVoid(good, bad);
    checkVoid(cg, cb);
    checkVoid(std::move(good), std::move(bad));
    checkVoid(std::move(cg), std::move(cb));
    auto fixed = good.transform([] { return Immovable(10); });
    auto fixedError = bad.transform_error([](int x) { return Immovable(x); });
    CHECK(fixed->value == 10 and fixedError.error().value == 9);
}
} // namespace

int main() {
    construction();
    assignmentsAndSwap();
    observers();
    monadic();
    voidMonadic();
    std::cout << "expected behavior passed\n";
}
