#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>

#include <GLES2/gl2ext.h>
#include <condition_variable>
#include <deque>
#include <future>
#include <lutils/compute/OpenGl.hpp>
#include <mutex>
#include <set>
#include <thread>

namespace lutils::compute::gl_detail {
// All pointers belong to this context; there is no global GL dispatch.
// clang-format off
#define LUTILS_GL_FUNCTIONS(X) \
    X(GetString) \
    X(GetStringi) \
    X(GetIntegerv) \
    X(GetInteger64v) \
    X(GetIntegeri_v) \
    X(GetError) \
    X(GenBuffers) \
    X(DeleteBuffers) \
    X(BindBuffer) \
    X(BufferData) \
    X(BufferSubData) \
    X(CopyBufferSubData) \
    X(MapBufferRange) \
    X(UnmapBuffer) \
    X(BindBufferBase) \
    X(GenTextures) \
    X(DeleteTextures) \
    X(BindTexture) \
    X(TexParameteri) \
    X(PixelStorei) \
    X(TexStorage2D) \
    X(TexStorage3D) \
    X(TexSubImage2D) \
    X(TexSubImage3D) \
    X(ActiveTexture) \
    X(BindImageTexture) \
    X(CreateShader) \
    X(DeleteShader) \
    X(ShaderSource) \
    X(CompileShader) \
    X(GetShaderiv) \
    X(GetShaderInfoLog) \
    X(CreateProgram) \
    X(DeleteProgram) \
    X(AttachShader) \
    X(LinkProgram) \
    X(GetProgramiv) \
    X(GetProgramInfoLog) \
    X(UseProgram) \
    X(GetUniformLocation) \
    X(Uniform1i) \
    X(Uniform2i) \
    X(Uniform4i) \
    X(Uniform4f) \
    X(DispatchCompute) \
    X(MemoryBarrier) \
    X(FenceSync) \
    X(DeleteSync) \
    X(ClientWaitSync) \
    X(Flush) \
    X(Finish) \
    X(GenFramebuffers) \
    X(DeleteFramebuffers) \
    X(BindFramebuffer) \
    X(FramebufferTexture2D) \
    X(FramebufferTextureLayer) \
    X(CheckFramebufferStatus) \
    X(ReadPixels) \
    X(ReadBuffer) \
    X(GetInternalformativ) \
    X(GetActiveUniformBlockiv) \
    X(GetUniformBlockIndex)
// clang-format on
struct Gl {
#define LUTILS_GL_DECLARE(name) decltype(&gl##name) name = nullptr;
    LUTILS_GL_FUNCTIONS(LUTILS_GL_DECLARE)
#undef LUTILS_GL_DECLARE
    void(GL_APIENTRYP TexStorage1D)(GLenum, GLsizei, GLenum, GLsizei) = nullptr;
    void(GL_APIENTRYP TexSubImage1D)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum,
                                     void const *) = nullptr;
    void(GL_APIENTRYP GetTexImage)(GLenum, GLint, GLenum, GLenum, void *) = nullptr;
    PFNGLBUFFERSTORAGEEXTERNALEXTPROC BufferStorageExternal = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC ImageTargetTexture = nullptr;
    PFNGLEGLIMAGETARGETTEXSTORAGEEXTPROC ImageTargetStorage = nullptr;
    void load(bool desktop);
};
struct Display;
struct Context final {
    Context() = default;
    static Result<std::shared_ptr<Context>> create(OpenGlOptions const &options);
    ~Context();
    Context(Context const &) = delete;
    Context &operator=(Context const &) = delete;
    template <class F> auto invoke(F &&function) -> decltype(function()) {
        using R = decltype(function());
        if (std::this_thread::get_id() == worker.get_id())
            return function();
        std::promise<R> result;
        auto ready = result.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex);
            tasks.emplace_back([&function, &result] {
                try {
                    if constexpr (std::is_void_v<R>) {
                        function();
                        result.set_value();
                    } else
                        result.set_value(function());
                } catch (...) {
                    result.set_exception(std::current_exception());
                }
            });
        }
        changed.notify_one();
        return ready.get();
    }
    Result<void> error(char const *operation);
    Result<GLuint> program(std::string const &source);
    bool extension(char const *name) const { return extensions.count(name) != 0; }
    bool eglExtension(char const *name) const { return eglExtensions.count(name) != 0; }
    Gl gl;
    OpenGlOptions options;
    DeviceInfo info;
    GLint maxGroups[3]{}, maxLocal[3]{};
    GLint maxStorageBindings = 0, maxImages = 0, maxSamplers = 0, maxTexture = 0, maxTexture3d = 0;
    std::size_t maxUniformBytes = 0;
    EGLDisplay display = EGL_NO_DISPLAY;
    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNEGLCREATESYNCKHRPROC createSync = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroySync = nullptr;
    PFNEGLWAITSYNCKHRPROC waitSync = nullptr;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dupFence = nullptr;
#ifdef __ANDROID__
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC nativeClientBuffer = nullptr;
#endif
  private:
    Result<void> initialize();
    void loop();
    std::shared_ptr<Display> displayLease;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    std::set<std::string> extensions, eglExtensions;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> tasks;
    bool stopping = false;
};
template <class T> T procedure(char const *name) {
    return reinterpret_cast<T>(eglGetProcAddress(name));
}
} // namespace lutils::compute::gl_detail
