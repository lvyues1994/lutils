#include <algorithm>
#include <condition_variable>
#include <exception>
#include <lutils/compute/Runtime.hpp>
#include <lutils/compute/Workgroup.hpp>
#include <thread>

namespace lutils::compute {
namespace {
struct ContextScope {
    kernel::detail::ExecutionContext *previous;
    explicit ContextScope(kernel::detail::ExecutionContext &current)
        : previous(std::exchange(kernel::detail::executionContext, &current)) {}
    ~ContextScope() { kernel::detail::executionContext = previous; }
};
struct ParallelContext final : kernel::detail::ExecutionContext {
    std::mutex atomic;
    void synchronize() override { throw std::logic_error("barrier requires a workgroup"); }
    std::mutex &atomicMutex() override { return atomic; }
};
struct Cancelled {};
struct GroupContext final : kernel::detail::ExecutionContext {
    std::mutex mutex, atomic;
    std::condition_variable changed;
    std::size_t lanes, arrived = 0, generation = 0;
    bool cancelled = false;
    std::exception_ptr error;
    explicit GroupContext(std::size_t count) : lanes(count) {}
    std::mutex &atomicMutex() override { return atomic; }
    void synchronize() override {
        std::unique_lock<std::mutex> lock(mutex);
        if (cancelled)
            throw Cancelled{};
        auto phase = generation;
        if (++arrived == lanes) {
            arrived = 0;
            ++generation;
            changed.notify_all();
        } else {
            changed.wait(lock, [&] { return cancelled || generation != phase; });
        }
        if (cancelled)
            throw Cancelled{};
    }
    void cancel(std::exception_ptr e) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!error)
            error = std::move(e);
        cancelled = true;
        changed.notify_all();
    }
};
struct CpuExecutionImpl final : CpuExecution {
    CpuOptions options;
    ParallelContext context;
    std::mutex mutex;
    std::condition_variable started, finished;
    std::vector<std::thread> workers;
    std::function<void(std::size_t, std::size_t)> job;
    std::size_t count = 0, generation = 0, outstanding = 0;
    bool stopping = false;
    std::exception_ptr error;
    explicit CpuExecutionImpl(CpuOptions o) : options(o) {
        if (!options.workerCount)
            options.workerCount =
                std::min<std::size_t>(8, std::max(1u, std::thread::hardware_concurrency()));
        try {
            for (std::size_t i = 1; i < options.workerCount; ++i)
                workers.emplace_back([this, i] { worker(i); });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~CpuExecutionImpl() override { stop(); }
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        started.notify_all();
        for (auto &worker : workers)
            if (worker.joinable())
                worker.join();
    }
    void run(std::size_t index) {
        ContextScope scope(context);
        try {
            // Divide without overflowing count * index.
            auto chunk = count / options.workerCount, remainder = count % options.workerCount;
            auto begin = chunk * index + std::min(index, remainder);
            auto end = begin + chunk + (index < remainder ? 1u : 0u);
            if (begin != end)
                job(begin, end);
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex);
            if (!error)
                error = std::current_exception();
        }
    }
    void worker(std::size_t index) {
        std::size_t seen = 0;
        std::unique_lock<std::mutex> lock(mutex);
        while (true) {
            started.wait(lock, [&] { return stopping || generation != seen; });
            if (stopping)
                return;
            seen = generation;
            lock.unlock();
            run(index);
            lock.lock();
            if (--outstanding == 0)
                finished.notify_one();
        }
    }
    void parallelFor(std::size_t n,
                     std::function<void(std::size_t, std::size_t)> const &f) override {
        if (workers.empty() || n < options.parallelThreshold) {
            ContextScope scope(context);
            f(0, n);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            job = f;
            count = n;
            error = nullptr;
            outstanding = workers.size();
            ++generation;
        }
        started.notify_all();
        run(0);
        std::unique_lock<std::mutex> lock(mutex);
        finished.wait(lock, [&] { return outstanding == 0; });
        job = {};
        if (error)
            std::rethrow_exception(error);
    }
    void workgroup(std::size_t lanes, std::function<void(std::size_t)> const &f) override {
        if (!lanes || lanes > 1024)
            throw std::invalid_argument("CPU workgroups support 1 to 1024 lanes");
        GroupContext group(lanes);
        std::vector<std::thread> threads;
        auto runLane = [&](std::size_t lane) {
            ContextScope scope(group);
            try {
                f(lane);
            } catch (Cancelled const &) {
                // The original exception was saved before waking this lane.
            } catch (...) {
                group.cancel(std::current_exception());
            }
        };
        try {
            threads.reserve(lanes - 1);
            for (std::size_t lane = 1; lane < lanes; ++lane)
                threads.emplace_back(runLane, lane);
            runLane(0);
        } catch (...) {
            group.cancel(std::current_exception());
        }
        for (auto &thread : threads)
            thread.join();
        if (group.error)
            std::rethrow_exception(group.error);
    }
};
} // namespace
Result<std::unique_ptr<CpuExecution>> createCpuExecution(CpuOptions options) {
    if (options.workerCount > 256)
        return Error{ErrorCode::InvalidArgument, "CPU worker count exceeds 256"};
    try {
        return std::unique_ptr<CpuExecution>{std::make_unique<CpuExecutionImpl>(options)};
    } catch (std::exception const &e) {
        return Error{ErrorCode::Device, e.what()};
    }
}
} // namespace lutils::compute
