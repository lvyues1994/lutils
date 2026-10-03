#pragma once

#include <atomic>
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
struct ValidationReport {
    std::atomic<std::uint64_t> errors{0};
    std::atomic<std::uint64_t> warnings{0};
};
struct VulkanStatistics {
    std::atomic<std::uint64_t> submissionSlotsCreated{0};
    std::atomic<std::uint64_t> descriptorPoolsCreated{0};
    std::atomic<std::uint64_t> stagingBuffersCreated{0};
};
struct VulkanOptions {
    bool requireHardware = false;
    bool enableValidation = false;
    bool enableTimestamps = false;
    std::shared_ptr<ValidationReport> validationReport;
    // A transfer policy, not a universal speedup; measure the complete workload.
    bool preferHostCached = false;
    bool deviceLocal = false;
    bool reuseSubmissionResources = true;
    std::uint32_t maxInFlight = 4;
    // Total payload capacity retained in idle staging buffers. In-flight staging
    // and caller-retained readback snapshots are outside this cache budget.
    std::size_t maxCachedStagingBytes = 64u * 1024u * 1024u;
    std::shared_ptr<VulkanStatistics> statistics = nullptr;
};
// Optional Vulkan diagnostics; the common Completion interface stays unchanged.
struct VulkanCompletion : Completion {
    // Waits for completion. Covers the recorded device commands and barriers,
    // excluding host recording/submission and image packing or unpacking.
    virtual Result<double> elapsedNanoseconds() = 0;
};
Result<std::unique_ptr<Device>> createVulkanDevice(VulkanOptions const &options);
} // namespace lutils::compute
