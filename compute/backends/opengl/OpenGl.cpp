#include "Resources.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>

namespace lutils::compute {
namespace gl_detail {
namespace {
GLenum accessMode(Access access) {
    return access == Access::Read    ? GL_READ_ONLY
           : access == Access::Write ? GL_WRITE_ONLY
                                     : GL_READ_WRITE;
}
struct ReadResult {
    ReadbackToken token;
    GLuint buffer = 0;
    std::size_t bytes = 0;
    WordSnapshot snapshot;
};
struct Submission final : Completion {
    explicit Submission(std::shared_ptr<Context> context) : context(std::move(context)) {}
    ~Submission() override {
        context->invoke([&] {
            if (fence) {
                context->gl.Finish();
                context->gl.DeleteSync(fence);
            }
            for (auto const &result : results)
                if (result.buffer)
                    context->gl.DeleteBuffers(1, &result.buffer);
            for (auto buffer : parameters)
                context->gl.DeleteBuffers(1, &buffer);
        });
    }
    Result<bool> finish(bool blocking) {
        if (failure)
            return *failure;
        if (complete)
            return true;
        auto &gl = context->gl;
        auto status =
            gl.ClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, blocking ? 1000000000 : 0);
        while (blocking && status == GL_TIMEOUT_EXPIRED)
            status = gl.ClientWaitSync(fence, 0, 1000000000);
        if (status == GL_WAIT_FAILED) {
            gl.Finish();
            failure = Error{ErrorCode::Device, "GL completion wait failed"};
            return *failure;
        }
        if (status == GL_TIMEOUT_EXPIRED)
            return false;
        gl.DeleteSync(fence);
        fence = nullptr;
        for (auto &result : results) {
            auto data = std::make_shared<std::vector<Word>>((result.bytes + 3) / 4);
            if (result.bytes) {
                gl.BindBuffer(GL_COPY_READ_BUFFER, result.buffer);
                auto mapped = gl.MapBufferRange(
                    GL_COPY_READ_BUFFER, 0, static_cast<GLsizeiptr>(result.bytes), GL_MAP_READ_BIT);
                if (!mapped) {
                    failure = Error{ErrorCode::Device, "GL readback mapping failed"};
                    return *failure;
                }
                std::memcpy(data->data(), mapped, result.bytes);
                if (!gl.UnmapBuffer(GL_COPY_READ_BUFFER)) {
                    failure = Error{ErrorCode::Device, "GL readback storage became invalid"};
                    return *failure;
                }
            }
            gl.DeleteBuffers(1, &result.buffer);
            result.buffer = 0;
            result.snapshot = std::move(data);
        }
        for (auto buffer : parameters)
            gl.DeleteBuffers(1, &buffer);
        parameters.clear();
        commands.clear();
        auto checked = context->error("complete submission");
        if (!checked) {
            failure = checked.error();
            return *failure;
        }
        complete = true;
        return true;
    }
    Result<void> wait() override {
        auto result = context->invoke([&] { return finish(true); });
        return result ? Result<void>{} : Result<void>{result.error()};
    }
    Result<bool> ready() override {
        return context->invoke([&] { return finish(false); });
    }
    Result<WordSnapshot> readback(ReadbackToken const &token) override {
        auto found = std::find_if(results.begin(), results.end(),
                                  [&](auto const &r) { return r.token == token; });
        if (found == results.end())
            return Error{ErrorCode::InvalidArgument,
                         "readback token does not belong to this submission"};
        auto checked = wait();
        if (!checked)
            return checked.error();
        return found->snapshot;
    }
    std::shared_ptr<Context> context;
    std::vector<Command> commands;
    std::vector<ReadResult> results;
    std::vector<GLuint> parameters;
    GLsync fence = nullptr;
    bool complete = false;
    std::optional<Error> failure;
};
struct ExternalUse {
    Storage *storage;
    Access access;
};
Result<void> acquire(Context &context, std::vector<ExternalUse> const &uses) {
    if (uses.empty())
        return {};
    if (!context.createSync || !context.destroySync || !context.waitSync || !context.dupFence ||
        !context.eglExtension("EGL_ANDROID_native_fence_sync") ||
        !context.eglExtension("EGL_KHR_wait_sync"))
        return Error{ErrorCode::Unsupported,
                     "external GL access requires EGL native fence and server wait support"};
    for (auto const &use : uses) {
        auto exported = use.storage->acquire(use.access);
        if (!exported)
            return exported.error();
        auto fd = std::move(exported).value();
        if (fd.get() < 0)
            continue;
        EGLint attributes[]{EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd.get(), EGL_NONE};
        auto sync = context.createSync(context.display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
        if (sync == EGL_NO_SYNC_KHR)
            return Error{ErrorCode::Device, "EGL acquire fence import failed"};
        (void)fd.release(); // EGL owns the fd only after successful creation.
        auto okay = context.waitSync(context.display, sync, 0);
        context.destroySync(context.display, sync);
        if (!okay)
            return Error{ErrorCode::Device, "EGL acquire fence wait failed"};
    }
    return {};
}
Result<void> release(Context &context, std::vector<ExternalUse> const &uses) {
    if (uses.empty())
        return {};
    auto sync = context.createSync(context.display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    if (sync == EGL_NO_SYNC_KHR)
        return Error{ErrorCode::Device, "EGL release fence creation failed"};
    context.gl.Flush();
    FileDescriptor fd{context.dupFence(context.display, sync)};
    context.destroySync(context.display, sync);
    if (fd.get() < 0)
        return Error{ErrorCode::Device, "EGL release fence export failed"};
    for (auto const &use : uses) {
        auto checked = use.storage->release(fd.get(), use.access);
        if (!checked)
            return checked;
    }
    return {};
}
struct GlDevice final : Device {
    explicit GlDevice(std::shared_ptr<Context> context) : context(std::move(context)) {}
    ~GlDevice() override {
        context->invoke([&] {
            for (auto const &submission : pending)
                (void)submission->finish(true);
            pending.clear();
            for (auto const &program : readPrograms)
                context->gl.DeleteProgram(program.second);
            if (copyProgram)
                context->gl.DeleteProgram(copyProgram);
        });
    }
    DeviceInfo info() const override { return context->info; }
    Result<BufferHandle> createBuffer(std::size_t words) override {
        if (words > UINT32_MAX)
            return Error{ErrorCode::Overflow, "buffer exceeds 32-bit kernel indexing"};
        return createBufferBytes(words * 4);
    }
    Result<BufferHandle> createBufferBytes(std::size_t bytes) override {
        return context->invoke([&]() -> Result<BufferHandle> {
            auto result = allocateBuffer(context, bytes);
            return result ? Result<BufferHandle>{std::move(result).value()}
                          : Result<BufferHandle>{result.error()};
        });
    }
    Result<BufferHandle> createImage(ImageDesc const &desc) override {
        return context->invoke([&]() -> Result<BufferHandle> {
            auto result = allocateImage(context, desc);
            return result ? Result<BufferHandle>{std::move(result).value()}
                          : Result<BufferHandle>{result.error()};
        });
    }
    Result<BufferHandle> importMemory(MemoryHandle const &memory) override {
        if (!memory)
            return Error{ErrorCode::InvalidArgument, "null external memory"};
        if (auto resident = memory->residentBuffer()) {
            if (!get(resident))
                return Error{ErrorCode::InvalidArgument, "foreign resident buffer"};
            return resident;
        }
        return context->invoke([&]() -> Result<BufferHandle> {
            auto result = importBuffer(context, memory);
            return result ? Result<BufferHandle>{std::move(result).value()}
                          : Result<BufferHandle>{result.error()};
        });
    }
    Result<BufferHandle> importImage(std::shared_ptr<NativeImage> const &image) override {
        return context->invoke([&]() -> Result<BufferHandle> {
            auto result = gl_detail::importImage(context, image);
            return result ? Result<BufferHandle>{std::move(result).value()}
                          : Result<BufferHandle>{result.error()};
        });
    }
    Result<KernelHandle> createKernel(KernelSource source) override {
        auto checked = validate(source);
        if (!checked)
            return checked.error();
        auto artifact = std::find_if(source.glsl.begin(), source.glsl.end(), [&](auto const &s) {
            return s.target == context->options.target;
        });
        if (artifact == source.glsl.end())
            return Error{ErrorCode::Unsupported,
                         "kernel has no artifact for the requested GL target"};
        if (!artifact->unsupportedReason.empty())
            return Error{ErrorCode::Unsupported, artifact->unsupportedReason};
        if (artifact->text.empty())
            return Error{ErrorCode::Compilation, "empty GL shader artifact"};
        if (source.localSize.x > static_cast<Word>(context->maxLocal[0]) ||
            source.localSize.y > static_cast<Word>(context->maxLocal[1]) ||
            source.localSize.z > static_cast<Word>(context->maxLocal[2]) ||
            std::uint64_t{source.localSize.x} * source.localSize.y * source.localSize.z >
                context->info.capabilities.maxWorkgroupInvocations ||
            source.sharedMemoryBytes > context->info.capabilities.maxSharedMemoryBytes ||
            ((source.parameterWords + 3) / 4) * 16 > context->maxUniformBytes)
            return Error{ErrorCode::Unsupported,
                         "kernel exceeds GL workgroup/shared/uniform limits"};
        GLint buffers = 0, images = 0, samplers = 0;
        if (source.resources.empty())
            buffers = static_cast<GLint>(source.bindings.size());
        else
            for (auto const &r : source.resources) {
                if (r.sampled)
                    ++samplers;
                else if (r.image)
                    ++images;
                else
                    ++buffers;
            }
        if (buffers > context->maxStorageBindings || images > context->maxImages ||
            samplers > context->maxSamplers)
            return Error{ErrorCode::Unsupported, "kernel exceeds GL resource binding limits"};
        auto text = artifact->text;
        auto result = std::make_shared<Program>(context, std::move(source), std::move(text));
        auto compiled = context->invoke([&] { return result->variant({}); });
        if (!compiled)
            return compiled.error();
        return KernelHandle{std::move(result)};
    }
    Result<void> upload(BufferHandle const &buffer, std::vector<Word> const &words) override {
        CommandList list;
        auto recorded = list.upload(buffer, words);
        if (!recorded)
            return recorded;
        auto result = submit(list);
        return result ? result.value()->wait() : Result<void>{result.error()};
    }
    Result<std::vector<Word>> download(BufferHandle const &buffer) override {
        CommandList list;
        auto token = list.readback(buffer);
        if (!token)
            return token.error();
        auto result = submit(list);
        if (!result)
            return result.error();
        auto data = result.value()->readback(token.value());
        return data ? Result<std::vector<Word>>{*data.value()}
                    : Result<std::vector<Word>>{data.error()};
    }
    Storage *get(BufferHandle const &buffer) const {
        auto result = dynamic_cast<Storage *>(buffer.get());
        return result && result->context == context ? result : nullptr;
    }
    Result<void> check(Upload const &command) {
        auto storage = get(command.buffer);
        if (!storage)
            return Error{ErrorCode::InvalidArgument, "foreign GL buffer"};
        if (storage->external())
            return Error{ErrorCode::Unsupported,
                         "ordinary upload is unavailable for external GL resources"};
        return {};
    }
    Result<void> check(Readback const &command) {
        auto storage = get(command.buffer);
        if (!storage)
            return Error{ErrorCode::InvalidArgument, "foreign GL buffer"};
        if (storage->external())
            return Error{ErrorCode::Unsupported,
                         "ordinary readback is unavailable for external GL resources"};
        if (storage->samplerKind)
            return Error{ErrorCode::Unsupported,
                         "raw external YUV requires a conversion kernel before readback"};
        if (storage->image) {
            auto words = storage->wordCount();
            auto groups = (words + 63) / 64;
            if (words > static_cast<std::size_t>(INT32_MAX) ||
                words * 4 > context->info.maxBufferBytes ||
                groups > std::uint64_t{static_cast<Word>(context->maxGroups[0])} *
                             static_cast<Word>(context->maxGroups[1]))
                return Error{ErrorCode::Unsupported,
                             "image readback exceeds GL buffer/dispatch limits"};
            if (!storage->storageImage)
                return Error{ErrorCode::Unsupported,
                             "native image cannot bind for storage readback"};
            auto program = readProgram(*storage->image);
            if (!program)
                return program.error();
        }
        return {};
    }
    Result<void> check(Dispatch const &command) {
        auto program = dynamic_cast<Program *>(command.kernel.get());
        if (!program || program->context != context)
            return Error{ErrorCode::InvalidArgument, "foreign GL kernel"};
        auto const &source = program->source();
        Word extent[]{command.extent.x, command.extent.y, command.extent.z};
        Word local[]{source.localSize.x, source.localSize.y, source.localSize.z};
        for (unsigned i = 0; i < 3; ++i)
            if ((std::uint64_t{extent[i]} + local[i] - 1) / local[i] >
                static_cast<Word>(context->maxGroups[i]))
                return Error{ErrorCode::Unsupported, "dispatch exceeds GL workgroup count limits"};
        for (std::size_t i = 0; i < command.buffers.size(); ++i) {
            auto storage = get(command.buffers[i]);
            if (!storage)
                return Error{ErrorCode::InvalidArgument, "foreign GL buffer"};
            bool sampled = !source.resources.empty() && source.resources[i].sampled;
            if (storage->image && !sampled && !storage->storageImage)
                return Error{ErrorCode::Unsupported,
                             "native image does not support storage binding"};
            if (storage->image && !sampled && context->options.target == GlslTarget::OpenGles31 &&
                source.bindings[i] == Access::ReadWrite &&
                storage->image->format != kernel::ImageFormat::R32F &&
                storage->image->format != kernel::ImageFormat::R32UI &&
                storage->image->format != kernel::ImageFormat::R32I)
                return Error{ErrorCode::Unsupported,
                             "GLES read/write image requires a single 32-bit channel"};
        }
        auto compiled = program->variant(command.buffers);
        return compiled ? Result<void>{} : Result<void>{compiled.error()};
    }
    Result<void> check(CopyImageRegion const &command) {
        auto source = get(command.source), destination = get(command.destination);
        if (!source || !destination)
            return Error{ErrorCode::InvalidArgument, "foreign GL image"};
        if (source->samplerKind || !destination->storageImage)
            return Error{ErrorCode::Unsupported,
                         "image copy requires RGBA sampling and writable RGBA storage"};
        if (!copyProgram) {
            auto compiled = context->program(
                shaderVersion(*context) +
                "layout(local_size_x=8,local_size_y=8) in;\n"
                "layout(binding=0) uniform highp sampler2D inputImage;\n"
                "layout(binding=0,rgba8) writeonly uniform highp image2D "
                "outputImage;\n"
                "uniform ivec4 region; uniform ivec4 placement; uniform vec4 "
                "background;\n"
                "void main(){ ivec2 p=ivec2(gl_GlobalInvocationID.xy); ivec2 "
                "size=imageSize(outputImage);"
                "if(any(greaterThanEqual(p,size)))return; ivec2 q=p-placement.xy;"
                "if(all(greaterThanEqual(q,ivec2(0)))&&all(lessThan(q,region.zw)))"
                "imageStore(outputImage,p,texelFetch(inputImage,q+region.xy,0));"
                "else if(placement.z!=0)imageStore(outputImage,p,background); }\n");
            if (!compiled)
                return compiled.error();
            copyProgram = compiled.value();
        }
        return {};
    }
    Result<GLuint> readProgram(ImageDesc const &image) {
        auto key = std::make_pair(image.format, image.dimensions);
        auto found = readPrograms.find(key);
        if (found != readPrograms.end())
            return found->second;
        auto f = kernel::formatInfo(image.format);
        auto componentBytes = f.bytes / f.channels;
        // Immutable storage permits a compatible unsigned image view. Read raw
        // bits to retain SNORM -128 and floating-point NaN payloads exactly.
        auto view = unsignedFormat(image.format);
        std::ostringstream source;
        source << shaderVersion(*context) << "layout(local_size_x=64) in;\nlayout(binding=0,"
               << kernel::formatInfo(view).glsl << ") readonly uniform highp uimage"
               << image.dimensions << "D inputImage;\n"
               << "layout(std430,binding=0) writeonly buffer Output { uint "
                  "words[]; };\nuniform ivec4 dimensions;\n"
               << "uint component(uint n){uint pixel=n/" << f.channels << "u; uint c=n%"
               << f.channels << "u;"
               << "ivec3 "
                  "p=ivec3(int(pixel%uint(dimensions.x)),int(pixel/"
                  "uint(dimensions.x)%uint(dimensions.y)),"
               << "int(pixel/(uint(dimensions.x)*uint(dimensions.y))));"
               << "uint v=imageLoad(inputImage,"
               << (image.dimensions == 3   ? "p"
                   : image.dimensions == 2 ? "p.xy"
                                           : "p.x")
               << ")[c]; return ";
        source << "v";
        source << ";}\nvoid main(){ uint "
                  "w=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_"
                  "NumWorkGroups.x*64u;"
               << "if(w>=uint(dimensions.w))return; uint bits=0u;";
        for (std::size_t lane = 0; lane < 4 / componentBytes; ++lane)
            source << "{uint n=w*" << 4 / componentBytes << "u+" << lane << "u;"
                   << "if(n<uint(dimensions.x)*uint(dimensions.y)*uint(dimensions.z)*" << f.channels
                   << "u)" << "bits|=(component(n)&"
                   << (componentBytes == 4   ? "0xffffffffu"
                       : componentBytes == 2 ? "65535u"
                                             : "255u")
                   << ")<<" << lane * componentBytes * 8 << "u;}";
        source << "words[w]=bits;}\n";
        auto program = context->program(source.str());
        if (!program)
            return program.error();
        readPrograms.emplace(key, program.value());
        return program.value();
    }
    void use(std::vector<ExternalUse> &uses, BufferHandle const &handle, Access access) {
        auto storage = get(handle);
        if (!storage->external())
            return;
        // Retain every imported view: each wrapper has its own producer lease and
        // release-fence state, even when two wrappers alias the same allocation.
        auto found = std::find_if(uses.begin(), uses.end(),
                                  [&](auto const &u) { return u.storage == storage; });
        if (found == uses.end())
            uses.push_back({storage, access});
        else if (found->access != access)
            found->access = Access::ReadWrite;
    }
    void collect(std::vector<ExternalUse> &uses, Upload const &c) {
        use(uses, c.buffer, Access::Write);
    }
    void collect(std::vector<ExternalUse> &uses, Readback const &c) {
        use(uses, c.buffer, Access::Read);
    }
    void collect(std::vector<ExternalUse> &uses, CopyImageRegion const &c) {
        use(uses, c.source, Access::Read);
        use(uses, c.destination, Access::Write);
    }
    void collect(std::vector<ExternalUse> &uses, Dispatch const &c) {
        for (std::size_t i = 0; i < c.buffers.size(); ++i)
            use(uses, c.buffers[i], c.kernel->source().bindings[i]);
    }
    Result<void> execute(Upload const &command, Submission &) {
        auto storage = get(command.buffer);
        return gl_detail::upload(*storage, command.words->data(),
                                 storage->image ? storage->bytes : storage->wordCount() * 4);
    }
    Result<void> execute(Dispatch const &command, Submission &submission) {
        if (!command.extent.x || !command.extent.y || !command.extent.z)
            return {};
        auto &gl = context->gl;
        auto program = static_cast<Program *>(command.kernel.get());
        auto compiled = program->variant(command.buffers);
        if (!compiled)
            return compiled.error();
        gl.UseProgram(compiled.value());
        GLuint bufferIndex = 0, imageIndex = 0, samplerIndex = 0;
        auto const &source = program->source();
        for (std::size_t i = 0; i < command.buffers.size(); ++i) {
            auto storage = get(command.buffers[i]);
            if (!storage->image)
                gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, bufferIndex++, storage->object);
            else if (!source.resources.empty() && source.resources[i].sampled) {
                gl.ActiveTexture(GL_TEXTURE0 + samplerIndex++);
                gl.BindTexture(storage->target, storage->object);
            } else
                gl.BindImageTexture(imageIndex++, storage->object, 0,
                                    storage->image->dimensions == 3 ? GL_TRUE : GL_FALSE, 0,
                                    accessMode(source.bindings[i]),
                                    format(storage->image->format).internal);
        }
        GLuint parameters = 0;
        gl.GenBuffers(1, &parameters);
        submission.parameters.push_back(parameters);
        gl.BindBuffer(GL_UNIFORM_BUFFER, parameters);
        auto words = command.parameters;
        words.resize(std::max<std::size_t>(4, ((words.size() + 3) / 4) * 4));
        gl.BufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(words.size() * 4), words.data(),
                      GL_STREAM_DRAW);
        gl.BindBufferBase(GL_UNIFORM_BUFFER, 0, parameters);
        gl.DispatchCompute((command.extent.x - 1) / source.localSize.x + 1,
                           (command.extent.y - 1) / source.localSize.y + 1,
                           (command.extent.z - 1) / source.localSize.z + 1);
        return context->error("dispatch compute");
    }
    Result<void> execute(Readback const &command, Submission &submission) {
        auto &gl = context->gl;
        auto storage = get(command.buffer);
        GLuint buffer = 0;
        gl.GenBuffers(1, &buffer);
        auto bytes = storage->image ? storage->bytes : storage->wordCount() * 4;
        submission.results.push_back({command.token, buffer, bytes, {}});
        gl.BindBuffer(GL_COPY_WRITE_BUFFER, buffer);
        gl.BufferData(GL_COPY_WRITE_BUFFER,
                      static_cast<GLsizeiptr>(std::max<std::size_t>(4, storage->wordCount() * 4)),
                      nullptr, GL_STREAM_READ);
        if (!storage->bytes)
            return {};
        if (!storage->image) {
            gl.BindBuffer(GL_COPY_READ_BUFFER, storage->object);
            gl.CopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0,
                                 static_cast<GLsizeiptr>(bytes));
        } else {
            auto program = readProgram(*storage->image);
            if (!program)
                return program.error();
            gl.UseProgram(program.value());
            auto const &image = *storage->image;
            auto view = unsignedFormat(image.format);
            gl.BindImageTexture(0, storage->object, 0, image.dimensions == 3 ? GL_TRUE : GL_FALSE,
                                0, GL_READ_ONLY, format(view).internal);
            gl.BindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffer);
            gl.Uniform4i(gl.GetUniformLocation(program.value(), "dimensions"),
                         static_cast<GLint>(image.extent.x), static_cast<GLint>(image.extent.y),
                         static_cast<GLint>(image.extent.z),
                         static_cast<GLint>(storage->wordCount()));
            auto groups = (storage->wordCount() + 63) / 64;
            auto x = std::min(groups, static_cast<std::size_t>(context->maxGroups[0]));
            gl.DispatchCompute(static_cast<GLuint>(x), static_cast<GLuint>((groups + x - 1) / x),
                               1);
        }
        return context->error("snapshot readback");
    }
    Result<void> execute(CopyImageRegion const &command, Submission &) {
        auto &gl = context->gl;
        auto source = get(command.source), destination = get(command.destination);
        gl.UseProgram(copyProgram);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(source->target, source->object);
        gl.BindImageTexture(0, destination->object, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        gl.Uniform4i(gl.GetUniformLocation(copyProgram, "region"),
                     static_cast<GLint>(command.sourceX), static_cast<GLint>(command.sourceY),
                     static_cast<GLint>(command.width), static_cast<GLint>(command.height));
        gl.Uniform4i(gl.GetUniformLocation(copyProgram, "placement"),
                     static_cast<GLint>(command.destinationX),
                     static_cast<GLint>(command.destinationY), command.background ? 1 : 0, 0);
        if (command.background) {
            auto const &v = *command.background;
            gl.Uniform4f(gl.GetUniformLocation(copyProgram, "background"), v[0], v[1], v[2], v[3]);
        }
        gl.DispatchCompute((destination->image->extent.x + 7) / 8,
                           (destination->image->extent.y + 7) / 8, 1);
        return context->error("copy image region");
    }
    Result<std::shared_ptr<Completion>> submit(CommandList const &commands) override {
        // The caller retains the submission outside the GL worker. Destruction
        // of the final Context owner therefore never runs on its own thread.
        auto submission = std::make_shared<Submission>(context);
        submission->commands = commands.commands();
        auto checked = context->invoke([&]() -> Result<void> {
            for (auto const &command : submission->commands) {
                auto valid = std::visit([&](auto const &c) { return check(c); }, command);
                if (!valid)
                    return valid;
            }
            while (!pending.empty()) {
                auto done = pending.front()->finish(pending.size() >= context->options.maxInFlight);
                if (!done) {
                    auto failure = done.error();
                    pending.pop_front();
                    return failure;
                }
                if (!done.value())
                    break;
                pending.pop_front();
            }
            std::vector<ExternalUse> external;
            for (auto const &command : submission->commands)
                std::visit([&](auto const &c) { collect(external, c); }, command);
            auto acquired = acquire(*context, external);
            if (!acquired)
                return acquired;
            auto &gl = context->gl;
            for (auto const &command : submission->commands) {
                // Shader writes must become visible to the next consumer type,
                // including transfer/readback and texture sampling commands.
                gl.MemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                                 GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT |
                                 GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                                 GL_PIXEL_BUFFER_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
                auto result =
                    std::visit([&](auto const &c) { return execute(c, *submission); }, command);
                if (!result) {
                    gl.Finish();
                    return result;
                }
            }
            gl.MemoryBarrier(GL_ALL_BARRIER_BITS);
            auto published = release(*context, external);
            if (!published) {
                gl.Finish();
                return published;
            }
            submission->fence = gl.FenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            gl.Flush();
            if (!submission->fence) {
                gl.Finish();
                return Error{ErrorCode::Device, "GL completion fence creation failed"};
            }
            pending.push_back(submission);
            return context->error("submit command list");
        });
        if (!checked)
            return checked.error();
        return std::shared_ptr<Completion>{std::move(submission)};
    }
    std::shared_ptr<Context> context;
    std::deque<std::shared_ptr<Submission>> pending;
    std::map<std::pair<kernel::ImageFormat, Word>, GLuint> readPrograms;
    GLuint copyProgram = 0;
};
} // namespace
} // namespace gl_detail
Result<std::unique_ptr<Device>> createOpenGlDevice(OpenGlOptions options) {
    auto context = gl_detail::Context::create(options);
    if (!context)
        return context.error();
    return std::unique_ptr<Device>{
        std::make_unique<gl_detail::GlDevice>(std::move(context).value())};
}
} // namespace lutils::compute
