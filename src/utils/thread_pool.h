#ifndef MARTINGALE_UTILS_THREAD_POOL_H
#define MARTINGALE_UTILS_THREAD_POOL_H

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace martingale {

/// Fixed-size thread pool with a central task queue and condition-variable synchronization.
class ThreadPool {
public:
    /// Creates a pool with n_workers worker threads.
    explicit ThreadPool(int n_workers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// Submits a callable to the task queue for asynchronous execution.
    template<typename F>
    void submit(F&& task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace_back(std::forward<F>(task));
        }
        cv_.notify_one();
    }

    /// Blocks until all submitted tasks have finished executing.
    void wait();

    /// Returns the number of worker threads.
    int worker_count() const { return n_workers_; }

private:
    int n_workers_;
    std::vector<std::thread> workers_;
    std::deque<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    int active_ = 0;

    void worker_loop();
};

} // namespace martingale

#endif // MARTINGALE_UTILS_THREAD_POOL_H
