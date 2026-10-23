#pragma once
#include <lutils/compute/Runtime.hpp>
#include <stdexcept>
#include <string>
#ifdef LUTILS_TEST_OPENGL
#include <lutils/compute/OpenGl.hpp>
#endif
namespace test_device {
inline std::string name(int argc, char **argv) {
    std::string result = argc > 1 ? argv[1] : "cpu";
    if (result == "cpu")
        return result;
#ifdef LUTILS_TEST_VULKAN
    if (result == "vulkan")
        return result;
#endif
#ifdef LUTILS_TEST_OPENGL
    if (result == "opengl" || result == "gles")
        return result;
#endif
    throw std::invalid_argument("unknown or unbuilt test backend: " + result);
}
inline bool gl(std::string const &name) { return name == "opengl" || name == "gles"; }
inline lutils::Result<std::unique_ptr<lutils::compute::Device>> createGl(int argc, char **argv,
                                                                         std::size_t depth = 4) {
#ifdef LUTILS_TEST_OPENGL
    lutils::compute::OpenGlOptions options;
    options.target = name(argc, argv) == "gles" ? lutils::compute::GlslTarget::OpenGles31
                                                : lutils::compute::GlslTarget::OpenGl43;
    options.maxInFlight = depth;
    for (int i = 2; i < argc; ++i) {
        if (std::string{argv[i]} == "--require-hardware")
            options.requireHardware = true;
        else
            throw std::invalid_argument("unknown GL test option");
    }
    return lutils::compute::createOpenGlDevice(options);
#else
    (void)argc;
    (void)argv;
    (void)depth;
    return lutils::Error{lutils::ErrorCode::Unsupported, "GL tests were not built"};
#endif
}
} // namespace test_device
