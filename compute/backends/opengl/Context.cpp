#include "Context.hpp"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace lutils::compute::gl_detail {
namespace {
Error eglError(char const *operation) {
    std::ostringstream message;
    message << operation << " failed (EGL 0x" << std::hex << eglGetError() << ')';
    return {ErrorCode::Device, message.str()};
}
std::set<std::string> words(char const *value) {
    std::istringstream input{value ? value : ""};
    std::set<std::string> result;
    std::string word;
    while (input >> word)
        result.insert(word);
    return result;
}
} // namespace
struct Display {
    EGLDisplay handle;
    explicit Display(EGLDisplay value) : handle(value) {}
    // EGL initialization is process-wide and is not reference counted. Other
    // libraries can use this same display; only our contexts/surfaces are owned.
    // In particular, eglTerminate here would invalidate the Android UI renderer.
};
namespace {
Result<std::shared_ptr<Display>> acquireDisplay(OpenGlOptions const &options) {
    // EGLDisplay is process-wide: overlapping devices must share its lifetime.
    static std::mutex mutex;
    static std::map<EGLDisplay, std::shared_ptr<Display>> displays;
    std::lock_guard<std::mutex> lock(mutex);
    EGLDisplay handle = EGL_NO_DISPLAY;
#ifdef __ANDROID__
    if (options.deviceIndex)
        return Error{ErrorCode::Unsupported, "EGL device enumeration is Linux-only"};
    handle = eglGetDisplay(EGL_DEFAULT_DISPLAY);
#else
    auto const client = words(eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    auto platform = procedure<PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT");
    if (!platform)
        return Error{ErrorCode::Unsupported, "headless EGL platform display is unavailable"};
    if (options.deviceIndex) {
        auto query = procedure<PFNEGLQUERYDEVICESEXTPROC>("eglQueryDevicesEXT");
        if (!query || !client.count("EGL_EXT_platform_device"))
            return Error{ErrorCode::Unsupported, "EGL device selection is unavailable"};
        EGLint count = 0;
        if (!query(0, nullptr, &count))
            return eglError("eglQueryDevicesEXT");
        if (*options.deviceIndex >= static_cast<unsigned>(std::max(0, count)))
            return Error{ErrorCode::InvalidArgument, "EGL device index is out of range"};
        std::vector<EGLDeviceEXT> devices(static_cast<std::size_t>(count));
        if (!query(count, devices.data(), &count))
            return eglError("eglQueryDevicesEXT");
        if (*options.deviceIndex >= static_cast<unsigned>(std::max(0, count)))
            return Error{ErrorCode::Unsupported, "EGL device disappeared during enumeration"};
        handle = platform(EGL_PLATFORM_DEVICE_EXT, devices[*options.deviceIndex], nullptr);
    } else if (client.count("EGL_MESA_platform_surfaceless")) {
        handle = platform(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
    } else {
        return Error{ErrorCode::Unsupported,
                     "surfaceless EGL is unavailable; select an EGL device explicitly"};
    }
#endif
    if (handle == EGL_NO_DISPLAY)
        return eglError("get headless EGL display");
    auto found = displays.find(handle);
    if (!eglInitialize(handle, nullptr, nullptr))
        return eglError("eglInitialize");
    if (found != displays.end())
        return found->second;
    auto result = std::make_shared<Display>(handle);
    displays[handle] = result;
    return result;
}
} // namespace
void Gl::load(bool desktop) {
#define LUTILS_GL_LOAD(name)                                                                       \
    name = procedure<decltype(name)>("gl" #name);                                                  \
    if (!name)                                                                                     \
        throw std::runtime_error("missing GL function gl" #name);
    LUTILS_GL_FUNCTIONS(LUTILS_GL_LOAD)
#undef LUTILS_GL_LOAD
    if (desktop) {
        TexStorage1D = procedure<decltype(TexStorage1D)>("glTexStorage1D");
        TexSubImage1D = procedure<decltype(TexSubImage1D)>("glTexSubImage1D");
        GetTexImage = procedure<decltype(GetTexImage)>("glGetTexImage");
        if (!TexStorage1D || !TexSubImage1D || !GetTexImage)
            throw std::runtime_error("missing desktop GL texture functions");
    }
    BufferStorageExternal =
        procedure<decltype(BufferStorageExternal)>("glBufferStorageExternalEXT");
    ImageTargetTexture = procedure<decltype(ImageTargetTexture)>("glEGLImageTargetTexture2DOES");
    ImageTargetStorage = procedure<decltype(ImageTargetStorage)>("glEGLImageTargetTexStorageEXT");
}
Result<std::shared_ptr<Context>> Context::create(OpenGlOptions const &options) {
    if (options.maxInFlight == 0 || options.maxInFlight > 1024)
        return Error{ErrorCode::InvalidArgument, "maxInFlight must be in [1,1024]"};
    if (options.target != GlslTarget::OpenGl43 && options.target != GlslTarget::OpenGles31)
        return Error{ErrorCode::InvalidArgument, "unknown GL target"};
#ifdef __ANDROID__
    if (options.target != GlslTarget::OpenGles31)
        return Error{ErrorCode::Unsupported, "Android requires OpenGL ES"};
#endif
    auto result = std::make_shared<Context>();
    result->options = options;
    result->worker = std::thread([ptr = result.get()] { ptr->loop(); });
    auto initialized = result->invoke([&] { return result->initialize(); });
    if (!initialized)
        return initialized.error();
    return result;
}
void Context::loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return stopping || !tasks.empty(); });
            if (tasks.empty() && stopping)
                return;
            task = std::move(tasks.front());
            tasks.pop_front();
        }
        task();
    }
}
Context::~Context() {
    if (!worker.joinable())
        return;
    invoke([&] {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT) {
                if (gl.Finish)
                    gl.Finish();
                eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                eglDestroyContext(display, context);
            }
            if (surface != EGL_NO_SURFACE)
                eglDestroySurface(display, surface);
            displayLease.reset();
        }
        eglReleaseThread();
    });
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    changed.notify_one();
    worker.join();
}
Result<void> Context::initialize() {
    auto lease = acquireDisplay(options);
    if (!lease)
        return lease.error();
    displayLease = std::move(lease).value();
    display = displayLease->handle;
    eglExtensions = words(eglQueryString(display, EGL_EXTENSIONS));
    auto eglVersion = eglQueryString(display, EGL_VERSION);
    if ((!eglVersion || std::string{eglVersion}.compare(0, 3, "1.5") < 0) &&
        !eglExtension("EGL_KHR_get_all_proc_addresses"))
        return Error{ErrorCode::Unsupported, "GL function loading requires EGL 1.5 "
                                             "or EGL_KHR_get_all_proc_addresses"};
    bool desktop = options.target == GlslTarget::OpenGl43;
    if (!eglBindAPI(desktop ? EGL_OPENGL_API : EGL_OPENGL_ES_API))
        return eglError("eglBindAPI");
    EGLint attributes[]{
        EGL_SURFACE_TYPE, eglExtension("EGL_KHR_surfaceless_context") ? 0 : EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, desktop ? EGL_OPENGL_BIT : EGL_OPENGL_ES3_BIT_KHR, EGL_NONE};
    EGLConfig config{};
    EGLint count = 0;
    if (!eglChooseConfig(display, attributes, &config, 1, &count) || count != 1)
        return Error{ErrorCode::Unsupported, "no EGL config for the requested compute API"};
    EGLint desktopContext[]{EGL_CONTEXT_MAJOR_VERSION_KHR,
                            4,
                            EGL_CONTEXT_MINOR_VERSION_KHR,
                            3,
                            EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
                            EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
                            EGL_NONE};
    EGLint esContext[]{EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 1,
                       EGL_NONE};
    context =
        eglCreateContext(display, config, EGL_NO_CONTEXT, desktop ? desktopContext : esContext);
    if (context == EGL_NO_CONTEXT)
        return eglError("eglCreateContext");
    if (!eglExtension("EGL_KHR_surfaceless_context")) {
        EGLint size[]{EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, size);
        if (surface == EGL_NO_SURFACE)
            return eglError("eglCreatePbufferSurface");
    }
    if (!eglMakeCurrent(display, surface, surface, context))
        return eglError("eglMakeCurrent");
    try {
        gl.load(desktop);
    } catch (std::runtime_error const &e) {
        return Error{ErrorCode::Unsupported, e.what()};
    }
    GLint major = 0, minor = 0;
    gl.GetIntegerv(GL_MAJOR_VERSION, &major);
    gl.GetIntegerv(GL_MINOR_VERSION, &minor);
    if ((desktop && (major < 4 || (major == 4 && minor < 3))) ||
        (!desktop && (major < 3 || (major == 3 && minor < 1))))
        return Error{ErrorCode::Unsupported, "compute requires OpenGL 4.3 or OpenGL ES 3.1"};
    GLint extensionCount = 0;
    gl.GetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; i < extensionCount; ++i) {
        auto name = gl.GetStringi(GL_EXTENSIONS, static_cast<GLuint>(i));
        if (name)
            extensions.emplace(reinterpret_cast<char const *>(name));
    }
    auto renderer = gl.GetString(GL_RENDERER);
    auto driver = gl.GetString(GL_VERSION);
    if (!renderer || !driver)
        return Error{ErrorCode::Device, "GL renderer/version unavailable"};
    info.name = std::string{reinterpret_cast<char const *>(renderer)} + " / " +
                reinterpret_cast<char const *>(driver);
    auto lower = info.name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto name : {"llvmpipe", "softpipe", "swiftshader", "software rasterizer"})
        info.software = info.software || lower.find(name) != std::string::npos;
    if (options.requireHardware && info.software)
        return Error{ErrorCode::Unsupported, "hardware GL requested but renderer is software"};
    GLint64 bytes = 0;
    gl.GetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &bytes);
    info.maxBufferBytes = static_cast<std::size_t>(std::max<GLint64>(0, bytes));
    gl.GetInteger64v(GL_MAX_UNIFORM_BLOCK_SIZE, &bytes);
    maxUniformBytes = static_cast<std::size_t>(std::max<GLint64>(0, bytes));
    GLint value = 0;
    gl.GetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &value);
    info.capabilities.maxWorkgroupInvocations = static_cast<std::size_t>(std::max(0, value));
    gl.GetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &value);
    info.capabilities.maxSharedMemoryBytes = static_cast<std::size_t>(std::max(0, value));
    info.capabilities.float64 = desktop;
    gl.GetIntegerv(GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS, &maxStorageBindings);
    gl.GetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &value);
    maxStorageBindings = std::min(maxStorageBindings, value);
    gl.GetIntegerv(GL_MAX_IMAGE_UNITS, &maxImages);
    gl.GetIntegerv(GL_MAX_COMPUTE_IMAGE_UNIFORMS, &value);
    maxImages = std::min(maxImages, value);
    gl.GetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &maxSamplers);
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    gl.GetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &maxTexture3d);
    for (GLuint i = 0; i < 3; ++i) {
        gl.GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &maxGroups[i]);
        gl.GetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, i, &maxLocal[i]);
    }
    createImage = procedure<decltype(createImage)>("eglCreateImageKHR");
    destroyImage = procedure<decltype(destroyImage)>("eglDestroyImageKHR");
    createSync = procedure<decltype(createSync)>("eglCreateSyncKHR");
    destroySync = procedure<decltype(destroySync)>("eglDestroySyncKHR");
    waitSync = procedure<decltype(waitSync)>("eglWaitSyncKHR");
    dupFence = procedure<decltype(dupFence)>("eglDupNativeFenceFDANDROID");
