#pragma once
#include <lutils/image/ImageResource.hpp>
#include <lutils/image/SimdConversion.hpp>

namespace lutils::image {
// Maps each object once and ends CPU access before returning a ready completion.
// No pixel staging copy. Caller serializes access for the duration of submit/run.
struct CpuConversionExecutor {
    virtual ~CpuConversionExecutor() = default;
    virtual Result<std::shared_ptr<compute::Completion>>
    submit(CpuConversionPlan const &plan, ImageResource const &source,
           ImageResource const &destination) = 0;
    Result<void> run(CpuConversionPlan const &plan, ImageResource const &source,
                     ImageResource const &destination);
};
std::unique_ptr<CpuConversionExecutor> createCpuConversionExecutor();
} // namespace lutils::image
