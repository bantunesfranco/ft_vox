#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <algorithm>
#include <atomic>
#include <deque>
#include <stdexcept>
#include <vector>
#include <thread>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <future>
#include <type_traits>

class ThreadPool {
private:
    struct PerThreadQueue {
        std::deque<std::function<void()>> tasks;
        std::mutex lock;
    };

    struct ThreadLocalMetrics {
        std::atomic<uint64_t> tasksProcessed{0};
        std::atomic<uint64_t> stealsAttempted{0};
        std::atomic<uint64_t> stealsSucceeded{0};
    };

public:
    explicit ThreadPool(const size_t numThreads):
        perThreadQueues(numThreads), threadMetrics(numThreads),
        roundRobinIdx(0), stop(false), taskCount(0)
    {

        workers.reserve(numThreads);

        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this, i] {
                workLoop(i);
            });
        }
    }

    ~ThreadPool() {
        shutdown();
    }

    // Fire-and-forget: preferred for chunk generation
    template<class F, class... Args>
    void enqueueFireAndForget(F&& f, Args&&... args)
    {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

        const size_t idx = roundRobinIdx.fetch_add(1, std::memory_order_relaxed) % perThreadQueues.size();

        {
            std::unique_lock lock(perThreadQueues[idx].lock);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            perThreadQueues[idx].tasks.emplace_back(std::move(task));
            taskCount.fetch_add(1, std::memory_order_relaxed);
        }

        globalCondition.notify_one();
    }

    // With return value: for tasks needing futures
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F(Args...)>>
    {
        using return_type = std::invoke_result_t<F(Args...)>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        const size_t idx = roundRobinIdx.fetch_add(1, std::memory_order_relaxed) % perThreadQueues.size();

        {
            std::unique_lock<std::mutex> lock(perThreadQueues[idx].lock);
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            perThreadQueues[idx].tasks.emplace_back([task]() { (*task)(); });
            taskCount.fetch_add(1, std::memory_order_relaxed);
        }

        globalCondition.notify_one();
        return res;
    }

    [[nodiscard]] size_t getPendingTasks() const { return taskCount.load(std::memory_order_relaxed); }

    [[nodiscard]] size_t getWorkerCount() const { return workers.size(); }

    struct PoolMetrics {
        size_t tasksProcessed = 0;
        size_t stealsAttempted = 0;
        size_t stealsSucceeded = 0;
        double stealSuccessRate = 0.0;
    };

    [[nodiscard]] PoolMetrics getMetrics() const {
        PoolMetrics metrics;
        for (const auto& m : threadMetrics) {
            metrics.tasksProcessed += m.tasksProcessed.load();
            metrics.stealsAttempted += m.stealsAttempted.load();
            metrics.stealsSucceeded += m.stealsSucceeded.load();
        }

        if (metrics.stealsAttempted > 0)
            metrics.stealSuccessRate = 100.0 * metrics.stealsSucceeded / metrics.stealsAttempted;

        return metrics;
    }

    void resetMetrics() {
        for (auto& m : threadMetrics) {
            m.tasksProcessed = 0;
            m.stealsAttempted = 0;
            m.stealsSucceeded = 0;
        }
    }

    void shutdown() {
        {
            std::unique_lock lock(globalMutex);
            stop = true;
        }
        globalCondition.notify_all();
        for (std::thread& worker : workers)
            if (worker.joinable())
                worker.join();
    }

private:
    std::vector<std::thread> workers;
    std::vector<PerThreadQueue> perThreadQueues;
    std::vector<ThreadLocalMetrics> threadMetrics;
    std::atomic<size_t> roundRobinIdx;
    std::atomic<bool> stop;
    std::atomic<size_t> taskCount;
    std::mutex globalMutex;
    std::condition_variable globalCondition;

    void workLoop(size_t threadIdx) {
        while (true) {
            std::function<void()> task;

            // First: try own queue (no lock needed if using atomic indices)
            {
                std::unique_lock lock(perThreadQueues[threadIdx].lock);
                if (!perThreadQueues[threadIdx].tasks.empty()) {
                    task = std::move(perThreadQueues[threadIdx].tasks.front());
                    perThreadQueues[threadIdx].tasks.pop_front();
                    taskCount.fetch_sub(1, std::memory_order_relaxed);
                    threadMetrics[threadIdx].tasksProcessed.fetch_add(1, std::memory_order_relaxed);
                }
            }

            // Second: work stealing from other queues
            if (!task) {
                bool foundWork = false;
                for (size_t i = 1; i < perThreadQueues.size(); ++i) {
                    const size_t stealIdx = (threadIdx + i) % perThreadQueues.size();

                    threadMetrics[threadIdx].stealsAttempted.fetch_add(1, std::memory_order_relaxed);

                    std::unique_lock lock(perThreadQueues[stealIdx].lock);
                    if (!perThreadQueues[stealIdx].tasks.empty()) {
                        task = std::move(perThreadQueues[stealIdx].tasks.back());  // Steal from back
                        perThreadQueues[stealIdx].tasks.pop_back();
                        taskCount.fetch_sub(1, std::memory_order_relaxed);
                        threadMetrics[threadIdx].stealsSucceeded.fetch_add(1, std::memory_order_relaxed);
                        threadMetrics[threadIdx].tasksProcessed.fetch_add(1, std::memory_order_relaxed);
                        foundWork = true;
                        break;
                    }
                }

                // Third: if still no work, wait
                if (!foundWork) {
                    std::unique_lock lock(globalMutex);
                    globalCondition.wait_for(lock, std::chrono::milliseconds(1), [this, threadIdx] {
                        return stop || !perThreadQueues[threadIdx].tasks.empty() || hasAnyWork();
                    });

                    if (stop && !hasAnyWork()) return;

                    continue;
                }
            }

            if (task) task();
        }
    }

    [[nodiscard]] bool hasAnyWork() const {
        return std::ranges::any_of(perThreadQueues, [](const auto& q) {
            return !q.tasks.empty();
        });
    }
};

#endif