#ifdef __ANDROID__
    nativeClientBuffer = procedure<decltype(nativeClientBuffer)>("eglGetNativeClientBufferANDROID");
    info.capabilities.externalAndroidHardwareBuffer =
        nativeClientBuffer && createImage && destroyImage && gl.ImageTargetTexture &&
        extension("GL_OES_EGL_image") && eglExtension("EGL_ANDROID_get_native_client_buffer") &&
        eglExtension("EGL_ANDROID_image_native_buffer");
#endif
    info.capabilities.externalDmaBuf = createImage && destroyImage && gl.ImageTargetTexture &&
                                       extension("GL_OES_EGL_image") &&
                                       eglExtension("EGL_EXT_image_dma_buf_import");
    return error("initialize GL capabilities");
}
Result<void> Context::error(char const *operation) {
    auto code = gl.GetError();
    if (code == GL_NO_ERROR)
        return {};
    while (gl.GetError() != GL_NO_ERROR) {
    }
    std::ostringstream message;
    message << operation << " failed (GL 0x" << std::hex << code << ')';
    return Error{ErrorCode::Device, message.str()};
}
Result<GLuint> Context::program(std::string const &source) {
    auto shader = gl.CreateShader(GL_COMPUTE_SHADER);
    auto text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint okay = 0;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &okay);
    if (!okay) {
        GLint size = 0;
        gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        gl.GetShaderInfoLog(shader, size, nullptr, log.data());
        gl.DeleteShader(shader);
        return Error{ErrorCode::Compilation, "GL compute shader: " + log};
    }
    auto result = gl.CreateProgram();
    gl.AttachShader(result, shader);
    gl.LinkProgram(result);
    gl.DeleteShader(shader);
    gl.GetProgramiv(result, GL_LINK_STATUS, &okay);
    if (!okay) {
        GLint size = 0;
        gl.GetProgramiv(result, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        gl.GetProgramInfoLog(result, size, nullptr, log.data());
        gl.DeleteProgram(result);
        return Error{ErrorCode::Compilation, "GL compute program: " + log};
    }
    auto checked = error("create GL program");
    if (!checked) {
        gl.DeleteProgram(result);
        return checked.error();
    }
    return result;
}
} // namespace lutils::compute::gl_detail
