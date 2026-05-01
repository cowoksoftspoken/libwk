// Copyright 2026 Inggrit Setya Budi
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "threading.h"

namespace wk {

ThreadPool::ThreadPool(uint32_t num_threads) {
    if (num_threads == 0) {
        num_threads = default_thread_count();
    }

    for (uint32_t i = 0; i < num_threads; i++) {
        workers_.emplace_back([this](std::stop_token stoken) {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this, &stoken] {
                        return stop_ || !tasks_.empty() || stoken.stop_requested();
                    });

                    if ((stop_ || stoken.stop_requested()) && tasks_.empty()) {
                        return;
                    }

                    if (tasks_.empty()) continue;

                    task = std::move(tasks_.front());
                    tasks_.pop();
                    active_tasks_++;
                }

                task();

                active_tasks_--;
                done_cv_.notify_all();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();

    for (auto& w : workers_) {
        w.request_stop();
        if (w.joinable()) w.join();
    }
}

void ThreadPool::wait_all() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_cv_.wait(lock, [this] {
        return tasks_.empty() && active_tasks_ == 0;
    });
}

uint32_t ThreadPool::default_thread_count() {
    auto n = std::thread::hardware_concurrency();
    return n > 0 ? n : 4;
}

}
