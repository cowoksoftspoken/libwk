#pragma once


#include "common.h"
#include <functional>
#include <future>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

namespace wk {

class ThreadPool {
public:
    explicit ThreadPool(uint32_t num_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;


    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<decltype(f(args...))> {
        using ReturnType = decltype(f(args...));

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto future = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_) {
                throw std::runtime_error("ThreadPool is stopped");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();

        return future;
    }


    [[nodiscard]] uint32_t num_threads() const {
        return static_cast<uint32_t>(workers_.size());
    }


    void wait_all();


    static uint32_t default_thread_count();

private:
    std::vector<std::jthread>          workers_;
    std::queue<std::function<void()>>  tasks_;
    std::mutex                         mutex_;
    std::condition_variable            cv_;
    std::condition_variable            done_cv_;
    std::atomic<bool>                  stop_{false};
    std::atomic<int>                   active_tasks_{0};
};

}
