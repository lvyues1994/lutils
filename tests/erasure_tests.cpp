#include <lutils/erasure.hpp>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace te = lutils::erasure;

#define CHECK(expression) do { \
    if (not (expression)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": " #expression "\n"; \
        std::abort(); \
    } \
} while (false)

namespace {
template <class Model>
struct IRead : te::Interface<IRead, Model> {
    using IRead::Interface::Interface;
    virtual int value() const { return te::value(*this).value(); }
};
template <class Model>
struct ICounter : te::Interface<ICounter, Model,
                               te::Extends<IRead, te::ICopyable>> {
    using ICounter::Interface::Interface;
    virtual void setValue(int n) { te::value(*this).setValue(n); }
};
template <class Model>
struct IMoveCounter : te::Interface<IMoveCounter, Model,
                                   te::Extends<IRead, te::IMovable>> {
    using IMoveCounter::Interface::Interface;
};
template <class Model>
struct IAddress : te::Interface<IAddress, Model, te::Extends<te::ICopyable>> {
    using IAddress::Interface::Interface;
    virtual void const *address() const { return te::value(*this).address(); }
    virtual int value() const { return te::value(*this).value(); }
};
template <class Model>
struct ILeft : te::Interface<ILeft, Model, te::Extends<IRead>> {
    using ILeft::Interface::Interface;
};
template <class Model>
struct IRight : te::Interface<IRight, Model, te::Extends<IRead>> {
    using IRight::Interface::Interface;
};
template <class Model>
struct IDiamond : te::Interface<IDiamond, Model,
                               te::Extends<ILeft, IRight, IRead, te::ICopyable>> {
    using IDiamond::Interface::Interface;
};
template <class Model>
struct IReference : te::Interface<IReference, Model, te::Extends<te::ICopyable>> {
    using IReference::Interface::Interface;
    virtual int &access() & { return te::value(*this).access(); }
    virtual int const &access() const & { return te::value(*this).access(); }
    virtual void accept(std::unique_ptr<int> n) {
        te::value(*this).accept(std::move(n));
    }
};
template <class Model>
struct INoexcept : te::Interface<INoexcept, Model> {
    using INoexcept::Interface::Interface;
    virtual int value() const noexcept { return te::value(*this).value(); }
};

struct Counter {
    explicit Counter(int n = 0) : n_(n) {}
    int value() const { return n_; }
    void setValue(int n) { n_ = n; }
    void const *address() const { return this; }
    int &access() & { return n_; }
    int const &access() const & { return n_; }
    void accept(std::unique_ptr<int> n) { n_ = *n; }
private:
    int n_;
};

struct ImmovableCounter : Counter {
    using Counter::Counter;
    ImmovableCounter(ImmovableCounter const &) = delete;
    ImmovableCounter(ImmovableCounter &&) = delete;
    ImmovableCounter &operator=(ImmovableCounter const &) = delete;
    ImmovableCounter &operator=(ImmovableCounter &&) = delete;
};

struct Tracked {
    inline static int alive = 0;
    inline static int copies = 0;
    inline static int moves = 0;
    inline static bool failCopy = false;
    inline static bool failConstruction = false;

