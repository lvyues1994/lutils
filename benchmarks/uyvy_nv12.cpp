#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <lutils/image/Conversion.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#ifdef LUTILS_BENCHMARK_VULKAN
#include <lutils/compute/Vulkan.hpp>
#endif

namespace co = lutils::compute;
namespace im = lutils::image;
namespace fs = std::filesystem;
namespace {
using Clock = std::chrono::steady_clock;
template <class T> T take(lutils::Result<T> result) {
    if (!result)
        throw std::runtime_error{result.error().message};
    return std::move(result).value();
}
void check(lutils::Result<void> result) {
    if (!result)
        throw std::runtime_error{result.error().message};
}
double milliseconds(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}
std::uint32_t number(std::string const &text) {
    std::size_t consumed = 0;
    auto value = std::stoull(text, &consumed);
    if (text.empty() || text[0] == '-' || consumed != text.size() || !value || value > UINT32_MAX)
        throw std::runtime_error{"expected a positive 32-bit integer: " + text};
    return static_cast<std::uint32_t>(value);
}
struct Options {
    std::string backend;
    std::uint32_t width;
    std::uint32_t height;
    fs::path report;
    std::uint32_t warmup = 1;
    std::uint32_t rounds = 3;
    std::uint32_t batch = 30;
    bool hardware = false;
    bool validation = false;
    bool timestamps = false;
    bool hostCached = false;
    bool deviceLocal = false;
    bool reuse = true;
    bool readbackCache = true;
    std::uint32_t cpuWorkers = 0;
    std::uint32_t pipelineDepth = 0;
    std::vector<fs::path> inputs;
};
Options parse(int argc, char **argv) {
    if (argc < 6)
        throw std::runtime_error{"usage: uyvy_nv12_benchmark cpu|vulkan width height report.json "
                                 "[--warmup N] [--rounds N] [--batch N] [--require-hardware] "
                                 "[--validation] [--timestamps] [--host-cached] [--device-local] "
                                 "[--no-reuse] [--no-readback-cache] [--cpu-workers N] "
                                 "[--pipeline-depth N] input.uyvy [...]"};
    Options out{};
    out.backend = argv[1];
    out.width = number(argv[2]);
    out.height = number(argv[3]);
    out.report = argv[4];
    for (int i = 5; i < argc; ++i) {
        std::string arg{argv[i]};
        if (arg == "--require-hardware")
            out.hardware = true;
        else if (arg == "--validation")
            out.validation = true;
        else if (arg == "--timestamps")
            out.timestamps = true;
        else if (arg == "--host-cached")
            out.hostCached = true;
        else if (arg == "--device-local")
            out.deviceLocal = true;
        else if (arg == "--no-reuse")
            out.reuse = false;
        else if (arg == "--no-readback-cache")
            out.readbackCache = false;
        else if (arg == "--warmup" || arg == "--rounds" || arg == "--batch" ||
                 arg == "--pipeline-depth" || arg == "--cpu-workers") {
            if (++i == argc)
                throw std::runtime_error{"missing value after " + arg};
            auto n = number(argv[i]);
            if (arg == "--warmup")
                out.warmup = n;
            else if (arg == "--rounds")
                out.rounds = n;
            else if (arg == "--pipeline-depth")
                out.pipelineDepth = n;
            else if (arg == "--cpu-workers")
                out.cpuWorkers = n;
            else
                out.batch = n;
        } else if (arg.compare(0, 2, "--") == 0)
            throw std::runtime_error{"unknown option: " + arg};
        else
            out.inputs.emplace_back(arg);
    }
    if (out.inputs.empty() || (out.backend != "cpu" && out.backend != "vulkan"))
        throw std::runtime_error{"expected cpu|vulkan and at least one input file"};
    if (out.backend == "cpu" &&
        (out.hardware || out.validation || out.timestamps || out.hostCached || out.deviceLocal ||
         !out.reuse || !out.readbackCache))
        throw std::runtime_error{
            "hardware, validation, timestamps and memory options require Vulkan"};
    if (fs::exists(out.report))
        throw std::runtime_error{"report already exists: " + out.report.string()};
    return out;
}
// Independent byte oracle: preserve Y; vertically average U/V, rounding upward.
void verify(im::ConstFrameView const &src, im::ConstFrameView const &dst) {
    for (std::size_t y = 0; y < src.desc.height; ++y) {
        auto s = im::row(src.planes[0], y);
        auto d = im::row(dst.planes[0], y);
        for (std::size_t x = 0; x < src.desc.width; ++x)
            if (d[x] != s[2 * x + 1])
                throw std::runtime_error{"Y differs from byte oracle"};
    }
    for (std::size_t y = 0; y < (src.desc.height / 2u + src.desc.height % 2u); ++y) {
        auto a = im::row(src.planes[0], 2 * y);
        auto b = im::row(src.planes[0], std::min(2 * y + 1, std::size_t{src.desc.height - 1}));
        auto d = im::row(dst.planes[1], y);
        for (std::size_t x = 0; x < src.desc.width; ++x) {
            auto expected =
                (std::to_integer<unsigned>(a[2 * x]) + std::to_integer<unsigned>(b[2 * x]) + 1u) /
                2u;
            if (std::to_integer<unsigned>(d[x]) != expected)
                throw std::runtime_error{"UV differs from byte oracle"};
        }
    }
}
std::vector<im::HostFrame> load(Options const &options, im::FrameDesc const &desc) {
    std::vector<im::HostFrame> frames;
    for (auto const &path : options.inputs) {
        auto frame = take(im::HostFrame::create(desc));
        auto plane = frame.view().planes[0];
        if (fs::file_size(path) != plane.capacity ||
            plane.capacity > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
            throw std::runtime_error{"input size does not match tightly packed UYVY: " +
                                     path.string()};
        std::ifstream in{path, std::ios::binary};
        in.read(reinterpret_cast<char *>(plane.base), static_cast<std::streamsize>(plane.capacity));
        if (!in)
            throw std::runtime_error{"cannot read " + path.string()};
        frames.push_back(std::move(frame));
    }
    return frames;
}
std::optional<double> deviceTime(co::Completion &completion, bool enabled) {
#ifdef LUTILS_BENCHMARK_VULKAN
    if (enabled) {
        auto *timed = dynamic_cast<co::VulkanCompletion *>(&completion);
        if (!timed)
            throw std::runtime_error{"Vulkan timestamp interface unavailable"};
        return take(timed->elapsedNanoseconds()) / 1000000.0;
    }
#else
    (void)completion;
    (void)enabled;
#endif
    return {};
}
struct Sample {
    double upload;
    double submitWait;
    double download;
    double endToEnd;
    std::optional<double> device;
};
struct ResidentSample {
    std::size_t frames;
    double submitWait;
    std::optional<double> device;
};
struct Results {
    co::DeviceInfo device;
    double setupMs;
    std::size_t deviceBytesPerFrame;
    std::vector<Sample> single;
    std::vector<ResidentSample> resident;
    std::vector<double> pipelineRounds;
    std::vector<double> pipelineLatency;
    std::vector<double> pipelineDevice;
    std::size_t depth = 0;
    std::size_t peakOutstanding = 0;
    std::size_t hostOutputBytes = 0;
    std::array<std::uint64_t, 4> countsBefore{};
    std::array<std::uint64_t, 4> countsAfter{};
};
Results run(co::Device &device, Options const &options, im::FrameDesc const &srcDesc,
            std::vector<im::HostFrame> &inputs) {
    auto dstDesc = srcDesc;
    dstDesc.format = im::nv12();
    dstDesc.color.vertical = im::ChromaLocation::Midpoint;
    auto setup = Clock::now();
    auto output = take(im::HostFrame::create(dstDesc));
    auto source = take(im::DeviceFrame::create(device, srcDesc));
    auto target = take(im::DeviceFrame::create(device, dstDesc));
    auto plan = take(im::ConversionPlan::prepare(device, srcDesc, dstDesc));
    co::CommandList single;
    check(plan.record(single, source, target));
    Results results{};
    results.device = device.info();
    results.setupMs = milliseconds(setup, Clock::now());
    results.deviceBytesPerFrame =
        (source.layout().words + target.layout().words) * sizeof(co::Word);
    auto singlePass = [&](bool measured) {
        for (auto &input : inputs) {
            auto start = Clock::now();
            check(im::upload(device, im::readOnly(input.view()), source));
            auto uploaded = Clock::now();
            auto done = take(device.submit(single));
            check(done->wait());
            auto executed = Clock::now();
            check(im::download(device, target, output.view()));
            auto downloaded = Clock::now();
            auto elapsed = deviceTime(*done, options.timestamps);
            // Native timestamp queries are harvested by wait(); this accessor reads the cache.
            auto cleanup = Clock::now();
            done.reset();
            auto end = Clock::now();
            if (measured)
                results.single.push_back(
                    {milliseconds(start, uploaded), milliseconds(uploaded, executed),
                     milliseconds(executed, downloaded),
                     milliseconds(start, downloaded) + milliseconds(cleanup, end), elapsed});
            verify(im::readOnly(input.view()), im::readOnly(output.view()));
        }
    };
    for (std::uint32_t i = 0; i < options.warmup; ++i)
        singlePass(false);
    for (std::uint32_t i = 0; i < options.rounds; ++i)
        singlePass(true);

    // Different frames have different buffers. Uploads and result checks are
    // untimed.
    auto count = std::min(std::size_t{options.batch}, inputs.size());
    std::vector<im::DeviceFrame> sources;
    std::vector<im::DeviceFrame> targets;
    for (std::size_t i = 0; i < count; ++i) {
        sources.push_back(take(im::DeviceFrame::create(device, srcDesc)));
        targets.push_back(take(im::DeviceFrame::create(device, dstDesc)));
    }
    for (std::size_t offset = 0; offset < inputs.size(); offset += count) {
        auto size = std::min(count, inputs.size() - offset);
        co::CommandList batch;
        for (std::size_t i = 0; i < size; ++i) {
            check(im::upload(device, im::readOnly(inputs[offset + i].view()), sources[i]));
            check(plan.record(batch, sources[i], targets[i]));
        }
        auto residentPass = [&](bool measured) {
            auto start = Clock::now();
            auto done = take(device.submit(batch));
            check(done->wait());
            auto end = Clock::now();
            auto elapsed = deviceTime(*done, options.timestamps);
            if (measured)
                results.resident.push_back({size, milliseconds(start, end), elapsed});
        };
        for (std::uint32_t i = 0; i < options.warmup; ++i)
            residentPass(false);
        for (std::uint32_t i = 0; i < options.rounds; ++i)
            residentPass(true);
        for (std::size_t i = 0; i < size; ++i) {
            check(im::download(device, targets[i], output.view()));
            verify(im::readOnly(inputs[offset + i].view()), im::readOnly(output.view()));
        }
    }
    return results;
}
// One upload/dispatch/readback submission per frame. Results are checked only
// after the entire round has drained, so the independent oracle cannot add
// pipeline bubbles.
template <class Counters>
Results runPipeline(co::Device &device, Options const &options, im::FrameDesc const &srcDesc,
                    std::vector<im::HostFrame> &inputs, Counters counters) {
    auto dstDesc = srcDesc;
    dstDesc.format = im::nv12();
    dstDesc.color.vertical = im::ChromaLocation::Midpoint;
    auto setup = Clock::now();
    auto plan = take(im::ConversionPlan::prepare(device, srcDesc, dstDesc));
    struct Slot {
        im::DeviceFrame source;
        im::DeviceFrame target;
        std::optional<im::FrameReadback> readback;
        std::shared_ptr<co::Completion> done;
        std::size_t frame = 0;
        Clock::time_point start;
    };
    Results results{};
    results.device = device.info();
    results.depth = std::min(std::size_t{options.pipelineDepth}, inputs.size());
    std::vector<Slot> slots;
    slots.reserve(results.depth);
    for (std::size_t i = 0; i < results.depth; ++i)
        slots.push_back({take(im::DeviceFrame::create(device, srcDesc)),
                         take(im::DeviceFrame::create(device, dstDesc)),
                         {},
                         {},
                         0,
                         {}});
    std::vector<im::HostFrame> outputs;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        outputs.push_back(take(im::HostFrame::create(dstDesc)));
        for (auto const &plane : outputs.back().view().planes)
            results.hostOutputBytes += plane.capacity;
    }
    results.setupMs = milliseconds(setup, Clock::now());
    results.deviceBytesPerFrame =
        (slots[0].source.layout().words + slots[0].target.layout().words) * sizeof(co::Word);
    // Reserve all result bookkeeping before measuring.
    results.pipelineRounds.reserve(options.rounds);
    results.pipelineLatency.reserve(inputs.size() * options.rounds);
    results.pipelineDevice.reserve(inputs.size() * options.rounds);
    auto pass = [&](bool measured) {
        for (auto &output : outputs)
            for (auto const &plane : output.view().planes)
                std::fill_n(plane.base, plane.capacity, std::byte{0xA5});
        std::size_t outstanding = 0;
        auto collect = [&](Slot &slot) {
            check(slot.readback->copyTo(*slot.done, outputs[slot.frame].view()));
            auto elapsed = deviceTime(*slot.done, options.timestamps);
            slot.done.reset();
            slot.readback.reset();
            auto end = Clock::now();
            --outstanding;
            if (measured) {
                results.pipelineLatency.push_back(milliseconds(slot.start, end));
                if (elapsed)
                    results.pipelineDevice.push_back(*elapsed);
            }
        };
        auto start = Clock::now();
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            auto &slot = slots[i % slots.size()];
            if (slot.done)
                collect(slot);
            slot.start = Clock::now();
            co::CommandList commands;
            check(im::recordUpload(commands, im::readOnly(inputs[i].view()), slot.source));
            check(plan.record(commands, slot.source, slot.target));
            slot.readback = take(im::recordReadback(commands, slot.target));
            slot.done = take(device.submit(commands));
            slot.frame = i;
            ++outstanding;
            results.peakOutstanding = std::max(results.peakOutstanding, outstanding);
        }
        // FIFO drain, including a partial final set of slots.
        for (std::size_t i = inputs.size() - slots.size(); i < inputs.size(); ++i)
            collect(slots[i % slots.size()]);
        auto end = Clock::now();
        if (measured)
            results.pipelineRounds.push_back(milliseconds(start, end));
        for (std::size_t i = 0; i < inputs.size(); ++i)
            verify(im::readOnly(inputs[i].view()), im::readOnly(outputs[i].view()));
    };
    for (std::uint32_t i = 0; i < options.warmup; ++i)
        pass(false);
    results.countsBefore = counters();
    for (std::uint32_t i = 0; i < options.rounds; ++i)
        pass(true);
    results.countsAfter = counters();
    return results;
}
void quoted(std::ostream &out, std::string const &value) {
    out << '"';
    for (char c : value) {
        auto byte = static_cast<unsigned char>(c);
        if (c == '"' || c == '\\')
            out << '\\' << c;
        else if (byte < 32)
            out << "\\u00" << "0123456789abcdef"[byte >> 4] << "0123456789abcdef"[byte & 15];
        else
            out << c;
    }
    out << '"';
}
void summary(std::ostream &out, std::vector<double> values) {
    if (values.empty()) {
        out << "null";
        return;
    }
    std::sort(values.begin(), values.end());
    auto percentile = [&](double p) {
        auto index =
            static_cast<std::size_t>(std::ceil(p * static_cast<double>(values.size()))) - 1;
        return values[index];
    };
    out << "{\"samples\":" << values.size() << ",\"min_ms\":" << values.front()
        << ",\"median_ms\":" << percentile(0.5) << ",\"p95_ms\":" << percentile(0.95)
        << ",\"max_ms\":" << values.back() << '}';
}
void report(Options const &options, Results const &results, std::uint64_t errors,
            std::uint64_t warnings) {
    std::ofstream out{options.report};
    out << std::setprecision(9) << "{\n\"schema\":3,\"backend\":";
    quoted(out, options.backend);
    out << ",\"device\":";
    quoted(out, results.device.name);
    out << ",\"compiler\":";
    quoted(out, __VERSION__);
    out << ",\"build\":";
    quoted(out, LUTILS_BENCHMARK_BUILD);
    out << std::boolalpha << ",\"software\":" << results.device.software
        << ",\"validation\":" << options.validation << ",\"validation_errors\":" << errors
        << ",\"validation_warnings\":" << warnings << ",\"timestamps\":" << options.timestamps
        << ",\"width\":" << options.width << ",\"height\":" << options.height
        << ",\"local_size_x\":" << LUTILS_BENCHMARK_LOCAL_SIZE_X
        << ",\"prefer_host_cached\":" << options.hostCached
        << ",\"device_local\":" << options.deviceLocal
        << ",\"reuse_submission_resources\":" << options.reuse
        << ",\"readback_cache\":" << options.readbackCache
        << ",\"cpu_workers\":" << options.cpuWorkers << ",\"warmup_passes\":" << options.warmup
        << ",\"measured_passes\":" << options.rounds << ",\"batch_limit\":" << options.batch
        << ",\"plan_and_buffer_setup_ms\":" << results.setupMs
        << ",\"storage_buffer_bytes_per_frame\":" << results.deviceBytesPerFrame
        << ",\"oracle\":\"byte_exact_Y_and_rounded_vertical_UV_average\","
           "\n\"inputs\":[";
    for (std::size_t i = 0; i < options.inputs.size(); ++i) {
        if (i)
            out << ',';
        quoted(out, options.inputs[i].string());
    }
    out << "],\n\"single_summary\":{";
    std::vector<double> upload, submit, download, endToEnd, device;
    for (auto const &s : results.single) {
        upload.push_back(s.upload);
        submit.push_back(s.submitWait);
        download.push_back(s.download);
        endToEnd.push_back(s.endToEnd);
        if (s.device)
            device.push_back(*s.device);
    }
    out << "\"pack_upload\":";
    summary(out, upload);
    out << ",\"submit_wait\":";
    summary(out, submit);
    out << ",\"download_unpack\":";
    summary(out, download);
    out << ",\"steady_state_e2e\":";
    summary(out, endToEnd);
    out << ",\"device_commands\":";
    summary(out, device);
    submit.clear();
    device.clear();
    std::uint64_t totalFrames = 0;
    double totalSubmit = 0;
    for (auto const &s : results.resident) {
        totalFrames += s.frames;
        totalSubmit += s.submitWait;
        submit.push_back(s.submitWait / static_cast<double>(s.frames));
        if (s.device)
            device.push_back(*s.device / static_cast<double>(s.frames));
    }
    out << "},\n\"resident_aggregate\":{\"frames\":" << totalFrames
        << ",\"submit_wait_ms\":" << totalSubmit << ",\"frames_per_second\":";
    if (totalSubmit > 0)
        out << static_cast<double>(totalFrames) * 1000.0 / totalSubmit;
    else
        out << "null";
    out << "},\n\"resident_normalized_batch_summary\":{\"submit_wait\":";
    summary(out, submit);
    out << ",\"device_commands\":";
    summary(out, device);
    out << "},\n\"single_columns\":[\"pack_upload_ms\",\"submit_wait_ms\","
           "\"download_unpack_ms\",\"steady_state_e2e_ms\",\"device_commands_"
           "ms\"],\n\"single_"
           "samples\":[";
    for (std::size_t i = 0; i < results.single.size(); ++i) {
        auto const &s = results.single[i];
        if (i)
            out << ',';
        out << '\n'
            << '[' << s.upload << ',' << s.submitWait << ',' << s.download << ',' << s.endToEnd
            << ',';
        if (s.device)
            out << *s.device;
        else
            out << "null";
        out << ']';
    }
    out << "],\n\"resident_columns\":[\"frames\",\"submit_wait_ms\",\"device_"
           "commands_ms\"],"
           "\n\"resident_samples\":[";
    for (std::size_t i = 0; i < results.resident.size(); ++i) {
        auto const &s = results.resident[i];
        if (i)
            out << ',';
        out << '[' << s.frames << ',' << s.submitWait << ',';
        if (s.device)
            out << *s.device;
        else
            out << "null";
        out << ']';
    }
    out << "],\n\"pipeline\":{\"depth\":" << results.depth
        << ",\"peak_outstanding_results\":" << results.peakOutstanding
        << ",\"frames\":" << results.pipelineLatency.size()
        << ",\"host_output_bytes\":" << results.hostOutputBytes
        << ",\"idle_staging_cache_limit_bytes\":"
        << (options.backend == "vulkan" ? 64u * 1024u * 1024u : 0u)
        << ",\"latency_scope\":\"pack_in_free_slot_to_host_output; excludes_admission_wait\""
        << ",\"wall_scope\":\"all_frames_pack_submit_wait_unpack_and_cleanup_including_fill_"
           "drain\"";
    double wall = 0;
    for (auto value : results.pipelineRounds)
        wall += value;
    out << ",\"wall_ms\":" << wall << ",\"frames_per_second\":";
    if (wall > 0)
        out << static_cast<double>(results.pipelineLatency.size()) * 1000.0 / wall;
    else
        out << "null";
    out << ",\"latency\":";
    summary(out, results.pipelineLatency);
    out << ",\"device_commands\":";
    summary(out, results.pipelineDevice);
    auto array = [&](auto const &values) {
        out << '[';
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i)
                out << ',';
            out << values[i];
        }
        out << ']';
    };
    out << ",\"round_ms\":";
    array(results.pipelineRounds);
    out << ",\"latency_ms\":";
    array(results.pipelineLatency);
    out << ",\"device_commands_ms\":";
    array(results.pipelineDevice);
    out << ",\"counter_columns\":[\"submission_slots_created\",\"descriptor_pools_created\","
           "\"staging_buffers_created\",\"readback_vectors_created\"]";
    out << ",\"counters_after_warmup\":";
    array(results.countsBefore);
    out << ",\"counters_after_measurement\":";
    array(results.countsAfter);
    out << "}\n}\n";
    out.close();
    if (!out)
        throw std::runtime_error{"cannot write report: " + options.report.string()};
}
} // namespace

