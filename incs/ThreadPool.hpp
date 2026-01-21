#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <vector>
#include <thread>
#include <queue>
#include <functional>
#include <condition_variable>
#include <mutex>
#include <future>
#include <atomic>
#include <type_traits>
#include <stdexcept>

class ThreadPool {
public:
    explicit ThreadPool(const size_t threadCount = 8): stop(false), active_tasks(0)
    {
        workers.reserve(threadCount);

        for (size_t i = 0; i < threadCount; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] {
                            return stop || !tasks.empty();
                        });

                        if (stop && tasks.empty())
                            return;

                        task = std::move(tasks.front());
                        tasks.pop();
                        ++active_tasks;
                    }

                    try {
                        task();
                    } catch (...) {
                    }

                    --active_tasks;
                    condition.notify_all();
                }
            });
        }
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }

        condition.notify_all();

        for (std::thread& worker : workers)
            worker.join();
    }

    // Enqueue a task and get a future
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one();
        return res;
    }

    // Wait until all tasks are completed
    void wait()
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        condition.wait(lock, [this] {
            return tasks.empty() && active_tasks == 0;
        });
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queue_mutex;
    std::condition_variable condition;

    bool stop;
    std::atomic<size_t> active_tasks;
};

#endif // THREAD_POOL_HPP
