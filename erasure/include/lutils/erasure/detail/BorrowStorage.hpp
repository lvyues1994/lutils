#pragma once

#include <lutils/erasure/detail/Model.hpp>
#include <memory>
#include <new>

namespace lutils::erasure::detail {

template <template <class> class I, class Link>
struct BorrowStorage {
    BorrowStorage() noexcept = default;
    BorrowStorage(BorrowStorage const &other) noexcept { copyFrom(other); }
    BorrowStorage &operator=(BorrowStorage const &other) noexcept {
        if (this != &other) {
            reset();
            copyFrom(other);
        }
        return *this;
    }
    BorrowStorage(BorrowStorage &&other) noexcept : BorrowStorage(other) {
        other.reset();
    }
    BorrowStorage &operator=(BorrowStorage &&other) noexcept {
        if (this != &other) {
            *this = other;
            other.reset();
        }
        return *this;
    }
    ~BorrowStorage() { reset(); }

    template <class T>
    void bind(T *target) noexcept {
        using Model = ReferenceModel<I, T>;
        static_assert(sizeof(Model) <= sizeof(buffer_),
                      "reference model exceeds the inline adapter buffer");
        static_assert(alignof(Model) <= alignof(decltype(buffer_)),
                      "reference model exceeds the inline adapter alignment");
        static_assert(std::is_nothrow_constructible_v<Model, T *>);
        reset();
        if (not target) { return; }
        model_ = ::new (static_cast<void *>(&buffer_)) Model{target};
        ops_ = &operations<Model>;
    }

    void reset() noexcept {
        if (model_) { ops_->destroy(model_); }
        model_ = nullptr;
        ops_ = nullptr;
    }
    bool empty() const noexcept { return model_ == nullptr; }
    Abstract<I> *get() const noexcept {
        if (empty()) { std::terminate(); }
        return model_;
    }

private:
    struct Operations {
        Abstract<I> *(*copy)(Abstract<I> const *, void *) noexcept;
        void (*destroy)(Abstract<I> *) noexcept;
    };
    template <class Model>
    inline static constexpr Operations operations{
        [](Abstract<I> const *source, void *destination) noexcept -> Abstract<I> * {
            return ::new (destination) Model{
                static_cast<Model const *>(source)->_te_target()};
        },
        [](Abstract<I> *model) noexcept {
            static_cast<Model *>(model)->~Model();
        }};

    void copyFrom(BorrowStorage const &other) noexcept {
        if (other.empty()) { return; }
        model_ = other.ops_->copy(other.model_, &buffer_);
        ops_ = other.ops_;
    }

    std::aligned_storage_t<sizeof(ReferenceLayout<I>),
                           alignof(ReferenceLayout<I>)> buffer_;
    Abstract<I> *model_ = nullptr;
    Operations const *ops_ = nullptr;
};

template <template <class> class I, class Self>
void bindInterface(Self &self, BorrowStorage<I> &out) noexcept {
    if constexpr (Self::_te_kind == ModelKind::abstract) {
        std::terminate();
    } else if constexpr (Self::_te_kind == ModelKind::proxy) {
        // Redispatch so that the destination binds the final concrete object.
        value(self)._te_bind(out);
    } else {
        out.bind(std::addressof(value(self)));
    }
}

} // namespace lutils::erasure::detail
