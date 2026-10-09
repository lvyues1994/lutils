#pragma once
#include <lutils/compute/Runtime.hpp>
#include <lutils/compute/Shader.hpp>
#include <map>
#include <typeindex>
#include <utility>

namespace lutils::compute {
// std430 byte layouts are independent of native object padding.
inline unsigned char *storageBytes(void *p) { return static_cast<unsigned char *>(p); }
inline unsigned char const *storageBytes(void const *p) {
    return static_cast<unsigned char const *>(p);
}
constexpr std::size_t alignedBytes(std::size_t size, std::size_t alignment) {
    return (size + alignment - 1) / alignment * alignment;
}
constexpr std::size_t alignedWords(std::size_t size, std::size_t alignment) {
    return alignedBytes(size, alignment);
}
template <class T, class = void> struct StorageCodec;
template <class T>
struct StorageCodec<T, std::enable_if_t<(std::is_arithmetic_v<T> && !std::is_same_v<T, bool> &&
                                         (sizeof(T) == 4 || sizeof(T) == 8)) ||
                                        std::is_same_v<T, kernel::half>>> {
    static constexpr std::size_t bytes = sizeof(T), alignmentBytes = bytes;
    static constexpr std::size_t words = (bytes + 3) / 4, alignment = (alignmentBytes + 3) / 4;
    static T read(void const *data) {
        T value;
        std::memcpy(&value, data, sizeof(T));
        return value;
    }
    static void write(void *data, T value) { std::memcpy(data, &value, sizeof(T)); }
};
template <> struct StorageCodec<bool> {
    static constexpr std::size_t bytes = 4, alignmentBytes = 4, words = 1, alignment = 1;
    static bool read(void const *p) { return StorageCodec<Word>::read(p) != 0; }
    static void write(void *p, bool v) { StorageCodec<Word>::write(p, static_cast<Word>(v)); }
};
template <class T, std::size_t N> struct StorageCodec<kernel::Vec<T, N>> {
    static constexpr std::size_t bytes = N * StorageCodec<T>::bytes;
    static constexpr std::size_t alignmentBytes =
        (N == 2 ? 2 : 4) * StorageCodec<T>::alignmentBytes;
    static constexpr std::size_t words = (bytes + 3) / 4, alignment = (alignmentBytes + 3) / 4;
    static kernel::Vec<T, N> read(void const *data) {
        kernel::Vec<T, N> value;
        for (std::size_t i = 0; i < N; ++i)
            value[i] = StorageCodec<T>::read(storageBytes(data) + i * StorageCodec<T>::bytes);
        return value;
    }
    static void write(void *data, kernel::Vec<T, N> const &v) {
        for (std::size_t i = 0; i < N; ++i)
            StorageCodec<T>::write(storageBytes(data) + i * StorageCodec<T>::bytes, v[i]);
    }
};
template <class T, std::size_t N> struct StorageCodec<std::array<T, N>> {
    static_assert(N > 0, "shader arrays must have positive size");
    static constexpr std::size_t alignmentBytes = StorageCodec<T>::alignmentBytes;
    static constexpr std::size_t stride = alignedBytes(StorageCodec<T>::bytes, alignmentBytes);
    static constexpr std::size_t bytes = N * stride;
    static constexpr std::size_t words = (bytes + 3) / 4, alignment = (alignmentBytes + 3) / 4;
    static std::array<T, N> read(void const *data) {
        std::array<T, N> out{};
        for (std::size_t i = 0; i < N; ++i)
            out[i] = StorageCodec<T>::read(storageBytes(data) + i * stride);
        return out;
    }
    static void write(void *data, std::array<T, N> const &v) {
        for (std::size_t i = 0; i < N; ++i)
            StorageCodec<T>::write(storageBytes(data) + i * stride, v[i]);
    }
};
template <class T> struct CpuStorage {
    static constexpr auto stride =
        alignedBytes(StorageCodec<T>::bytes, StorageCodec<T>::alignmentBytes);
    CpuBuffer buffer;
    struct Values {
        std::unique_ptr<T[]> pointer;
        std::size_t count;
        explicit Values(std::size_t n) : pointer(std::make_unique<T[]>(n)), count(n) {}
        T *data() const { return pointer.get(); }
        std::size_t size() const { return count; }
        T &operator[](std::size_t i) { return pointer[i]; }
    } values;
    explicit CpuStorage(CpuBuffer b) : buffer(b), values(b.byteCount() / stride) {
        if (b.image || b.byteCount() % stride)
            throw std::invalid_argument("typed buffer layout");
        for (std::size_t i = 0; i < values.size(); ++i)
            values[i] = StorageCodec<T>::read(storageBytes(b.data) + i * stride);
    }
    void flush() {
        for (std::size_t i = 0; i < values.size(); ++i)
            StorageCodec<T>::write(storageBytes(buffer.data) + i * stride, values[i]);
    }
};
// Word buffers contain live Word objects; image pipelines need no decode or copy.
template <> struct CpuStorage<Word> {
    struct View {
        Word *pointer;
        std::size_t count;
        Word *data() const { return pointer; }
        std::size_t size() const { return count; }
    } values;
    explicit CpuStorage(CpuBuffer b) : values{b.data, b.byteCount() / 4} {
        if (b.image || b.byteCount() % 4)
            throw std::invalid_argument("expected word buffer");
    }
    void flush() {}
};
struct ResourceIdentity {};
template <class T, kernel::Dim D> struct BufferResource {
    using Index = typename kernel::DimTraits<D>::IndexType;
    explicit BufferResource(Index extent)
        : extent_(extent), count_(kernel::DimTraits<D>::product(extent)),
          values_(std::make_unique<T[]>(count_)) {}
    BufferResource(BufferResource const &) = delete;
    BufferResource &operator=(BufferResource const &) = delete;
    BufferResource(BufferResource &&other) noexcept
        : extent_(std::exchange(other.extent_, {})), count_(std::exchange(other.count_, 0)),
          values_(std::move(other.values_)), identity_(std::move(other.identity_)) {}
    BufferResource &operator=(BufferResource &&other) noexcept {
        if (this != &other) {
            extent_ = std::exchange(other.extent_, {});
            count_ = std::exchange(other.count_, 0);
            values_ = std::move(other.values_);
            identity_ = std::move(other.identity_);
        }
        return *this;
    }
    T &operator[](Index index) { return values_[kernel::DimTraits<D>::flatten(index, extent_)]; }
    T const &operator[](Index index) const {
        return values_[kernel::DimTraits<D>::flatten(index, extent_)];
    }
    T *data() { return values_.get(); }
    T const *data() const { return values_.get(); }
    std::size_t size() const { return count_; }
    Index extent() const { return extent_; }
    std::shared_ptr<ResourceIdentity const> identity() const { return identity_; }

  private:
    Index extent_;
    std::size_t count_;
    std::unique_ptr<T[]> values_;
    std::shared_ptr<ResourceIdentity const> identity_ = std::make_shared<ResourceIdentity>();
};
inline Extent3 extent3(std::int32_t x) { return {static_cast<Word>(x), 1, 1}; }
inline Extent3 extent3(kernel::ivec2 p) {
    return {static_cast<Word>(p.x), static_cast<Word>(p.y), 1};
}
inline Extent3 extent3(kernel::ivec3 p) {
    return {static_cast<Word>(p.x), static_cast<Word>(p.y), static_cast<Word>(p.z)};
}
template <kernel::Dim D> typename kernel::DimTraits<D>::IndexType imageExtent(ImageDesc const &d);
template <> inline std::int32_t imageExtent<kernel::Dim::D1>(ImageDesc const &d) {
    return static_cast<std::int32_t>(d.extent.x);
}
template <> inline kernel::ivec2 imageExtent<kernel::Dim::D2>(ImageDesc const &d) {
    return {d.extent.x, d.extent.y};
}
template <> inline kernel::ivec3 imageExtent<kernel::Dim::D3>(ImageDesc const &d) {
    return {d.extent.x, d.extent.y, d.extent.z};
}
template <kernel::ImageFormat G, kernel::Channel C, class P>
typename kernel::GPUFormatTraits<G>::ChannelType imageChannel(P const &p) {
    using F = kernel::GPUFormatTraits<G>;
    using V = typename P::template ChannelType<C>;
    if constexpr (!kernel::HasChannel<P, C>::value)
        return C == kernel::Channel::A ? 1 : 0;
    auto value = p.template get<C>();
    if constexpr ((F::kind == kernel::NumberKind::UNorm || F::kind == kernel::NumberKind::SNorm ||
                   F::kind == kernel::NumberKind::Float) &&
                  std::is_integral_v<V>)
        return kernel::ChannelConverter<V, typename F::ChannelType>::apply(value);
    else
        return kernel::saturated<typename F::ChannelType>(static_cast<double>(value));
}
template <kernel::ImageFormat G, class P> auto encodePixel(P const &p) {
    return typename kernel::GPUFormatTraits<G>::VectorType{
        imageChannel<G, kernel::Channel::R>(p), imageChannel<G, kernel::Channel::G>(p),
        imageChannel<G, kernel::Channel::B>(p), imageChannel<G, kernel::Channel::A>(p)};
}
template <kernel::ImageFormat G, kernel::Channel C, class P, class V>
void decodeChannel(P &p, V value) {
    using T = typename P::template ChannelType<C>;
    constexpr auto kind = kernel::GPUFormatTraits<G>::kind;
    if constexpr (std::is_integral_v<T> &&
                  (kind == kernel::NumberKind::UNorm || kind == kernel::NumberKind::SNorm ||
                   kind == kernel::NumberKind::Float))
        p.template set<C>(kernel::ChannelConverter<V, T>::apply(value));
    else
        p.template set<C>(kernel::saturated<T>(static_cast<double>(value)));
}
template <kernel::ImageFormat G, class P>
void decodePixel(P &p, typename kernel::GPUFormatTraits<G>::VectorType v) {
    decodeChannel<G, kernel::Channel::R>(p, v.x);
    decodeChannel<G, kernel::Channel::G>(p, v.y);
    decodeChannel<G, kernel::Channel::B>(p, v.z);
    decodeChannel<G, kernel::Channel::A>(p, v.w);
}
template <class K> struct KernelTraits; // Generated for each annotated kernel.
struct BuiltinScope {
    kernel::uvec3 global = kernel::gl_GlobalInvocationID, local = kernel::gl_LocalInvocationID,
                  group = kernel::gl_WorkGroupID, groups = kernel::gl_NumWorkGroups,
                  size = kernel::gl_WorkGroupSize;
    Word index = kernel::gl_LocalInvocationIndex;
    ~BuiltinScope() {
        kernel::gl_GlobalInvocationID = global;
        kernel::gl_LocalInvocationID = local;
        kernel::gl_WorkGroupID = group;
        kernel::gl_NumWorkGroups = groups;
        kernel::gl_WorkGroupSize = size;
        kernel::gl_LocalInvocationIndex = index;
    }
};
inline void setGrid(Extent3 extent, Extent3 local) {
    kernel::gl_WorkGroupSize = {local.x, local.y, local.z};
    kernel::gl_NumWorkGroups = {extent.x / local.x + (extent.x % local.x != 0),
                                extent.y / local.y + (extent.y % local.y != 0),
                                extent.z / local.z + (extent.z % local.z != 0)};
}
inline void setInvocation(Extent3 local, Word x, Word y, Word z) {
    kernel::gl_GlobalInvocationID = {x, y, z};
    kernel::gl_LocalInvocationID = {x % local.x, y % local.y, z % local.z};
    kernel::gl_WorkGroupID = {x / local.x, y / local.y, z / local.z};
    kernel::gl_LocalInvocationIndex = (z % local.z * local.y + y % local.y) * local.x + x % local.x;
}
template <Word LocalX, Word LocalY, Word LocalZ, class K>
void executeCpu(K const &value, Extent3 extent, CpuExecution &execution, bool grouped = false) {
    static_assert(LocalX > 0 && LocalY > 0 && LocalZ > 0, "local size must be positive");
    constexpr Extent3 local{LocalX, LocalY, LocalZ};
    if (!extent.x || !extent.y || !extent.z)
        return;
    if (std::size_t{extent.x} > SIZE_MAX / extent.y / extent.z)
        throw std::overflow_error("CPU dispatch size");
    if (grouped) {
        if (extent.x % local.x || extent.y % local.y || extent.z % local.z)
            throw std::invalid_argument("shared kernels require complete workgroups");
        execution.workgroup(std::size_t{local.x} * local.y * local.z, [&](std::size_t lane) {
            BuiltinScope scope;
            setGrid(extent, local);
            auto k = value;
            auto x = static_cast<Word>(lane % local.x);
            auto y = static_cast<Word>(lane / local.x % local.y);
            auto z = static_cast<Word>(lane / local.x / local.y);
            for (Word gz = 0; gz < extent.z / local.z; ++gz)
                for (Word gy = 0; gy < extent.y / local.y; ++gy)
                    for (Word gx = 0; gx < extent.x / local.x; ++gx) {
                        setInvocation(local, gx * local.x + x, gy * local.y + y, gz * local.z + z);
                        k.main();
                        // All lanes finish using shared storage before it is reused.
                        kernel::barrier();
                    }
        });
        return;
    }
    execution.parallelFor(std::size_t{extent.x} * extent.y * extent.z,
                          [&](std::size_t begin, std::size_t end) {
                              BuiltinScope scope;
                              setGrid(extent, local);
                              auto k = value;
                              auto x = static_cast<Word>(begin % extent.x);
                              auto y = static_cast<Word>(begin / extent.x % extent.y);
                              auto z = static_cast<Word>(begin / extent.x / extent.y);
                              for (auto i = begin; i < end; ++i) {
                                  setInvocation(local, x, y, z);
                                  k.main();
                                  if (++x == extent.x) {
                                      x = 0;
                                      if (++y == extent.y) {
                                          y = 0;
                                          ++z;
                                      }
                                  }
                              }
                          });
}
// Owns one device and its resident copies. Kernels borrow host resources until recording.
struct Backend {
    virtual ~Backend() = default;
    Backend(Backend const &) = delete;
    Backend &operator=(Backend const &) = delete;
    Backend(Backend &&) noexcept = default;
    Backend &operator=(Backend &&) noexcept = default;
    explicit Backend(std::unique_ptr<Device> device) : device_(std::move(device)) {
        if (!device_)
            throw std::invalid_argument("null device");
    }
    Device &device() { return *device_; }
    template <class T, kernel::Dim D> Result<void> uploadBuffer(BufferResource<T, D> *resource) {
        if (!resource || !resource->identity())
            return Error{ErrorCode::InvalidArgument, "null resource"};
        constexpr auto stride =
            alignedBytes(StorageCodec<T>::bytes, StorageCodec<T>::alignmentBytes);
        if (resource->size() > (SIZE_MAX - 3) / stride)
            return Error{ErrorCode::Overflow, "typed buffer size"};
        auto bytes = resource->size() * stride;
        std::vector<Word> words((bytes + 3) / 4, 0);
        for (std::size_t i = 0; i < resource->size(); ++i)
            StorageCodec<T>::write(storageBytes(words.data()) + i * stride, resource->data()[i]);
        return upload(resource->identity(), {}, std::move(words), bytes);
    }
    template <kernel::ImageFormat G, class P, kernel::Dim D>
    Result<void> uploadImage(BufferResource<P, D> *resource) {
        if (!resource || !resource->identity())
            return Error{ErrorCode::InvalidArgument, "null resource"};
        ImageDesc desc{G, static_cast<Word>(D), extent3(resource->extent())};
        auto count = imageWordCount(desc);
        if (!count)
            return count.error();
        std::vector<Word> words(count.value(), 0);
        auto *bytes = reinterpret_cast<unsigned char *>(words.data());
        for (std::size_t i = 0; i < resource->size(); ++i)
            kernel::GPUFormatTraits<G>::store(bytes + i * kernel::GPUFormatTraits<G>::bytes,
                                              encodePixel<G>(resource->data()[i]));
        return upload(resource->identity(), desc, std::move(words));
    }
    template <class T, kernel::Dim D> Result<void> downloadBuffer(BufferResource<T, D> *resource) {
        auto handle = find(resource ? resource->identity() : nullptr, {});
        if (!handle)
            return handle.error();
        auto words = device_->download(handle.value());
        if (!words)
            return words.error();
        constexpr auto stride =
            alignedBytes(StorageCodec<T>::bytes, StorageCodec<T>::alignmentBytes);
        if (handle.value()->byteCount() != resource->size() * stride)
            return Error{ErrorCode::InvalidArgument, "resource size changed"};
        for (std::size_t i = 0; i < resource->size(); ++i)
            resource->data()[i] =
                StorageCodec<T>::read(storageBytes(words.value().data()) + i * stride);
        return {};
    }
    template <kernel::ImageFormat G, class P, kernel::Dim D>
    Result<void> downloadImage(BufferResource<P, D> *resource) {
        if (!resource || !resource->identity())
            return Error{ErrorCode::InvalidArgument, "null resource"};
        ImageDesc desc{G, static_cast<Word>(D), extent3(resource->extent())};
        auto handle = find(resource->identity(), desc);
        if (!handle)
            return handle.error();
        auto words = device_->download(handle.value());
        if (!words)
            return words.error();
        auto const *bytes = reinterpret_cast<unsigned char const *>(words.value().data());
        for (std::size_t i = 0; i < resource->size(); ++i)
            decodePixel<G>(resource->data()[i], kernel::GPUFormatTraits<G>::load(
                                                    bytes + i * kernel::GPUFormatTraits<G>::bytes));
        return {};
    }
    template <class T, Word S>
    Result<BufferHandle> resolve(kernel::BufferBinding<T, S> const &binding) {
        return find(binding.resource() ? binding.resource()->identity() : nullptr, {});
    }
    template <kernel::ImageFormat G, kernel::Dim D, class P, Word S>
    Result<BufferHandle> resolve(kernel::ImageBinding<G, D, P, S> const &binding) {
        auto *r = binding.resource();
        if (!r)
            return Error{ErrorCode::InvalidArgument, "unbound image"};
        return find(r->identity(), ImageDesc{G, static_cast<Word>(D), extent3(r->extent())});
    }
    template <class B> Result<void> bindBuffer(B const &binding) {
        auto r = resolve(binding);
        return r ? Result<void>{} : Result<void>{r.error()};
    }
    template <class B> Result<void> bindImage(B const &binding) { return bindBuffer(binding); }
    template <class T, Word S> Result<void> bindUniform(kernel::Uniform<T, S> const &) {
        return {};
    }
    template <class K> Result<KernelHandle> useKernel(K const &) {
        auto found = kernels_.find(typeid(K));
        if (found != kernels_.end())
            return found->second;
        auto handle = device_->createKernel(KernelTraits<K>::source());
        if (!handle)
            return handle.error();
        kernels_.emplace(typeid(K), handle.value());
        return handle.value();
    }
    template <class K> Result<void> record(CommandList &commands, K const &kernel, Extent3 extent) {
        auto handle = useKernel(kernel);
        if (!handle)
            return handle.error();
        return KernelTraits<K>::record(*this, commands, handle.value(), kernel, extent);
    }
    template <class K>
    Result<std::shared_ptr<Completion>> execute(K const &kernel, kernel::uvec3 extent) {
        CommandList commands;
        auto r = record(commands, kernel, {extent.x, extent.y, extent.z});
        return r ? device_->submit(commands) : Result<std::shared_ptr<Completion>>{r.error()};
    }

