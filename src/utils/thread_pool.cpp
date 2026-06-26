#include "thread_pool.h"

namespace martingale {

ThreadPool::ThreadPool(int n_workers)
    : n_workers_(n_workers > 0 ? n_workers : static_cast<int>(std::thread::hardware_concurrency()))
{
    workers_.reserve(static_cast<size_t>(n_workers_));
    for (int i = 0; i < n_workers_; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) {
            w.join();
        }
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty()) {
                return;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
            ++active_;
        }
        task();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_;
        }
        cv_.notify_all();
    }
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return tasks_.empty() && active_ == 0; });
}

} // namespace martingale
