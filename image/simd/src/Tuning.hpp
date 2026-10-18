#pragma once
#include <algorithm>
#include <cmath>
#include <lutils/image/CpuTuning.hpp>
#include <optional>

namespace lutils::image::simd_detail {
struct PairSamples {
    std::vector<double> baseline;
    std::vector<double> candidate;
};
inline double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return (values[(values.size() - 1) / 2] + values[values.size() / 2]) / 2;
}
// A conservative noise margin, not a statistical confidence interval.
inline std::optional<double> stableGain(PairSamples const &samples) {
    if (samples.baseline.size() != 5 || samples.candidate.size() != 5)
        return {};
    std::vector<double> ratios;
    unsigned wins = 0;
    for (std::size_t i = 0; i < 5; ++i) {
        auto a = samples.baseline[i], b = samples.candidate[i];
        if (!(a > 0 && b > 0) || !std::isfinite(a) || !std::isfinite(b))
            return {};
        auto ratio = b / a;
        if (!std::isfinite(ratio))
            return {};
        ratios.push_back(ratio);
        wins += b < a;
    }
    auto center = median(ratios);
    for (auto &r : ratios)
        r = std::abs(r - center);
    auto score = center + 2 * median(ratios);
    return wins >= 4 && std::isfinite(score) && score <= 0.95 ? std::optional<double>{score}
                                                              : std::nullopt;
}
inline bool confirmed(PairSamples const &screen, PairSamples const &confirmation) {
    return stableGain(screen).has_value() && stableGain(confirmation).has_value();
}
// Append only complete ABBA/BAAB blocks. nullopt marks the deadline.
template <class Measure> Result<bool> collectPairs(PairSamples &out, Measure measure) {
    for (std::size_t round = 0; round < 5; ++round) {
        double sums[2]{};
        for (unsigned step = 0; step < 4; ++step) {
            auto candidate = (step == 1 || step == 2) != (round % 2 != 0);
            auto value = measure(candidate);
            if (!value)
                return value.error();
            if (!value.value())
                return false;
            sums[candidate ? 1 : 0] += *value.value();
        }
        out.baseline.push_back(sums[0] / 2);
        out.candidate.push_back(sums[1] / 2);
    }
    return true;
}
struct Calibration {
    std::size_t selected;
    std::vector<std::vector<double>> samples;
    CpuTuningOutcome outcome;
};
using TuningClock = std::chrono::steady_clock;
struct TuningBudget {
    TuningClock::time_point warmDeadline, screenDeadline, deadline;
    std::chrono::nanoseconds warmMinimum;
    double batchNanoseconds;
};
inline TuningBudget tuningBudget(TuningClock::time_point start, std::chrono::milliseconds budget,
                                 std::size_t alternatives) {
    using namespace std::chrono;
    auto total = duration_cast<nanoseconds>(budget);
    auto warmWindow = std::min(duration_cast<nanoseconds>(milliseconds{20}), total / 4);
    // Screening and confirmation need at most 40 * alternatives batches.
    auto batch = std::min(duration_cast<nanoseconds>(microseconds{200}),
                          total / static_cast<nanoseconds::rep>(80 * alternatives));
    return {start + warmWindow, start + total * 3 / 4, start + total,
            std::min(duration_cast<nanoseconds>(milliseconds{5}), total / 8),
            static_cast<double>(batch.count())};
}
inline std::size_t batchIterations(double cost, TuningBudget const &budget) {
    return static_cast<std::size_t>(
        std::clamp(budget.batchNanoseconds / std::max(1.0, cost), 1.0, 256.0));
}
inline CpuTuningOutcome incomplete(TuningClock::time_point now, TuningBudget const &budget) {
    return now >= budget.deadline ? CpuTuningOutcome::BudgetExhausted
                                  : CpuTuningOutcome::InsufficientBudget;
}
struct Warmup {
    std::optional<double> cost;
    CpuTuningOutcome outcome;
};
template <class Now, class Measure>
Result<Warmup> warmup(TuningBudget const &budget, Now now, Measure measure) {
    auto start = now();
    std::vector<double> samples;
    double cost = 1;
    bool observedInstability = false;
    do {
        auto value =
            measure(0, samples.empty() ? 1 : batchIterations(cost, budget), budget.warmDeadline);
        if (!value)
            return value.error();
        if (!value.value())
            break;
        cost = *value.value();
        samples.push_back(cost);
        if (samples.size() >= 5 && now() - start >= budget.warmMinimum) {
            auto first = samples.end() - 5;
            auto limits = std::minmax_element(first, samples.end());
            if (*limits.first > 0 && *limits.second <= *limits.first * 1.05)
                return Warmup{median(std::vector<double>(first, samples.end())),
                              CpuTuningOutcome::NoGain};
            observedInstability = true;
        }
    } while (now() < budget.warmDeadline);
    auto outcome = incomplete(now(), budget);
    if (outcome != CpuTuningOutcome::BudgetExhausted && observedInstability)
        outcome = CpuTuningOutcome::WarmupUnstable;
    return Warmup{{}, outcome};
}
inline void recordPairs(std::vector<std::vector<double>> &timings, std::size_t index,
                        PairSamples const &samples) {
    timings[0].insert(timings[0].end(), samples.baseline.begin(), samples.baseline.end());
    timings[index].insert(timings[index].end(), samples.candidate.begin(), samples.candidate.end());
}
struct Selection {
    std::size_t index = 0;
    std::size_t iterations = 1;
    PairSamples screen;
    CpuTuningOutcome outcome = CpuTuningOutcome::NoGain;
};
template <class Now, class Measure>
Result<Selection> screenCandidates(std::vector<std::vector<double>> &timings, double base,
                                   TuningBudget const &budget, Now now, Measure measure) {
    Selection winner;
    double best = 1;
    for (std::size_t i = 1; i < timings.size(); ++i) {
        auto probe = measure(i, 1, budget.screenDeadline);
        if (!probe)
            return probe.error();
        if (!probe.value())
            return Selection{0, 1, {}, incomplete(now(), budget)};
        timings[i].push_back(*probe.value());
        // A cold probe can miss an opportunity; it cannot select a slower path.
        if (*probe.value() >= base * 0.98)
            continue;
        auto count = batchIterations(std::max(base, *probe.value()), budget);
        PairSamples samples;
        auto complete = collectPairs(samples, [&](bool candidate) {
            return measure(candidate ? i : 0, count, budget.screenDeadline);
        });
        if (!complete)
            return complete.error();
        recordPairs(timings, i, samples);
        if (!complete.value())
            return Selection{0, 1, {}, incomplete(now(), budget)};
        auto score = stableGain(samples);
        if (score && *score < best) {
            best = *score;
            winner = {i, count, std::move(samples), CpuTuningOutcome::Selected};
        }
    }
    return winner;
}
template <class Now, class Measure>
Result<CpuTuningOutcome> confirmSelection(Selection const &winner,
                                          std::vector<std::vector<double>> &timings,
                                          TuningBudget const &budget, Now now, Measure measure) {
    PairSamples samples;
    auto complete = collectPairs(samples, [&](bool candidate) {
        return measure(candidate ? winner.index : 0, winner.iterations, budget.deadline);
    });
    if (!complete)
        return complete.error();
    recordPairs(timings, winner.index, samples);
    if (!complete.value())
        return incomplete(now(), budget);
    return confirmed(winner.screen, samples) ? CpuTuningOutcome::Selected
                                             : CpuTuningOutcome::NoGain;
}
// Clock and execution are injected so budget and fallback branches are
// testable.
template <class Now, class Run>
Result<Calibration> calibrate(std::size_t planCount, std::chrono::milliseconds duration,
                              TuningClock::time_point start, Now now, Run run) {
    Calibration result{0, std::vector<std::vector<double>>(planCount),
                       CpuTuningOutcome::NoAlternative};
    if (planCount == 1)
        return result;
    auto budget = tuningBudget(start, duration, planCount - 1);
    auto measure = [&](std::size_t index, std::size_t count,
                       TuningClock::time_point limit) -> Result<std::optional<double>> {
        auto before = now();
        for (std::size_t n = 0; n < count; ++n) {
            if (now() >= limit)
                return std::optional<double>{};
            auto executed = run(index);
            if (!executed)
                return executed.error();
        }
        return std::optional<double>{
            std::chrono::duration<double, std::nano>(now() - before).count() / double(count)};
    };
    auto warm = warmup(budget, now, measure);
    if (!warm)
        return warm.error();
    result.outcome = warm.value().outcome;
    if (!warm.value().cost)
        return result;
    auto winner = screenCandidates(result.samples, *warm.value().cost, budget, now, measure);
    if (!winner)
        return winner.error();
    result.outcome = winner.value().outcome;
    if (!winner.value().index)
        return result;
    auto confirmation = confirmSelection(winner.value(), result.samples, budget, now, measure);
    if (!confirmation)
        return confirmation.error();
    result.outcome = confirmation.value();
    if (result.outcome == CpuTuningOutcome::Selected)
        result.selected = winner.value().index;
    return result;
}
} // namespace lutils::image::simd_detail