    explicit Tracked(int n) : n_(n) {
        if (failConstruction) { throw std::runtime_error{"construction"}; }
        ++alive;
    }
    Tracked(Tracked const &other) : n_(other.n_) {
        if (failCopy) { throw std::runtime_error{"copy"}; }
        ++alive;
        ++copies;
    }
    Tracked(Tracked &&other) noexcept : n_(other.n_) { ++alive; ++moves; }
    Tracked &operator=(Tracked const &) = delete;
    Tracked &operator=(Tracked &&) = delete;
    ~Tracked() { --alive; }
    int value() const { return n_; }
    void setValue(int n) { n_ = n; }
    void const *address() const { return this; }
private:
    int n_;
};

struct ThrowingMove : Counter {
    using Counter::Counter;
    ThrowingMove(ThrowingMove const &) = default;
    ThrowingMove(ThrowingMove &&) { throw std::runtime_error{"move"}; }
};
struct LargeCounter : Counter {
    using Counter::Counter;
    std::array<unsigned char, 256> padding{};
};
struct alignas(128) AlignedCounter : Counter { using Counter::Counter; };
struct ThrowingCall { int value() const { throw std::runtime_error{"call"}; } };

struct External { int n; };
int readExternal(External const &object) { return object.n; }
template <class Model>
struct IExternal : te::Interface<IExternal, Model, te::Extends<te::ICopyable>> {
    using IExternal::Interface::Interface;
    // ADL adapts the concrete object; the abstract instantiation calls its own
    // virtual method so the same body is valid for the erased proxy as well.
    friend int readExternal(IExternal const &object) { return object.read(); }
    virtual int read() const { return readExternal(te::value(*this)); }
};

using CopyOwner = te::Any<ICounter>;
using MoveOwner = te::Any<IMoveCounter>;
using FixedOwner = te::Any<IRead>;
using Small = te::SmallBufferStorage<64>;

static_assert(std::is_copy_constructible_v<CopyOwner>);
static_assert(std::is_copy_assignable_v<CopyOwner>);
static_assert(std::is_nothrow_move_constructible_v<CopyOwner>);
static_assert(std::is_nothrow_move_assignable_v<CopyOwner>);
static_assert(not std::is_copy_constructible_v<MoveOwner>);
static_assert(not std::is_copy_assignable_v<MoveOwner>);
static_assert(std::is_nothrow_move_constructible_v<MoveOwner>);
static_assert(std::is_nothrow_move_assignable_v<MoveOwner>);
static_assert(not std::is_copy_constructible_v<FixedOwner>);
static_assert(not std::is_copy_assignable_v<FixedOwner>);
static_assert(not std::is_move_constructible_v<FixedOwner>);
static_assert(not std::is_move_assignable_v<FixedOwner>);
static_assert(not std::is_constructible_v<CopyOwner, ImmovableCounter &>);
static_assert(not std::is_constructible_v<CopyOwner, MoveOwner &>);
static_assert(not std::is_constructible_v<te::AnyPtr<ICounter>, Counter const *>);
static_assert(not std::is_constructible_v<te::AnyPtr<ICounter>,
                                         te::AnyConstPtr<ICounter>>);
static_assert(not std::is_constructible_v<te::AnyPtr<ICounter>, te::AnyPtr<IRead>>);
static_assert(not std::is_constructible_v<te::AnyPtr<ICounter>, Counter &&>);
static_assert(std::is_nothrow_copy_constructible_v<te::AnyPtr<IRead>>);
static_assert(std::is_nothrow_copy_assignable_v<te::AnyConstPtr<IRead>>);

void ownership() {
    auto empty = CopyOwner{};
    CHECK(te::empty(empty));
    auto emptyCopy = empty;
    CHECK(te::empty(emptyCopy));
    auto owner = CopyOwner{Counter{10}};
    owner.setValue(20);
    CHECK(std::as_const(owner).value() == 20);
    auto copy = owner;
    copy.setValue(30);
    CHECK(owner.value() == 20);
    auto moved = std::move(copy);
    CHECK(te::empty(copy));
    CHECK(moved.value() == 30);
    empty = moved;
    CHECK(empty.value() == 30);
    emptyCopy = std::move(moved);
    CHECK(te::empty(moved));
    CHECK(emptyCopy.value() == 30);
    auto *same = &owner;
    owner = *same;
    owner = std::move(*same);
    CHECK(owner.value() == 20);
    te::swap(owner, *same);
    te::swap(owner, copy);
    CHECK(te::empty(owner));
    CHECK(copy.value() == 20);
    te::reset(copy);
    te::reset(copy);
    CHECK(te::empty(copy));

    auto fixed = FixedOwner{std::in_place_type<ImmovableCounter>, 41};
    CHECK(fixed.value() == 41);
    te::emplace<ImmovableCounter>(fixed, 42);
    CHECK(fixed.value() == 42);
    auto immovableTarget = MoveOwner{std::in_place_type<ImmovableCounter>, 51};
    auto movedOwner = std::move(immovableTarget);
    CHECK(movedOwner.value() == 51);
    CHECK(te::empty(immovableTarget));

    auto values = std::vector<MoveOwner>{};
    values.emplace_back(std::in_place_type<ImmovableCounter>, 61);
    values.emplace_back(Counter{62});
    CHECK(values[0].value() == 61);
    CHECK(values[1].value() == 62);
}

void borrowing() {
    auto target = Counter{1};
    auto pointer = te::AnyPtr<ICounter>{&target};
    auto const pointerCopy = pointer;
    pointerCopy->setValue(2);
    CHECK(target.value() == 2);
    te::reset(pointer);
    CHECK(not pointer);
    CHECK(pointerCopy->value() == 2);
    auto readonly = te::AnyConstPtr<ICounter>{pointerCopy};
    CHECK(readonly->value() == 2);
    auto const constant = Counter{3};
    auto constView = te::AnyConstPtr<ICounter>{&constant};
    CHECK(constView->value() == 3);

    auto projected = te::AnyPtr<IRead>{};
    auto constProjected = te::AnyConstPtr<IRead>{};
    {
        auto source = te::AnyPtr<ICounter>{&target};
        projected = te::AnyPtr<IRead>{source};
        constProjected = te::AnyConstPtr<IRead>{source};
    }
    CHECK(projected->value() == 2);
    CHECK(constProjected->value() == 2);
    auto owner = CopyOwner{Counter{4}};
    auto ownerView = te::AnyPtr<ICounter>{&owner};
    ownerView->setValue(5);
    CHECK(owner.value() == 5);
    auto baseOwnerView = te::AnyPtr<IRead>{&owner};
    CHECK(baseOwnerView->value() == 5);
    auto constOwnerView = te::AnyConstPtr<IRead>{&std::as_const(owner)};
    CHECK(constOwnerView->value() == 5);
    auto emptyOwner = CopyOwner{};
    CHECK(not te::AnyPtr<IRead>{&emptyOwner});
    CHECK(not te::AnyPtr<IRead>{static_cast<CopyOwner *>(nullptr)});
    CHECK(not te::AnyPtr<IRead>{static_cast<Counter *>(nullptr)});
    CHECK(not te::AnyConstPtr<IRead>{te::AnyPtr<ICounter>{}});

    auto noncopyable = ImmovableCounter{6};
    auto borrowedNoncopyable = te::AnyPtr<ICounter>{&noncopyable};
    auto copiedView = borrowedNoncopyable;
    copiedView->setValue(7);
    CHECK(noncopyable.value() == 7);
    auto swapped = te::AnyPtr<ICounter>{};
    te::swap(copiedView, swapped);
    CHECK(not copiedView);
    CHECK(swapped->value() == 7);
    auto *same = &swapped;
    te::swap(swapped, *same);
    swapped = *same;
    swapped = std::move(*same);
    CHECK(swapped->value() == 7);
}

template <class Policy>
void exceptionsAndLifetime() {
    CHECK(Tracked::alive == 0);
    {
        auto original = te::Any<ICounter, Policy>{std::in_place_type<Tracked>, 10};
        auto destination = te::Any<ICounter, Policy>{std::in_place_type<Tracked>, 20};
        CHECK(Tracked::alive == 2);
        Tracked::failCopy = true;
        try { destination = original; CHECK(false); }
        catch (std::runtime_error const &) {}
        CHECK(destination.value() == 20);
        CHECK(original.value() == 10);
        CHECK(Tracked::alive == 2);
        Tracked::failCopy = false;
        destination = original;
        CHECK(destination.value() == 10);
        CHECK(Tracked::alive == 2);

        Tracked::failConstruction = true;
        try { te::emplace<Tracked>(destination, 30); CHECK(false); }
        catch (std::runtime_error const &) {}
        Tracked::failConstruction = false;
        CHECK(destination.value() == 10);
        CHECK(Tracked::alive == 2);
        auto moved = std::move(original);
        CHECK(te::empty(original));
        CHECK(Tracked::alive == 2);
        te::swap(moved, destination);
        te::reset(moved);
        CHECK(Tracked::alive == 1);
    }
    CHECK(Tracked::alive == 0);
    auto throwing = te::Any<IRead, Policy>{ThrowingCall{}};
    try { (void)throwing.value(); CHECK(false); }
    catch (std::runtime_error const &) {}
}

template <class Owner>
bool insideOwner(Owner const &owner, void const *target) {
    auto const begin = reinterpret_cast<std::uintptr_t>(&owner);
    auto const address = reinterpret_cast<std::uintptr_t>(target);
    return address >= begin and address - begin < sizeof(Owner);
}

void storagePolicies() {
    auto heap = te::Any<IAddress>{Counter{1}};
    CHECK(not insideOwner(heap, heap.address()));
    auto small = te::Any<IAddress, Small>{Counter{2}};
    CHECK(insideOwner(small, small.address()));
    auto copy = small;
    CHECK(insideOwner(copy, copy.address()));
    CHECK(copy.address() != small.address());
    auto moved = std::move(copy);
    CHECK(insideOwner(moved, moved.address()));
    CHECK(te::empty(copy));
    auto large = te::Any<IAddress, Small>{std::in_place_type<LargeCounter>, 3};
    CHECK(not insideOwner(large, large.address()));
    auto aligned = te::Any<IAddress, Small>{std::in_place_type<AlignedCounter>, 4};
    CHECK(not insideOwner(aligned, aligned.address()));
    CHECK(reinterpret_cast<std::uintptr_t>(aligned.address()) % 128 == 0);
    auto alignedInline = te::Any<IAddress, te::SmallBufferStorage<512, 128>>{
        std::in_place_type<AlignedCounter>, 5};
    CHECK(insideOwner(alignedInline, alignedInline.address()));
    auto throwingMove = te::Any<IAddress, Small>{std::in_place_type<ThrowingMove>, 6};
    CHECK(not insideOwner(throwingMove, throwingMove.address()));
    auto *address = throwingMove.address();
    auto safeMove = std::move(throwingMove);
    CHECK(safeMove.address() == address);
    CHECK(safeMove.value() == 6);
    te::swap(small, large);
    CHECK(small.value() == 3);
    CHECK(large.value() == 2);
    CHECK(not insideOwner(small, small.address()));
    CHECK(insideOwner(large, large.address()));
    auto empty = te::Any<IAddress, Small>{};
    te::swap(large, empty);
    CHECK(te::empty(large));
    CHECK(insideOwner(empty, empty.address()));
    te::emplace<Counter>(small, 7);
    CHECK(insideOwner(small, small.address()));
    CHECK(small.value() == 7);
    auto view = te::AnyPtr<IAddress>{&small};
    CHECK(view->address() == small.address());
}

void compositionAndReferences() {
    auto diamond = te::Any<IDiamond>{Counter{9}};
    CHECK(diamond.value() == 9);
    auto left = te::AnyPtr<ILeft>{&diamond};
    auto right = te::AnyPtr<IRight>{&diamond};
    auto base = te::AnyPtr<IRead>{right};
    CHECK(left->value() == 9);
    CHECK(base->value() == 9);

    auto owner = te::Any<IReference>{Counter{10}};
    owner.access() = 11;
    CHECK(std::as_const(owner).access() == 11);
    owner.accept(std::make_unique<int>(12));
    CHECK(owner.access() == 12);
    auto borrow = te::AnyPtr<IReference>{&owner};
    borrow->access() = 13;
    CHECK(owner.access() == 13);
    auto readonly = te::AnyConstPtr<IReference>{borrow};
    static_assert(std::is_same_v<decltype(readonly->access()), int const &>);
    CHECK(&readonly->access() == &owner.access());

    auto external = te::Any<IExternal>{External{24}};
    CHECK(external.read() == 24);
}
} // namespace

int main(int argc, char **argv) {
    if (argc == 2) {
        std::set_terminate([] { std::_Exit(42); });
        auto const mode = std::string_view{argv[1]};
        if (mode == "empty-owner") { (void)te::Any<IRead>{}.value(); }
        if (mode == "empty-borrow") { (void)te::AnyPtr<IRead>{}->value(); }
        if (mode == "noexcept-call") {
            (void)te::Any<INoexcept>{ThrowingCall{}}.value();
        }
        return 1;
    }
    ownership();
    borrowing();
    exceptionsAndLifetime<te::HeapStorage>();
    exceptionsAndLifetime<Small>();
    storagePolicies();
    compositionAndReferences();
    std::cout << "erasure behavior checks passed\n";
}
