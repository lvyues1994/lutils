#pragma once

#include <lutils/erasure/Lifetime.hpp>
#include <lutils/erasure/detail/BorrowStorage.hpp>
#include <cstddef>
#include <memory>
#include <new>

namespace lutils::erasure {

struct HeapStorage {};

template <std::size_t Bytes = 3 * sizeof(void *),
          std::size_t Alignment = alignof(std::max_align_t)>
struct SmallBufferStorage {
    static_assert(Bytes > 0, "small buffer capacity must be positive");
    static_assert(Alignment > 0 and (Alignment & (Alignment - 1)) == 0,
                  "small buffer alignment must be a power of two");
};

namespace detail {
template <class Policy> struct StorageBuffer;
template <> struct StorageBuffer<HeapStorage> {
    template <class Model> static constexpr bool fits = false;
    void *data() noexcept { return nullptr; }
};
template <std::size_t Bytes, std::size_t Alignment>
struct StorageBuffer<SmallBufferStorage<Bytes, Alignment>> {
    template <class Model>
    static constexpr bool fits =
        sizeof(Model) <= Bytes and alignof(Model) <= Alignment;
    void *data() noexcept { return &buffer_; }
private:
    std::aligned_storage_t<Bytes, Alignment> buffer_;
};

template <template <class> class I, class Policy, class Link = I<AbstractRoot>>
struct OwnerStorage : private StorageBuffer<Policy> {
    OwnerStorage() noexcept = default;
    OwnerStorage(OwnerStorage const &other) {
        if (other.empty()) { return; }
        if (not other.ops_->copy) { std::terminate(); }
        model_ = other.ops_->copy(other.model_, this->data());
        ops_ = other.ops_;
    }
    OwnerStorage &operator=(OwnerStorage const &other) {
        if (this != &other) {
            auto candidate = OwnerStorage{other};
            swap(candidate);
        }
        return *this;
    }
    OwnerStorage(OwnerStorage &&other) noexcept { take(other); }
    OwnerStorage &operator=(OwnerStorage &&other) noexcept {
        if (this != &other) {
            reset();
            take(other);
        }
        return *this;
    }
    ~OwnerStorage() { reset(); }

    template <class T, class... Args>
    T &emplace(Args &&...args) {
        auto candidate = OwnerStorage{};
        candidate.template construct<T>(std::forward<Args>(args)...);
        swap(candidate);
        return static_cast<ValueModel<I, T> *>(model_)->_te_value();
    }
    void reset() noexcept {
        if (model_) { ops_->destroy(model_); }
        model_ = nullptr;
        ops_ = nullptr;
    }
    void swap(OwnerStorage &other) noexcept {
        if (this == &other) { return; }
        auto temporary = OwnerStorage{std::move(other)};
        other.take(*this);
        take(temporary);
    }
    bool empty() const noexcept { return model_ == nullptr; }
    Abstract<I> &get() noexcept {
        if (empty()) { std::terminate(); }
        return *model_;
    }
    Abstract<I> const &get() const noexcept {
        if (empty()) { std::terminate(); }
        return *model_;
    }

private:
    struct Operations {
        void (*destroy)(Abstract<I> *) noexcept;
        Abstract<I> *(*copy)(Abstract<I> const *, void *);
        Abstract<I> *(*relocate)(Abstract<I> *, void *) noexcept;
    };

    template <class T>
    static constexpr bool useInline =
        StorageBuffer<Policy>::template fits<ValueModel<I, T>> and
        std::is_nothrow_constructible_v<ValueModel<I, T>,
                                       std::in_place_type_t<T>, T &&>;

    template <class T, class... Args>
    static Abstract<I> *create(void *destination, Args &&...args) {
        using Model = ValueModel<I, T>;
        if constexpr (useInline<T>) {
            return ::new (destination) Model{
                std::in_place_type<T>, std::forward<Args>(args)...};
        } else {
            return std::make_unique<Model>(
                std::in_place_type<T>, std::forward<Args>(args)...).release();
        }
    }
    template <class T>
    static constexpr Operations makeOperations() noexcept {
        using Model = ValueModel<I, T>;
        auto result = Operations{};
        result.destroy = [](Abstract<I> *model) noexcept {
            if constexpr (useInline<T>) {
                static_cast<Model *>(model)->~Model();
            } else {
                auto owner = std::unique_ptr<Model>{static_cast<Model *>(model)};
            }
        };
        if constexpr (canCopy<I>) {
            result.copy = [](Abstract<I> const *source, void *destination) {
                return create<T>(destination,
                    static_cast<Model const *>(source)->_te_value());
            };
        }
        if constexpr (useInline<T>) {
            result.relocate = [](Abstract<I> *source, void *destination) noexcept {
                auto *model = static_cast<Model *>(source);
                auto *moved = create<T>(destination, std::move(*model)._te_value());
                model->~Model();
                return moved;
            };
        }
        return result;
    }
    template <class T>
    inline static constexpr Operations operations = makeOperations<T>();

    template <class T, class... Args>
    void construct(Args &&...args) {
        static_assert(std::is_same_v<T, std::decay_t<T>>,
                      "owned type must be an unqualified object type");
        static_assert(std::is_nothrow_destructible_v<T>,
                      "owned type must have a nonthrowing destructor");
        static_assert(not canCopy<I> or std::is_copy_constructible_v<T>,
                      "ICopyable requires a copy-constructible target");
        model_ = create<T>(this->data(), std::forward<Args>(args)...);
        ops_ = &operations<T>;
    }
    void take(OwnerStorage &other) noexcept {
        if (other.empty()) { return; }
        ops_ = other.ops_;
        model_ = ops_->relocate
            ? ops_->relocate(other.model_, this->data()) : other.model_;
        other.model_ = nullptr;
        other.ops_ = nullptr;
    }

    Abstract<I> *model_ = nullptr;
    Operations const *ops_ = nullptr;
};
} // namespace detail
} // namespace lutils::erasure