int main(int argc, char **argv) {
    try {
        auto options = parse(argc, argv);
        im::FrameDesc desc{options.width, options.height, im::uyvy422(), {}, im::Scan::Progressive};
        desc.color.horizontal = im::ChromaLocation::Cosited;
        desc.color.vertical = im::ChromaLocation::Cosited;
        auto inputs = load(options, desc);
        std::unique_ptr<co::Device> device;
        std::uint64_t errors = 0, warnings = 0;
#ifdef LUTILS_BENCHMARK_VULKAN
        auto validation = std::make_shared<co::ValidationReport>();
        auto statistics = std::make_shared<co::VulkanStatistics>();
        if (options.backend == "vulkan") {
            co::VulkanOptions config;
            config.requireHardware = options.hardware;
            config.enableValidation = options.validation;
            config.enableTimestamps = options.timestamps;
            config.validationReport = validation;
            config.preferHostCached = options.hostCached;
            config.deviceLocal = options.deviceLocal;
            config.reuseSubmissionResources = options.reuse;
            if (!options.readbackCache)
                config.maxCachedReadbackBytes = 0;
            config.maxInFlight = options.pipelineDepth
                                     ? static_cast<std::uint32_t>(std::min(
                                           std::size_t{options.pipelineDepth}, inputs.size()))
                                     : 4;
            config.statistics = statistics;
            device = take(co::createVulkanDevice(config));
        }
#endif
        if (options.backend == "cpu")
            device = take(co::createCpuDevice({options.cpuWorkers, 4096}));
        if (!device)
            throw std::runtime_error{"Vulkan backend is unavailable in this build"};
        std::cout << "device=" << device->info().name << std::endl;
        auto counters = [&]() -> std::array<std::uint64_t, 4> {
#ifdef LUTILS_BENCHMARK_VULKAN
            return {statistics->submissionSlotsCreated.load(),
                    statistics->descriptorPoolsCreated.load(),
                    statistics->stagingBuffersCreated.load(),
                    statistics->readbackVectorsCreated.load()};
#else
            return {};
#endif
        };
        auto results = options.pipelineDepth ? runPipeline(*device, options, desc, inputs, counters)
                                             : run(*device, options, desc, inputs);
        device.reset();
#ifdef LUTILS_BENCHMARK_VULKAN
        errors = validation->errors.load();
        warnings = validation->warnings.load();
#endif
        report(options, results, errors, warnings);
        if (errors)
            throw std::runtime_error{"Vulkan validation errors; see report and stderr"};
        std::cout << "PASS frames=" << inputs.size() << " samples=" << results.single.size()
                  << " pipeline_frames=" << results.pipelineLatency.size()
                  << " resident_batches=" << results.resident.size()
                  << " validation_errors=" << errors << " validation_warnings=" << warnings
                  << " report=" << options.report << '\n';
    } catch (std::exception const &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
