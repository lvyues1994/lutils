#include <lutils/erasure.hpp>
#include <cstdlib>
#include <new>

namespace {
std::size_t allocations = 0;
}

void *operator new(std::size_t size) {
    ++allocations;
    if (auto *memory = std::malloc(size == 0 ? 1 : size)) { return memory; }
    throw std::bad_alloc{};
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept { std::free(memory); }

namespace te = lutils::erasure;
template <class Model>
struct IRead : te::Interface<IRead, Model> {
    using IRead::Interface::Interface;
    virtual int read() const { return te::value(*this).read(); }
};
template <class Model>
struct ICounter : te::Interface<ICounter, Model,
                               te::Extends<IRead, te::ICopyable>> {
    using ICounter::Interface::Interface;
};
struct Counter { int read() const { return 42; } };

int main() {
    auto const beforeHeap = allocations;
    auto heap = te::Any<ICounter>{Counter{}};
    if (allocations != beforeHeap + 1) { return 1; }
    auto heapCopy = heap;
    if (allocations != beforeHeap + 2) { return 2; }
    auto movedHeap = std::move(heapCopy);
    if (allocations != beforeHeap + 2) { return 3; }

    auto const beforeInline = allocations;
    auto small = te::Any<ICounter, te::SmallBufferStorage<>>{Counter{}};
    auto copiedSmall = small;
    auto movedSmall = std::move(copiedSmall);
    te::swap(small, movedSmall);
    if (allocations != beforeInline) { return 4; }

    auto target = Counter{};
    auto const beforeBorrow = allocations;
    auto pointer = te::AnyPtr<ICounter>{&target};
    auto copy = pointer;
    auto moved = std::move(copy);
    auto base = te::AnyPtr<IRead>{moved};
    auto readOnly = te::AnyConstPtr<IRead>{base};
    auto ownerView = te::AnyPtr<ICounter>{&heap};
    te::reset(pointer);
    te::reset(moved);
    if (allocations != beforeBorrow) { return 5; }
    return readOnly->read() == 42 and ownerView->read() == 42 and
           movedHeap.read() == 42 ? 0 : 6;
}
