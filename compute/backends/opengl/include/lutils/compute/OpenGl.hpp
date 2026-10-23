#pragma once
#include <lutils/compute/Runtime.hpp>

namespace lutils::compute {
struct OpenGlOptions {
#ifdef __ANDROID__
    GlslTarget target = GlslTarget::OpenGles31;
#else
    GlslTarget target = GlslTarget::OpenGl43;
#endif
    bool requireHardware = false;
    std::size_t maxInFlight = 4;
    // Linux EGL device enumeration index; absent selects a surfaceless display.
    std::optional<std::uint32_t> deviceIndex;
};
// Owns a headless EGL context on a private thread. Caller serialization follows
// Device's contract; handles can be destroyed on any host thread.
Result<std::unique_ptr<Device>> createOpenGlDevice(OpenGlOptions options = {});
} // namespace lutils::compute
