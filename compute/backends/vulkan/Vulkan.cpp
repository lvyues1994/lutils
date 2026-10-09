#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <lutils/compute/Vulkan.hpp>
#include <optional>
#include <stdexcept>
#include <vulkan/vulkan.h>

namespace lutils::compute {
namespace {
struct Failure : std::runtime_error {
    using std::runtime_error::runtime_error;
};
void check(VkResult result, char const *operation) {
    if (result != VK_SUCCESS)
        throw Failure{std::string{operation} + " failed (VkResult " + std::to_string(result) + ")"};
}
Error deviceError(std::exception const &e) { return {ErrorCode::Device, e.what()}; }
bool supportedModule(std::vector<Word> const &words, Extent3 expected) {
    if (words.size() < 5 || words[0] != 0x07230203u || words[1] > 0x00010300u)
        return false;
    bool localSize = false;
    for (std::size_t i = 5; i < words.size();) {
        auto size = words[i] >> 16;
        auto opcode = words[i] & 0xffffu;
        if (!size || size > words.size() - i)
            return false;
        // SPIR-V OpExecutionMode, LocalSize must match the host dispatch metadata.
        if (opcode == 16u && size >= 3 && words[i + 2] == 17u) {
            if (size != 6 || words[i + 3] != expected.x || words[i + 4] != expected.y ||
                words[i + 5] != expected.z)
                return false;
            localSize = true;
        }
        i += size;
    }
    return localSize;
}
template <class T> T structure(VkStructureType type) {
    T result{};
    result.sType = type;
    return result;
}

bool hasLayer(char const *name) {
    std::uint32_t count = 0;
    check(vkEnumerateInstanceLayerProperties(&count, nullptr), "enumerate layers");
    std::vector<VkLayerProperties> layers(count);
    check(vkEnumerateInstanceLayerProperties(&count, layers.data()), "enumerate layers");
    return std::any_of(layers.begin(), layers.end(),
                       [&](auto const &layer) { return std::strcmp(layer.layerName, name) == 0; });
}
bool hasExtension(char const *name, char const *layer = nullptr) {
    std::uint32_t count = 0;
    check(vkEnumerateInstanceExtensionProperties(layer, &count, nullptr), "enumerate extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data()),
          "enumerate extensions");
    return std::any_of(extensions.begin(), extensions.end(), [&](auto const &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}
VKAPI_ATTR VkBool32 VKAPI_CALL
validationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                  VkDebugUtilsMessengerCallbackDataEXT const *message, void *data) {
    auto &report = *static_cast<ValidationReport *>(data);
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        report.errors.fetch_add(1, std::memory_order_relaxed);
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        report.warnings.fetch_add(1, std::memory_order_relaxed);
    std::fprintf(stderr, "Vulkan validation: %s\n", message->pMessage);
    return VK_FALSE;
}

struct DeviceState {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t family = 0;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VulkanOptions options;
    std::uint32_t timestampBits = 0;
    ComputeCapabilities capabilities;
    bool uniformBuffer16 = false;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    PFN_vkDestroyDebugUtilsMessengerEXT destroyMessenger = nullptr;
    DeviceState() = default;
    DeviceState(DeviceState const &) = delete;
    DeviceState &operator=(DeviceState const &) = delete;
    ~DeviceState() {
        if (device)
            vkDestroyDevice(device, nullptr);
        if (messenger)
            destroyMessenger(instance, messenger, nullptr);
        if (instance)
            vkDestroyInstance(instance, nullptr);
    }
    void initialize(VulkanOptions requested) {
        options = std::move(requested);
        auto app = structure<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName = "lutils compute";
        app.apiVersion = VK_API_VERSION_1_1;
        auto create = structure<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
        create.pApplicationInfo = &app;
        char const *layer = "VK_LAYER_KHRONOS_validation";
        char const *extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
                                    VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME};
        auto debug = structure<VkDebugUtilsMessengerCreateInfoEXT>(
            VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
        auto validation =
            structure<VkValidationFeaturesEXT>(VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT);
        VkValidationFeatureEnableEXT enabled =
            VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
        if (options.enableValidation) {
            if (!hasLayer(layer))
                throw Failure{"VK_LAYER_KHRONOS_validation is unavailable"};
            if (!hasExtension(extensions[0]) || !hasExtension(extensions[1], layer))
                throw Failure{"validation requires debug-utils and validation-features "
                              "extensions"};
            if (!options.validationReport)
                options.validationReport = std::make_shared<ValidationReport>();
            debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debug.pfnUserCallback = validationMessage;
            debug.pUserData = options.validationReport.get();
            validation.enabledValidationFeatureCount = 1;
            validation.pEnabledValidationFeatures = &enabled;
            validation.pNext = &debug;
            create.pNext = &validation;
            create.enabledLayerCount = 1;
            create.ppEnabledLayerNames = &layer;
            create.enabledExtensionCount = 2;
            create.ppEnabledExtensionNames = extensions;
        }
        check(vkCreateInstance(&create, nullptr, &instance), "vkCreateInstance");
        if (options.enableValidation) {
            auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
            destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (!createMessenger || !destroyMessenger)
                throw Failure{"debug-utils entry points are unavailable"};
            check(createMessenger(instance, &debug, nullptr, &messenger), "create debug messenger");
        }
        std::uint32_t count = 0;
        check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "enumerate devices");
        std::vector<VkPhysicalDevice> devices(count);
        check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "enumerate devices");
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_1)
                continue;
            if (options.requireHardware &&
                props.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU &&
                props.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                continue;
            std::uint32_t n = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, nullptr);
            std::vector<VkQueueFamilyProperties> queues(n);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &n, queues.data());
            for (std::uint32_t i = 0; i < n; ++i) {
                if (options.enableTimestamps &&
                    (!queues[i].timestampValidBits || props.limits.timestampPeriod <= 0))
                    continue;
                if (queues[i].queueCount && (queues[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                    if (!physical || (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU &&
                                      props.deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU)) {
                        physical = candidate;
                        family = i;
                        properties = props;
                        timestampBits = queues[i].timestampValidBits;
                    }
                    break;
                }
            }
        }
        if (!physical)
            throw Failure{options.requireHardware ? "no hardware Vulkan 1.1 compute device"
                                                  : "no Vulkan 1.1 compute device"};
        if (options.enableTimestamps && (!timestampBits || properties.limits.timestampPeriod <= 0))
            throw Failure{"selected compute queue does not support timestamps"};
        float priority = 1.0f;
        auto q = structure<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        q.queueFamilyIndex = family;
        q.queueCount = 1;
        q.pQueuePriorities = &priority;
        auto d = structure<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        d.queueCreateInfoCount = 1;
        d.pQueueCreateInfos = &q;
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physical, &features);
        VkPhysicalDeviceFeatures enabledFeatures{};
        enabledFeatures.shaderStorageImageExtendedFormats =
            features.shaderStorageImageExtendedFormats;
        enabledFeatures.shaderFloat64 = features.shaderFloat64;
        d.pEnabledFeatures = &enabledFeatures;
        std::uint32_t extensionCount = 0;
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, nullptr),
              "enumerate device extensions");
        std::vector<VkExtensionProperties> available(extensionCount);
        check(vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount,
                                                   available.data()),
              "enumerate device extensions");
        char const *halfExtension = VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME;
        bool hasHalf = std::any_of(available.begin(), available.end(), [&](auto const &e) {
            return std::strcmp(e.extensionName, halfExtension) == 0;
        });
        auto storage16 = structure<VkPhysicalDevice16BitStorageFeatures>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES);
        auto arithmetic16 = structure<VkPhysicalDeviceShaderFloat16Int8Features>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES);
        auto queried =
            structure<VkPhysicalDeviceFeatures2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
        queried.pNext = &storage16;
        if (hasHalf)
            storage16.pNext = &arithmetic16;
        vkGetPhysicalDeviceFeatures2(physical, &queried);
        if (!options.enableFloat16) {
            storage16.storageBuffer16BitAccess = VK_FALSE;
            storage16.uniformAndStorageBuffer16BitAccess = VK_FALSE;
            storage16.storagePushConstant16 = VK_FALSE;
            arithmetic16.shaderFloat16 = VK_FALSE;
        }
        storage16.storageInputOutput16 = VK_FALSE;
        arithmetic16.shaderInt8 = VK_FALSE;
        d.pNext = &storage16;
        if (hasHalf && options.enableFloat16) {
            d.enabledExtensionCount = 1;
            d.ppEnabledExtensionNames = &halfExtension;
        } else
            storage16.pNext = nullptr;
        capabilities = {arithmetic16.shaderFloat16 != VK_FALSE,
                        storage16.storageBuffer16BitAccess != VK_FALSE,
                        storage16.storagePushConstant16 != VK_FALSE,
                        properties.limits.maxComputeWorkGroupInvocations,
                        properties.limits.maxComputeSharedMemorySize};
        uniformBuffer16 = storage16.uniformAndStorageBuffer16BitAccess != VK_FALSE;
        check(vkCreateDevice(physical, &d, nullptr, &device), "vkCreateDevice");
        vkGetDeviceQueue(device, family, 0, &queue);
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    }
};
enum class BufferKind { Storage, Upload, Readback };
struct VulkanBuffer final : Buffer {
    std::shared_ptr<DeviceState> state;
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void *mapped = nullptr;
    std::size_t words = 0;
    std::size_t logicalBytes = 0;
    bool coherent = false;
    bool initialized = false;
    explicit VulkanBuffer(std::shared_ptr<DeviceState> s) : state(std::move(s)) {}
    VulkanBuffer(VulkanBuffer const &) = delete;
    VulkanBuffer &operator=(VulkanBuffer const &) = delete;
    ~VulkanBuffer() override {
        if (mapped)
            vkUnmapMemory(state->device, memory);
        if (buffer)
            vkDestroyBuffer(state->device, buffer, nullptr);
        if (memory)
            vkFreeMemory(state->device, memory, nullptr);
    }
    std::size_t wordCount() const noexcept override { return words; }
    std::size_t byteCount() const noexcept override {
        return logicalBytes ? logicalBytes : words * 4;
    }
    void initialize(std::size_t count, BufferKind kind = BufferKind::Storage) {
        words = count;
        auto create = structure<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
        create.size = std::max<std::size_t>(1, words) * sizeof(Word);
        create.usage = kind == BufferKind::Upload ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT
                       : kind == BufferKind::Readback
                           ? VK_BUFFER_USAGE_TRANSFER_DST_BIT
                           : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(state->device, &create, nullptr, &buffer), "vkCreateBuffer");
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(state->device, buffer, &req);
        std::uint32_t index = state->memory.memoryTypeCount;
        bool const local = kind == BufferKind::Storage && state->options.deviceLocal;
        VkMemoryPropertyFlags required =
            local ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT : VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        bool const cached = kind == BufferKind::Readback ||
                            (kind == BufferKind::Storage && state->options.preferHostCached);
        unsigned bestScore = 0;
        for (std::uint32_t i = 0; i < state->memory.memoryTypeCount; ++i) {
            auto flags = state->memory.memoryTypes[i].propertyFlags;
            if ((req.memoryTypeBits & (1u << i)) && (flags & required)) {
                unsigned score =
                    (cached && (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? 2u : 0u) +
                    (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ? 1u : 0u);
                if (index == state->memory.memoryTypeCount || score > bestScore) {
                    index = i;
                    bestScore = score;
                }
            }
        }
        if (index == state->memory.memoryTypeCount)
            throw Failure{"buffer has no compatible memory type"};
        coherent = (state->memory.memoryTypes[index].propertyFlags &
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        auto allocation = structure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        allocation.allocationSize = req.size;
        allocation.memoryTypeIndex = index;
        check(vkAllocateMemory(state->device, &allocation, nullptr, &memory), "vkAllocateMemory");
        check(vkBindBufferMemory(state->device, buffer, memory, 0), "vkBindBufferMemory");
        if (!local) {
            check(vkMapMemory(state->device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
            if (kind == BufferKind::Storage) {
                std::memset(mapped, 0, static_cast<std::size_t>(create.size));
                flush();
                initialized = true;
            }
        }
        if (kind != BufferKind::Storage && state->options.statistics)
            state->options.statistics->stagingBuffersCreated.fetch_add(1,
                                                                       std::memory_order_relaxed);
    }
    void flush() {
        if (coherent)
            return;
        auto range = structure<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(vkFlushMappedMemoryRanges(state->device, 1, &range), "vkFlushMappedMemoryRanges");
    }
    void invalidate() {
        if (coherent)
            return;
        auto range = structure<VkMappedMemoryRange>(VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE);
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        check(vkInvalidateMappedMemoryRanges(state->device, 1, &range),
              "vkInvalidateMappedMemoryRanges");
    }
};
VkFormat imageFormat(kernel::ImageFormat format) {
    switch (format) {
#define LUTILS_VK_FORMAT(N, G, T, K, C, V)                                                         \
    case kernel::ImageFormat::N:                                                                   \
        return VK_FORMAT_##V;
        LUTILS_IMAGE_FORMATS(LUTILS_VK_FORMAT)
#undef LUTILS_VK_FORMAT
    }
    throw Failure{"unknown image format"};
}
struct VulkanImage final : Buffer {
    std::shared_ptr<DeviceState> state;
    ImageDesc desc;
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    std::size_t words;
    bool initialized = false;
    VulkanImage(std::shared_ptr<DeviceState> s, ImageDesc d, std::size_t count)
        : state(std::move(s)), desc(d), words(count) {}
    ~VulkanImage() override {
        if (view)
            vkDestroyImageView(state->device, view, nullptr);
        if (image)
            vkDestroyImage(state->device, image, nullptr);
        if (memory)
            vkFreeMemory(state->device, memory, nullptr);
    }
    std::size_t wordCount() const noexcept override { return words; }
    std::optional<ImageDesc> imageDescription() const override { return desc; }
    void initialize() {
        auto format = imageFormat(desc.format);
        auto type = desc.dimensions == 1   ? VK_IMAGE_TYPE_1D
                    : desc.dimensions == 2 ? VK_IMAGE_TYPE_2D
                                           : VK_IMAGE_TYPE_3D;
        auto viewType = desc.dimensions == 1   ? VK_IMAGE_VIEW_TYPE_1D
                        : desc.dimensions == 2 ? VK_IMAGE_VIEW_TYPE_2D
                                               : VK_IMAGE_VIEW_TYPE_3D;
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(state->physical, format, &properties);
        constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                                                VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                                                VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        if ((properties.optimalTilingFeatures & needed) != needed)
            throw Failure{
                "device does not support storage/transfer for the requested image format"};
        auto create = structure<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        create.imageType = type;
        create.format = format;
        create.extent = {desc.extent.x, desc.extent.y, desc.extent.z};
        create.mipLevels = 1;
        create.arrayLayers = 1;
        create.samples = VK_SAMPLE_COUNT_1_BIT;
        create.tiling = VK_IMAGE_TILING_OPTIMAL;
        create.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        create.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageFormatProperties limits{};
        check(vkGetPhysicalDeviceImageFormatProperties(state->physical, format, type, create.tiling,
                                                       create.usage, 0, &limits),
              "image format support");
        if (desc.extent.x > limits.maxExtent.width || desc.extent.y > limits.maxExtent.height ||
            desc.extent.z > limits.maxExtent.depth)
            throw Failure{"image exceeds device dimensions"};
        check(vkCreateImage(state->device, &create, nullptr, &image), "create image");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(state->device, image, &req);
        auto alloc = structure<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = state->memory.memoryTypeCount;
        for (std::uint32_t i = 0; i < state->memory.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) && (state->memory.memoryTypes[i].propertyFlags &
                                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                alloc.memoryTypeIndex = i;
                break;
            }
        if (alloc.memoryTypeIndex == state->memory.memoryTypeCount)
            throw Failure{"image has no device-local memory"};
        check(vkAllocateMemory(state->device, &alloc, nullptr, &memory), "allocate image memory");
        check(vkBindImageMemory(state->device, image, memory, 0), "bind image memory");
        auto info = structure<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        info.image = image;
        info.viewType = viewType;
        info.format = format;
        info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(state->device, &info, nullptr, &view), "create image view");
    }
};
VkDescriptorType bindingType(KernelSource const &source, std::size_t i) {
    return !source.resources.empty() && source.resources[i].image
               ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
               : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
}
struct VulkanKernel final : Kernel {
    std::shared_ptr<DeviceState> state;
    KernelSource description;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VulkanKernel(std::shared_ptr<DeviceState> s, KernelSource src)
        : state(std::move(s)), description(std::move(src)) {}
    VulkanKernel(VulkanKernel const &) = delete;
    VulkanKernel &operator=(VulkanKernel const &) = delete;
    ~VulkanKernel() override {
        if (pipeline)
            vkDestroyPipeline(state->device, pipeline, nullptr);
        if (layout)
            vkDestroyPipelineLayout(state->device, layout, nullptr);
        if (setLayout)
            vkDestroyDescriptorSetLayout(state->device, setLayout, nullptr);
        if (shader)
            vkDestroyShaderModule(state->device, shader, nullptr);
    }
    KernelSource const &source() const noexcept override { return description; }
    void initialize() {
        auto module =
            structure<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
        module.codeSize = description.spirv.size() * sizeof(Word);
        module.pCode = description.spirv.data();
        check(vkCreateShaderModule(state->device, &module, nullptr, &shader),
              "vkCreateShaderModule");
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (std::uint32_t i = 0; i < description.bindings.size(); ++i)
            bindings.push_back(
                {i, bindingType(description, i), 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr});
        auto set = structure<VkDescriptorSetLayoutCreateInfo>(
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
        set.bindingCount = static_cast<std::uint32_t>(bindings.size());
        set.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(state->device, &set, nullptr, &setLayout),
              "vkCreateDescriptorSetLayout");
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                  static_cast<std::uint32_t>(description.parameterWords * 4)};
        auto pl =
            structure<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &setLayout;
        pl.pushConstantRangeCount = range.size ? 1u : 0u;
        pl.pPushConstantRanges = &range;
        check(vkCreatePipelineLayout(state->device, &pl, nullptr, &layout),
              "vkCreatePipelineLayout");
        auto create =
            structure<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
        create.stage = structure<VkPipelineShaderStageCreateInfo>(
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
        create.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        create.stage.module = shader;
        create.stage.pName = "main";
        create.layout = layout;
        check(
            vkCreateComputePipelines(state->device, VK_NULL_HANDLE, 1, &create, nullptr, &pipeline),
            "vkCreateComputePipelines");
    }
};
struct SubmissionSlot {
    std::shared_ptr<DeviceState> state;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer command = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queries = VK_NULL_HANDLE;
    std::size_t descriptorCapacity = 0;
    std::size_t setCapacity = 0;
    std::vector<std::unique_ptr<VulkanBuffer>> uploads;
    std::vector<std::unique_ptr<VulkanBuffer>> downloads;
    bool submitted = false;
    explicit SubmissionSlot(std::shared_ptr<DeviceState> s) : state(std::move(s)) {}
    SubmissionSlot(SubmissionSlot const &) = delete;
    SubmissionSlot &operator=(SubmissionSlot const &) = delete;
    ~SubmissionSlot() {
        if (submitted) {
            auto result = vkWaitForFences(state->device, 1, &fence, VK_TRUE, UINT64_MAX);
            if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST) {
                result = vkDeviceWaitIdle(state->device);
                if (result != VK_SUCCESS && result != VK_ERROR_DEVICE_LOST)
                    std::terminate();
            }
        }
        if (fence)
            vkDestroyFence(state->device, fence, nullptr);
        if (queries)
            vkDestroyQueryPool(state->device, queries, nullptr);
        if (descriptorPool)
            vkDestroyDescriptorPool(state->device, descriptorPool, nullptr);
        if (commandPool)
            vkDestroyCommandPool(state->device, commandPool, nullptr);
    }
    void initialize() {
        auto pool = structure<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pool.queueFamilyIndex = state->family;
        check(vkCreateCommandPool(state->device, &pool, nullptr, &commandPool),
              "create command pool");
        auto alloc =
            structure<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        alloc.commandPool = commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(state->device, &alloc, &command), "allocate command buffer");
        auto f = structure<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        check(vkCreateFence(state->device, &f, nullptr, &fence), "create fence");
        if (state->options.enableTimestamps) {
            auto query = structure<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
            query.queryType = VK_QUERY_TYPE_TIMESTAMP;
            query.queryCount = 2;
            check(vkCreateQueryPool(state->device, &query, nullptr, &queries), "create query pool");
        }
        if (state->options.statistics)
            state->options.statistics->submissionSlotsCreated.fetch_add(1,
                                                                        std::memory_order_relaxed);
    }
    void prepare(std::size_t sets, std::size_t descriptors) {
        check(vkResetCommandPool(state->device, commandPool, 0), "reset command pool");
        check(vkResetFences(state->device, 1, &fence), "reset fence");
        if (sets > setCapacity || descriptors > descriptorCapacity) {
            if (descriptorPool)
                vkDestroyDescriptorPool(state->device, descriptorPool, nullptr);
            descriptorPool = VK_NULL_HANDLE;
            setCapacity = std::max(setCapacity, sets);
            descriptorCapacity = std::max(descriptorCapacity, descriptors);
            VkDescriptorPoolSize sizes[] = {
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, static_cast<std::uint32_t>(descriptorCapacity)},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, static_cast<std::uint32_t>(descriptorCapacity)}};
            auto create = structure<VkDescriptorPoolCreateInfo>(
                VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            create.maxSets = static_cast<std::uint32_t>(setCapacity);
            create.poolSizeCount = 2;
            create.pPoolSizes = sizes;
            check(vkCreateDescriptorPool(state->device, &create, nullptr, &descriptorPool),
                  "create descriptor pool");
            if (state->options.statistics)
                state->options.statistics->descriptorPoolsCreated.fetch_add(
                    1, std::memory_order_relaxed);
        } else if (descriptorPool) {
            check(vkResetDescriptorPool(state->device, descriptorPool, 0), "reset descriptor pool");
        }
    }
    VulkanBuffer &staging(BufferKind kind, std::size_t index, std::size_t words) {
        auto &buffers = kind == BufferKind::Upload ? uploads : downloads;
        if (buffers.size() <= index)
            buffers.resize(index + 1);
        if (!buffers[index] || buffers[index]->words < words) {
            auto buffer = std::make_unique<VulkanBuffer>(state);
            buffer->initialize(words, kind);
            buffers[index] = std::move(buffer);
        }
        return *buffers[index];
    }
    std::size_t stagingBytes() const {
        std::size_t bytes = 0;
        for (auto const *buffers : {&uploads, &downloads})
            for (auto const &buffer : *buffers)
                if (buffer)
                    bytes += std::max<std::size_t>(1, buffer->words) * sizeof(Word);
        return bytes;
    }
};
struct SlotPool {
    std::shared_ptr<DeviceState> state;
    std::vector<std::unique_ptr<SubmissionSlot>> idle;
    std::size_t cachedBytes = 0;
    std::vector<std::shared_ptr<std::vector<Word>>> readbacks;
    std::size_t readbackBytes = 0;
    explicit SlotPool(std::shared_ptr<DeviceState> s) : state(std::move(s)) {
        idle.reserve(state->options.maxInFlight);
    }
    std::shared_ptr<std::vector<Word>> acquireReadback(std::size_t count) {
        for (auto const &buffer : readbacks)
            if (buffer.use_count() == 1 && buffer->size() == count)
                return buffer;
        auto buffer = std::make_shared<std::vector<Word>>(count);
        if (state->options.statistics)
            state->options.statistics->readbackVectorsCreated.fetch_add(1,
                                                                        std::memory_order_relaxed);
        auto bytes = count * sizeof(Word);
        auto limit =
            state->options.reuseSubmissionResources ? state->options.maxCachedReadbackBytes : 0;
        if (bytes && bytes <= limit) {
            // Evict only idle entries; externally held snapshots keep their storage.
            for (auto it = readbacks.begin();
                 it != readbacks.end() && readbackBytes > limit - bytes;) {
                if (it->use_count() == 1) {
                    readbackBytes -= (*it)->size() * sizeof(Word);
                    it = readbacks.erase(it);
                } else
                    ++it;
            }
            if (readbackBytes <= limit - bytes) {
                readbacks.push_back(buffer);
                readbackBytes += bytes;
            }
        }
        return buffer;
    }
    std::unique_ptr<SubmissionSlot> acquire() {
        if (!idle.empty()) {
            auto slot = std::move(idle.back());
            idle.pop_back();
            cachedBytes -= slot->stagingBytes();
            return slot;
        }
        auto slot = std::make_unique<SubmissionSlot>(state);
        slot->initialize();
        return slot;
    }
    void release(std::unique_ptr<SubmissionSlot> slot) {
        if (!state->options.reuseSubmissionResources)
            return;
        auto bytes = slot->stagingBytes();
        auto limit = state->options.maxCachedStagingBytes;
        if (bytes > limit || cachedBytes > limit - bytes) {
            slot->uploads.clear();
            slot->downloads.clear();
            bytes = 0;
        }
        cachedBytes += bytes;
        idle.push_back(std::move(slot));
    }
};
struct ReadbackResult {
    ReadbackToken token;
    std::shared_ptr<std::vector<Word>> words;
};
struct Submission final : VulkanCompletion {
    std::shared_ptr<SlotPool> pool;
    std::unique_ptr<SubmissionSlot> slot;
    std::vector<Command> retained;
    std::vector<BufferHandle> initialized;
    std::vector<ReadbackResult> results;
    bool completed = false;
    bool timed;
    std::optional<Error> failure;
    double elapsed = 0;
    explicit Submission(std::shared_ptr<SlotPool> p)
        : pool(std::move(p)), timed(pool->state->options.enableTimestamps) {}
    ~Submission() override { slot.reset(); }
    Result<void> status() const { return failure ? Result<void>{*failure} : Result<void>{}; }
    Result<void> finish(VkResult result) {
        if (result != VK_SUCCESS) {
            failure =
                Error{ErrorCode::Device, "Vulkan completion failed: " + std::to_string(result)};
            slot.reset(); // The slot destructor ensures completion before freeing
                          // resources.
        } else {
            slot->submitted = false;
            auto const &state = *slot->state;
            try {
                if (timed) {
                    std::uint64_t ticks[2]{};
                    check(vkGetQueryPoolResults(state.device, slot->queries, 0, 2, sizeof(ticks),
                                                ticks, sizeof(ticks[0]),
                                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT),
                          "read timestamp queries");
                    auto delta = ticks[1] - ticks[0];
                    if (state.timestampBits < 64)
                        delta &= (std::uint64_t{1} << state.timestampBits) - 1;
                    elapsed = static_cast<double>(delta) *
                              static_cast<double>(state.properties.limits.timestampPeriod);
                }
                for (std::size_t i = 0; i < results.size(); ++i) {
                    auto &staging = *slot->downloads[i];
                    staging.invalidate();
                    auto &words = *results[i].words;
                    if (!words.empty())
                        std::memcpy(words.data(), staging.mapped, words.size() * sizeof(Word));
                }
                pool->release(std::move(slot));
            } catch (Failure const &e) {
                failure = deviceError(e);
                slot.reset();
            }
        }
        completed = true;
        retained.clear();
        initialized.clear();
        pool.reset();
        return status();
    }
    Result<void> wait() override {
        if (completed)
            return status();
        return finish(vkWaitForFences(slot->state->device, 1, &slot->fence, VK_TRUE, UINT64_MAX));
    }
    Result<bool> ready() override {
        if (completed)
            return failure ? Result<bool>{*failure} : Result<bool>{true};
        auto result = vkGetFenceStatus(slot->state->device, slot->fence);
        if (result == VK_NOT_READY)
            return false;
        auto done = finish(result);
        return done ? Result<bool>{true} : Result<bool>{done.error()};
    }
    Result<double> elapsedNanoseconds() override {
        if (!timed)
            return Error{ErrorCode::Unsupported, "timestamps were not enabled"};
        auto done = wait();
        return done ? Result<double>{elapsed} : Result<double>{done.error()};
    }
    Result<WordSnapshot> readback(ReadbackToken const &token) override {
        auto found = std::find_if(results.begin(), results.end(),
                                  [&](auto const &r) { return r.token == token; });
        if (found == results.end())
            return Error{ErrorCode::InvalidArgument,
                         "readback token does not belong to this submission"};
        auto done = wait();
        return done ? Result<WordSnapshot>{WordSnapshot{found->words}}
                    : Result<WordSnapshot>{done.error()};
    }
};
void barrier(VkCommandBuffer command, VkPipelineStageFlags from, VkPipelineStageFlags to,
             VkAccessFlags reads, VkAccessFlags writes) {
    auto memory = structure<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    memory.srcAccessMask = reads;
    memory.dstAccessMask = writes;
    vkCmdPipelineBarrier(command, from, to, 0, 1, &memory, 0, nullptr, 0, nullptr);
}
// A conservative dependency between ordered operations, including across
// submissions.
void orderedBarrier(VkCommandBuffer command) {
    constexpr auto stages = VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    constexpr auto accesses = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                              VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier(command, stages | VK_PIPELINE_STAGE_HOST_BIT, stages,
            accesses | VK_ACCESS_HOST_WRITE_BIT, accesses);
}
struct CommandRecorder {
    Submission &submission;
    std::size_t uploadIndex = 0;
    std::size_t readbackIndex = 0;
    void initialize(BufferHandle const &handle) {
        auto *buffer = dynamic_cast<VulkanBuffer *>(handle.get());
        auto *image = dynamic_cast<VulkanImage *>(handle.get());
        auto initialized = buffer ? buffer->initialized : image->initialized;
        if (initialized || std::find(submission.initialized.begin(), submission.initialized.end(),
                                     handle) != submission.initialized.end())
            return;
        submission.initialized.push_back(handle);
        auto command = submission.slot->command;
        if (buffer) {
            vkCmdFillBuffer(command, buffer->buffer, 0,
                            std::max<std::size_t>(1, buffer->words) * sizeof(Word), 0);
        } else {
            auto transition =
                structure<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
            transition.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            transition.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            transition.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transition.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            transition.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            transition.image = image->image;
            transition.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &transition);
            VkClearColorValue clear{};
            vkCmdClearColorImage(command, image->image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1,
                                 &transition.subresourceRange);
        }
    }
    VkBufferImageCopy imageRegion(VulkanImage const &image) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {image.desc.extent.x, image.desc.extent.y, image.desc.extent.z};
        return region;
    }
    void initialize(Upload const &c) { initialize(c.buffer); }
    void initialize(Readback const &c) { initialize(c.buffer); }
    void initialize(Dispatch const &c) {
        for (auto const &buffer : c.buffers)
            initialize(buffer);
    }
    void operator()(Upload const &c) {
        auto &slot = *submission.slot;
        auto &staging = slot.staging(BufferKind::Upload, uploadIndex++, c.words->size());
        if (!c.words->empty()) {
            std::memcpy(staging.mapped, c.words->data(), c.words->size() * sizeof(Word));
            staging.flush();
            VkBufferCopy region{0, 0, c.words->size() * sizeof(Word)};
            if (auto const *image = dynamic_cast<VulkanImage *>(c.buffer.get())) {
                auto copy = imageRegion(*image);
                vkCmdCopyBufferToImage(slot.command, staging.buffer, image->image,
                                       VK_IMAGE_LAYOUT_GENERAL, 1, &copy);
            } else {
                auto const &destination = *static_cast<VulkanBuffer *>(c.buffer.get());
                vkCmdCopyBuffer(slot.command, staging.buffer, destination.buffer, 1, &region);
            }
        }
    }
    void operator()(Readback const &c) {
        auto &slot = *submission.slot;
        auto count = c.buffer->wordCount();
        auto &staging = slot.staging(BufferKind::Readback, readbackIndex++, count);
        if (count) {
            VkBufferCopy region{0, 0, count * sizeof(Word)};
            if (auto const *image = dynamic_cast<VulkanImage *>(c.buffer.get())) {
                // Image transfers leave the final partial word untouched; keep padding
                // deterministic.
                vkCmdFillBuffer(slot.command, staging.buffer, 0, VK_WHOLE_SIZE, 0);
                orderedBarrier(slot.command);
                auto copy = imageRegion(*image);
                vkCmdCopyImageToBuffer(slot.command, image->image, VK_IMAGE_LAYOUT_GENERAL,
                                       staging.buffer, 1, &copy);
            } else {
                auto const &source = *static_cast<VulkanBuffer *>(c.buffer.get());
                vkCmdCopyBuffer(slot.command, source.buffer, staging.buffer, 1, &region);
            }
        }
    }
    void operator()(Dispatch const &d) {
        if (!d.extent.x || !d.extent.y || !d.extent.z)
            return;
        auto &slot = *submission.slot;
        auto &k = *static_cast<VulkanKernel *>(d.kernel.get());
        auto alloc =
            structure<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        alloc.descriptorPool = slot.descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &k.setLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        check(vkAllocateDescriptorSets(slot.state->device, &alloc, &set),
              "allocate descriptor set");
        std::vector<VkDescriptorBufferInfo> infos(d.buffers.size());
        std::vector<VkDescriptorImageInfo> images(d.buffers.size());
        std::vector<VkWriteDescriptorSet> updates;
        for (std::uint32_t i = 0; i < d.buffers.size(); ++i) {
            auto update = structure<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
            update.dstSet = set;
            update.dstBinding = i;
            update.descriptorCount = 1;
            update.descriptorType = bindingType(k.description, i);
            if (auto const *image = dynamic_cast<VulkanImage *>(d.buffers[i].get())) {
                images[i] = {VK_NULL_HANDLE, image->view, VK_IMAGE_LAYOUT_GENERAL};
                update.pImageInfo = &images[i];
            } else {
                auto const &buffer = *static_cast<VulkanBuffer *>(d.buffers[i].get());
                infos[i] = {buffer.buffer, 0,
                            std::max<std::size_t>(1, buffer.words) * sizeof(Word)};
                update.pBufferInfo = &infos[i];
            }
            updates.push_back(update);
        }
        vkUpdateDescriptorSets(slot.state->device, static_cast<std::uint32_t>(updates.size()),
                               updates.data(), 0, nullptr);
        vkCmdBindPipeline(slot.command, VK_PIPELINE_BIND_POINT_COMPUTE, k.pipeline);
        vkCmdBindDescriptorSets(slot.command, VK_PIPELINE_BIND_POINT_COMPUTE, k.layout, 0, 1, &set,
                                0, nullptr);
        if (!d.parameters.empty())
            vkCmdPushConstants(slot.command, k.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               static_cast<std::uint32_t>(d.parameters.size() * sizeof(Word)),
                               d.parameters.data());
        auto local = k.description.localSize;
        vkCmdDispatch(slot.command, (d.extent.x / local.x + (d.extent.x % local.x != 0)),
                      (d.extent.y / local.y + (d.extent.y % local.y != 0)),
                      (d.extent.z / local.z + (d.extent.z % local.z != 0)));
    }
};
void record(Submission &submission) {
    auto &slot = *submission.slot;
    std::size_t descriptors = 0, sets = 0;
    for (auto const &command : submission.retained) {
        if (auto const *d = std::get_if<Dispatch>(&command)) {
            if (d->buffers.size() > UINT32_MAX - descriptors || sets == UINT32_MAX)
                throw Failure{"submission too large"};
            descriptors += d->buffers.size();
            ++sets;
        } else if (auto const *r = std::get_if<Readback>(&command)) {
            // Allocate before queue submission, so completion/recycling need no
            // result allocations.
            submission.results.push_back(
                {r->token, submission.pool->acquireReadback(r->buffer->wordCount())});
        }
    }
    slot.prepare(sets, descriptors);
    auto begin = structure<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(slot.command, &begin), "begin command buffer");
    if (slot.queries) {
        vkCmdResetQueryPool(slot.command, slot.queries, 0, 2);
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, slot.queries, 0);
    }
    orderedBarrier(slot.command);
    CommandRecorder recorder{submission};
    for (auto const &command : submission.retained)
        std::visit([&](auto const &c) { recorder.initialize(c); }, command);
    if (!submission.initialized.empty())
        orderedBarrier(slot.command);
    for (auto const &command : submission.retained) {
        std::visit(recorder, command);
        orderedBarrier(slot.command);
    }
    barrier(slot.command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_HOST_READ_BIT);
    if (slot.queries)
        vkCmdWriteTimestamp(slot.command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, slot.queries, 1);
    check(vkEndCommandBuffer(slot.command), "end command buffer");
    auto submit = structure<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &slot.command;
    check(vkQueueSubmit(slot.state->queue, 1, &submit, slot.fence), "submit queue");
    slot.submitted = true;
    for (auto const &resource : submission.initialized) {
        if (auto *image = dynamic_cast<VulkanImage *>(resource.get()))
            image->initialized = true;
        else
            static_cast<VulkanBuffer *>(resource.get())->initialized = true;
    }
}
struct VulkanDevice final : Device {
    std::shared_ptr<DeviceState> state;
    std::shared_ptr<SlotPool> pool;
    std::vector<std::shared_ptr<Submission>> pending;
    explicit VulkanDevice(std::shared_ptr<DeviceState> s)
        : state(std::move(s)), pool(std::make_shared<SlotPool>(state)) {}
    ~VulkanDevice() override { (void)drain(); }
    Result<void> drain() {
        std::optional<Error> failure;
        for (auto const &s : pending) {
            auto result = s->wait();
            if (!result && !failure)
                failure = result.error();
        }
        pending.clear();
        return failure ? Result<void>{*failure} : Result<void>{};
    }
    DeviceInfo info() const override {
        return {state->properties.deviceName,
                state->properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU,
                state->properties.limits.maxStorageBufferRange, state->capabilities};
    }
    bool get(BufferHandle const &buffer) const {
        if (auto const *image = dynamic_cast<VulkanImage *>(buffer.get()))
            return image->state == state;
        auto const *result = dynamic_cast<VulkanBuffer *>(buffer.get());
        return result && result->state == state;
    }
    Result<BufferHandle> createImage(ImageDesc const &desc) override {
        auto count = imageWordCount(desc);
        if (!count)
            return count.error();
        try {
            auto image = std::make_shared<VulkanImage>(state, desc, count.value());
            image->initialize();
            return BufferHandle{std::move(image)};
        } catch (Failure const &e) {
            return deviceError(e);
        }
    }
    Result<BufferHandle> createBuffer(std::size_t words) override {
        if (words > state->properties.limits.maxStorageBufferRange / sizeof(Word) ||
            words > UINT32_MAX)
            return Error{ErrorCode::Unsupported, "buffer exceeds device or kernel indexing limit"};
        try {
            auto result = std::make_shared<VulkanBuffer>(state);
            result->initialize(words);
            return BufferHandle{std::move(result)};
        } catch (Failure const &e) {
            return deviceError(e);
        }
    }
    Result<BufferHandle> createBufferBytes(std::size_t bytes) override {
        if (bytes > state->properties.limits.maxStorageBufferRange)
            return Error{ErrorCode::Unsupported, "buffer exceeds device limit"};
        auto result = createBuffer((bytes + 3) / 4);
        if (result)
            static_cast<VulkanBuffer *>(result.value().get())->logicalBytes = bytes;
        return result;
    }
    Result<KernelHandle> createKernel(KernelSource source) override {
        auto status = validate(source);
        if (!status)
            return status.error();
        auto const &limits = state->properties.limits;
        if (!supportedModule(source.spirv, source.localSize))
            return Error{ErrorCode::InvalidArgument,
                         "module must use SPIR-V <= 1.3 and LocalSize matching "
                         "kernel metadata"};
        auto local = source.localSize;
        std::size_t images = 0;
        for (auto const &r : source.resources)
            images += r.image.has_value();
        auto buffers = source.bindings.size() - images;
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(state->physical, &features);
        for (std::size_t i = 5; i < source.spirv.size(); i += source.spirv[i] >> 16) {
            if ((source.spirv[i] & 0xffffu) == 224u && !source.requiresFullWorkgroups)
                return Error{ErrorCode::InvalidArgument,
                             "barrier requires complete workgroup metadata"};
            if ((source.spirv[i] & 0xffffu) == 59u && (source.spirv[i] >> 16) >= 4u &&
                source.spirv[i + 3] == 4u &&
                (!source.sharedMemoryBytes || !source.requiresFullWorkgroups))
                return Error{ErrorCode::InvalidArgument,
                             "shared storage requires workgroup metadata"};
            if ((source.spirv[i] & 0xffffu) == 17u && (source.spirv[i] >> 16) == 2u) {
                auto capability = source.spirv[i + 1];
                if ((capability == 9u && !state->capabilities.float16) ||
                    (capability == 4433u && !state->capabilities.storageBuffer16) ||
                    (capability == 4434u && !state->uniformBuffer16) ||
                    (capability == 4435u && !state->capabilities.pushConstant16))
                    return Error{ErrorCode::Unsupported,
                                 "required FP16 capability was not enabled"};
                if (source.spirv[i + 1] == 10u && !features.shaderFloat64)
                    return Error{ErrorCode::Unsupported, "device does not support Float64"};
                if (source.spirv[i + 1] == 49u && !features.shaderStorageImageExtendedFormats)
                    return Error{ErrorCode::Unsupported,
                                 "device does not support extended storage image formats"};
            }
        }
        if (local.x > limits.maxComputeWorkGroupSize[0] ||
            local.y > limits.maxComputeWorkGroupSize[1] ||
            local.z > limits.maxComputeWorkGroupSize[2] ||
            std::uint64_t{local.x} * local.y * local.z > limits.maxComputeWorkGroupInvocations ||
            buffers > limits.maxPerStageDescriptorStorageBuffers ||
            buffers > limits.maxDescriptorSetStorageBuffers ||
            images > limits.maxPerStageDescriptorStorageImages ||
            images > limits.maxDescriptorSetStorageImages ||
            source.bindings.size() > limits.maxPerStageResources ||
            source.parameterWords * 4 > limits.maxPushConstantsSize ||
            source.sharedMemoryBytes > limits.maxComputeSharedMemorySize)
            return Error{ErrorCode::Unsupported, "kernel exceeds Vulkan device limits"};
        try {
            auto result = std::make_shared<VulkanKernel>(state, std::move(source));
            result->initialize();
            return KernelHandle{std::move(result)};
        } catch (Failure const &e) {
            return deviceError(e);
        }
    }
    Result<void> upload(BufferHandle const &buffer, std::vector<Word> const &words) override {
        CommandList commands;
        auto recorded = commands.upload(buffer, words);
        if (!recorded)
            return recorded;
        auto done = submit(commands);
        if (!done)
            return done.error();
        return drain();
    }
    Result<std::vector<Word>> download(BufferHandle const &buffer) override {
        CommandList commands;
        auto token = commands.readback(buffer);
        if (!token)
            return token.error();
        auto done = submit(commands);
        if (!done)
            return done.error();
        auto data = done.value()->readback(token.value());
        if (!data)
            return data.error();
        auto completed = drain();
        if (!completed)
            return completed.error();
        return *data.value();
    }
    Result<void> validateCommand(Upload const &c) const {
        return get(c.buffer)
                   ? Result<void>{}
                   : Result<void>{Error{ErrorCode::InvalidArgument, "foreign upload buffer"}};
    }
    Result<void> validateCommand(Readback const &c) const {
        return get(c.buffer)
                   ? Result<void>{}
                   : Result<void>{Error{ErrorCode::InvalidArgument, "foreign readback buffer"}};
    }
    Result<void> validateCommand(Dispatch const &d) const {
        auto *k = dynamic_cast<VulkanKernel *>(d.kernel.get());
        if (!k || k->state != state)
            return Error{ErrorCode::InvalidArgument, "foreign kernel"};
        for (auto const &b : d.buffers)
            if (!get(b))
                return Error{ErrorCode::InvalidArgument, "foreign buffer"};
        auto local = k->description.localSize;
        auto const *limits = state->properties.limits.maxComputeWorkGroupCount;
        if ((d.extent.x / local.x + (d.extent.x % local.x != 0)) > limits[0] ||
            (d.extent.y / local.y + (d.extent.y % local.y != 0)) > limits[1] ||
            (d.extent.z / local.z + (d.extent.z % local.z != 0)) > limits[2])
            return Error{ErrorCode::Unsupported, "dispatch exceeds device workgroup count"};
        return {};
    }
    Result<void> reap() {
        for (auto it = pending.begin(); it != pending.end();) {
            auto ready = (*it)->ready();
            if (!ready) {
                pending.erase(it);
                return ready.error();
            }
            if (ready.value())
                it = pending.erase(it);
            else
                ++it;
        }
        if (pending.size() >= state->options.maxInFlight) {
            auto done = pending.front()->wait();
            pending.erase(pending.begin());
            if (!done)
                return done;
        }
        return {};
    }
    Result<std::shared_ptr<Completion>> submit(CommandList const &commands) override {
        for (auto const &command : commands.commands()) {
            auto valid = std::visit([&](auto const &c) { return validateCommand(c); }, command);
            if (!valid)
                return valid.error();
        }
        auto available = reap();
        if (!available)
            return available.error();
        try {
            auto submission = std::make_shared<Submission>(pool);
            submission->retained = commands.commands();
            submission->slot = pool->acquire();
            // Reserve before submit so allocation failure cannot orphan in-flight
            // work.
            pending.reserve(pending.size() + 1);
            record(*submission);
            pending.push_back(submission);
            return std::shared_ptr<Completion>{std::move(submission)};
        } catch (Failure const &e) {
            return deviceError(e);
        }
    }
};
} // namespace
Result<std::unique_ptr<Device>> createVulkanDevice() { return createVulkanDevice(VulkanOptions{}); }
Result<std::unique_ptr<Device>> createVulkanDevice(VulkanOptions const &options) {
    if (!options.maxInFlight)
        return Error{ErrorCode::InvalidArgument, "maxInFlight must be positive"};
    try {
        auto state = std::make_shared<DeviceState>();
        state->initialize(options);
        return std::unique_ptr<Device>{std::make_unique<VulkanDevice>(std::move(state))};
    } catch (Failure const &e) {
        return deviceError(e);
    }
}
} // namespace lutils::compute