  private:
    struct Resident {
        std::weak_ptr<ResourceIdentity const> identity;
        std::optional<ImageDesc> image;
        BufferHandle handle;
    };
    std::unique_ptr<Device> device_;
    std::vector<Resident> resources_;
    std::map<std::type_index, KernelHandle> kernels_;
    Result<BufferHandle> find(std::shared_ptr<ResourceIdentity const> const &id,
                              std::optional<ImageDesc> const &image) {
        if (!id)
            return Error{ErrorCode::InvalidArgument, "unbound resource"};
        for (auto const &r : resources_)
            if (r.identity.lock() == id && r.image == image)
                return r.handle;
        return Error{ErrorCode::InvalidArgument, "resource has not been uploaded to this backend"};
    }
    Result<void> upload(std::shared_ptr<ResourceIdentity const> id, std::optional<ImageDesc> image,
                        std::vector<Word> words, std::size_t bytes = 0) {
        if (!id)
            return Error{ErrorCode::InvalidArgument, "moved-from resource"};
        resources_.erase(std::remove_if(resources_.begin(), resources_.end(),
                                        [](Resident const &r) { return r.identity.expired(); }),
                         resources_.end());
        auto handle = find(id, image);
        if (!handle) {
            handle = image ? device_->createImage(*image) : device_->createBufferBytes(bytes);
            if (!handle)
                return handle.error();
            auto uploaded = device_->upload(handle.value(), words);
            if (!uploaded)
                return uploaded;
            resources_.push_back({std::move(id), image, handle.value()});
            return {};
        }
        auto uploaded = device_->upload(handle.value(), words);
        if (!uploaded)
            resources_.erase(
                std::remove_if(resources_.begin(), resources_.end(),
                               [&](Resident const &r) { return r.handle == handle.value(); }),
                resources_.end());
        return uploaded;
    }
};
inline std::unique_ptr<Device> requiredDevice(Result<std::unique_ptr<Device>> result) {
    if (!result)
        throw std::runtime_error(result.error().message);
    return std::move(result).value();
}
namespace cpu {
struct CPUBackend final : Backend {
    CPUBackend() : Backend(requiredDevice(createCpuDevice())) {}
};
} // namespace cpu
namespace gpu {
struct GPUBackend final : Backend {
    GPUBackend() : Backend(requiredDevice(createVulkanDevice())) {}
};
} // namespace gpu
} // namespace lutils::compute
