#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <lutils/image/SimdConversion.hpp>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif
namespace benchmark {
namespace im = lutils::image;
using Clock = std::chrono::steady_clock;
inline void check(bool ok) {
    if (!ok)
        throw std::runtime_error("benchmark validation failed");
}
template <class T> T take(lutils::Result<T> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
    return std::move(r).value();
}
inline void take(lutils::Result<void> r) {
    if (!r)
        throw std::runtime_error(r.error().message);
}
inline int cpu() {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}
struct Sample {
    std::size_t iterations;
    std::int64_t ns;
    int before;
    int after;
};
inline volatile std::uint64_t consumed = 0;
template <class Run, class Validate, class VerifyFinal>
void measure(std::string const &fields, Run run, Validate validate, VerifyFinal verifyFinal, bool smoke,
             Clock::time_point deadline) {
    validate();
    auto warmStart = Clock::now();
    std::size_t warmups = 0;
    do {
        if (Clock::now() >= deadline)
            throw std::runtime_error("benchmark exceeded 45 second budget during warmup");
        consumed = run();
        ++warmups;
    } while (!smoke && Clock::now() - warmStart < std::chrono::milliseconds(50));
    auto warmNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - warmStart).count();
    std::uint64_t lastResult = 0;
    auto timed = [&](std::size_t n) {
        int before = cpu();
        auto start = Clock::now();
        std::uint64_t value = 0, result = 0;
        for (std::size_t i = 0; i < n; ++i) {
            result = run();
            value ^= result;
        }
        auto end = Clock::now();
        lastResult = result;
        consumed = value;
        return Sample{n, std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count(),
                      before, cpu()};
    };
    std::size_t iterations = 1;
    if (!smoke) {
        auto probe = timed(1);
        iterations = std::min(
            std::size_t{100000},
            std::max(std::size_t{1},
                     static_cast<std::size_t>(2000000 / std::max(std::int64_t{1}, probe.ns))));
    }
    std::vector<Sample> samples;
    for (int i = 0; i < (smoke ? 1 : 9); ++i) {
        if (Clock::now() > deadline)
            throw std::runtime_error("benchmark exceeded 45 second budget");
        samples.push_back(timed(iterations));
    }
    verifyFinal(lastResult);
    std::vector<double> times;
    for (auto const &sample : samples)
        times.push_back(double(sample.ns) / double(sample.iterations));
    std::sort(times.begin(), times.end());
    double median = times[times.size() / 2];
    std::vector<double> deviations;
    for (auto t : times)
        deviations.push_back(std::abs(t - median));
    std::sort(deviations.begin(), deviations.end());
    std::cout << "{\"type\":\"case\"," << fields << ",\"validated\":true,\"warmups\":" << warmups
              << ",\"warmup_ns\":" << warmNs << ",\"median_ns\":" << median
              << ",\"min_ns\":" << times.front() << ",\"max_ns\":" << times.back()
              << ",\"mad_ns\":" << deviations[deviations.size() / 2] << ",\"samples\":[";
    bool first = true;
    for (auto const &sample : samples) {
        if (!first)
            std::cout << ',';
        first = false;
        std::cout << "{\"iterations\":" << sample.iterations << ",\"elapsed_ns\":" << sample.ns
                  << ",\"cpu_before\":" << sample.before << ",\"cpu_after\":" << sample.after
                  << '}';
    }
    std::cout << "]}\n";
}
inline void imageReference(im::ConstFrameView const &src, im::FrameView const &dst, bool rgba) {
    auto integer = [](std::byte b) { return std::to_integer<int>(b); };
    auto q = [](int value) { return std::byte(std::clamp((value + 128) / 256, 0, 255)); };
    for (std::size_t y = 0; y < src.desc.height; ++y) {
        auto const *a = im::row(src.planes[0], y);
        auto *out = im::row(dst.planes[0], y);
        if (rgba) {
            auto const *uv = im::row(src.planes[1], y / 2);
            for (std::size_t x = 0; x < src.desc.width; ++x) {
                int c = integer(a[x]) - 16, d = integer(uv[2 * (x / 2)]) - 128,
                    e = integer(uv[2 * (x / 2) + 1]) - 128;
                out[4 * x] = q(298 * c + 409 * e);
                out[4 * x + 1] = q(298 * c - 100 * d - 208 * e);
                out[4 * x + 2] = q(298 * c + 516 * d);
                out[4 * x + 3] = std::byte{255};
            }
        } else {
            for (std::size_t x = 0; x < src.desc.width; ++x)
                out[x] = a[2 * x + 1];
            if (y % 2 == 0) {
                auto const *bottom =
                    im::row(src.planes[0], std::min(y + 1, std::size_t(src.desc.height) - 1));
                auto *uv = im::row(dst.planes[1], y / 2);
                for (std::size_t x = 0; x < src.desc.width; ++x)
                    uv[x] = std::byte((integer(a[2 * x]) + integer(bottom[2 * x]) + 1) / 2);
            }
        }
    }
}
} // namespace benchmark
