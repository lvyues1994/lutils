#include "Test.hpp"

using ex::expected;
using ex::unexpect;
using ex::unexpected;

namespace {
struct Failure {};

// Independent counters catch leaked or doubly destroyed active union members.
template <bool SafeMove, int Id> struct Probe {
    inline static int live{};
    inline static bool failCopy{};
    inline static bool failMove{};
    inline static bool failAssign{};
    inline static bool failInt{};

    explicit Probe(int value_) : value(value_) {
        if (failInt)
            throw Failure{};
        ++live;
    }
    Probe(Probe const &other) : value(other.value) {
        if (failCopy)
            throw Failure{};
        ++live;
    }
    Probe(Probe &&other) noexcept(SafeMove) : value(other.value) {
        if constexpr (not SafeMove) {
            if (failMove)
                throw Failure{};
        }
        other.value = -1;
        ++live;
    }
    Probe &operator=(Probe const &other) {
        value = other.value;
        if (failAssign)
            throw Failure{};
        return *this;
    }
    Probe &operator=(Probe &&other) noexcept(false) {
        value = other.value;
        if (failAssign)
            throw Failure{};
        other.value = -1;
        return *this;
    }
    Probe &operator=(int other) {
        value = other;
        return *this;
    }
    ~Probe() {
        --live;
        CHECK(live >= 0);
    }
    friend void swap(Probe &left, Probe &right) noexcept { std::swap(left.value, right.value); }
    int value;
};
using Safe = Probe<true, 0>;
using Risky = Probe<false, 1>;

template <class F> void mustThrow(F &&f) {
    bool caught{};
    try {
        std::forward<F>(f)();
    } catch (Failure const &) {
        caught = true;
    }
    CHECK(caught);
}

void constructorFailure() {
    expected<Risky, Safe> value(std::in_place, 7);
    expected<Risky, Safe> error(unexpect, 9);
    Risky::failCopy = true;
    mustThrow([&] {
        auto copy = value;
        (void)copy;
    });
    CHECK(value->value == 7 and Risky::live == 1);
    Risky::failCopy = false;
    Safe::failCopy = true;
    mustThrow([&] {
        auto copy = error;
        (void)copy;
    });
    CHECK(error.error().value == 9 and Safe::live == 1);
    Safe::failCopy = false;
    Risky::failMove = true;
    mustThrow([&] {
        auto moved = std::move(value);
        (void)moved;
    });
    CHECK(value->value == 7 and Risky::live == 1);
    Risky::failMove = false;
    Risky::failInt = true;
    mustThrow([] {
        expected<Risky, Safe> bad(std::in_place, 1);
        (void)bad;
    });
    mustThrow([] {
        expected<void, Risky> bad(unexpect, 1);
        (void)bad;
    });
    CHECK(Risky::live == 1);
    Risky::failInt = false;
}

void assignmentRollback() {
    expected<Risky, Safe> value(std::in_place, 7);
    expected<Risky, Safe> error(unexpect, 9);
    // New value cannot be moved without throwing: back up and restore old error.
    Risky::failCopy = true;
    mustThrow([&] { error = value; });
    CHECK(not error and error.error().value == 9);
    CHECK(Risky::live == 1 and Safe::live == 1);
    Risky::failCopy = false;
    Risky::failMove = true;
    mustThrow([&] { error = std::move(value); });
    CHECK(not error and error.error().value == 9 and value);
    CHECK(Risky::live == 1 and Safe::live == 1);
    Risky::failMove = false;
    // New error moves without throwing: construct the temporary before destruction.
    Safe::failCopy = true;
    mustThrow([&] { value = error; });
    CHECK(value and value->value == 7);
    CHECK(Risky::live == 1 and Safe::live == 1);
    Safe::failCopy = false;
    // The value-assignment overload takes the same guarded path.
    Risky::failInt = true;
    mustThrow([&] { error = 3; });
    CHECK(not error and error.error().value == 9);
    Risky::failInt = false;
    error = 3;
    CHECK(error->value == 3 and Risky::live == 2 and Safe::live == 0);
    value = unexpected<Safe>(std::in_place, 10);
    CHECK(not value and value.error().value == 10);
}

void reverseAssignmentRollback() {
    expected<Safe, Risky> value(std::in_place, 7);
    expected<Safe, Risky> error(unexpect, 9);
    Risky::failCopy = true;
    mustThrow([&] { value = error; });
    CHECK(value and value->value == 7);
    Risky::failCopy = false;
    Safe::failCopy = true;
    mustThrow([&] { error = value; });
    CHECK(not error and error.error().value == 9);
    Safe::failCopy = false;
    auto u = unexpected<Risky>(std::in_place, 13);
    Risky::failCopy = true;
    mustThrow([&] { value = u; });
    CHECK(value and value->value == 7);
    Risky::failCopy = false;
    Risky::failMove = true;
    mustThrow([&] { value = std::move(u); });
    CHECK(value and value->value == 7);
    Risky::failMove = false;
    CHECK(Safe::live == 1 and Risky::live == 2);
}

void sameStateFailure() {
    expected<Risky, Safe> a(std::in_place, 7), b(std::in_place, 8);
    Risky::failAssign = true;
    mustThrow([&] { a = b; });
    CHECK(a and b and a->value == 8 and Risky::live == 2);
    Risky::failAssign = false;
    expected<Risky, Safe> e(unexpect, 9), f(unexpect, 10);
    Safe::failAssign = true;
    mustThrow([&] { e = f; });
    CHECK(not e and not f and e.error().value == 10 and Safe::live == 2);
    Safe::failAssign = false;
}

void swapRollback() {
    {
        expected<Risky, Safe> a(std::in_place, 7), b(unexpect, 9);
        Risky::failMove = true;
        mustThrow([&] { a.swap(b); });
        CHECK(a and not b and a->value == 7 and b.error().value == 9);
        CHECK(Risky::live == 1 and Safe::live == 1);
        Risky::failMove = false;
        a.swap(b);
        CHECK(not a and b and a.error().value == 9 and b->value == 7);
    }
    {
        expected<Safe, Risky> a(std::in_place, 7), b(unexpect, 9);
        Risky::failMove = true;
        mustThrow([&] { b.swap(a); });
        CHECK(a and not b and a->value == 7 and b.error().value == 9);
        CHECK(Risky::live == 1 and Safe::live == 1);
        Risky::failMove = false;
        b.swap(a);
        CHECK(not a and b and a.error().value == 9 and b->value == 7);
    }
}

void voidRollback() {
    expected<void, Risky> good, bad(unexpect, 9);
    Risky::failCopy = true;
    mustThrow([&] { good = bad; });
    CHECK(good and not bad and Risky::live == 1);
    Risky::failCopy = false;
    Risky::failMove = true;
    mustThrow([&] { good = std::move(bad); });
    CHECK(good and not bad and Risky::live == 1);
    mustThrow([&] { good.swap(bad); });
    CHECK(good and not bad and Risky::live == 1);
    Risky::failMove = false;
    good.swap(bad);
    CHECK(not good and bad and good.error().value == 9);
    good.emplace();
    CHECK(good and Risky::live == 0);
}

void monadicFailure() {
    expected<Risky, Safe> a(std::in_place, 7), e(unexpect, 9);
    auto callback = [](Risky const &) -> int { throw Failure{}; };
    mustThrow([&] { (void)a.transform(callback); });
    CHECK(a and a->value == 7 and Risky::live == 1);
    Safe::failCopy = true;
    mustThrow([&] { (void)e.transform(callback); });
    CHECK(not e and e.error().value == 9 and Safe::live == 1);
    Safe::failCopy = false;
}
} // namespace

int main() {
    for (auto test : {constructorFailure, assignmentRollback, reverseAssignmentRollback,
                      sameStateFailure, swapRollback, voidRollback, monadicFailure}) {
        test();
        CHECK(Safe::live == 0 and Risky::live == 0);
    }
    std::cout << "expected exception guarantees passed\n";
}
