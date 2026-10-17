// thread_pool.hpp - a small header-only worker thread pool.
//
// Part of Parallel Numerics Bench. Standard textbook design (condition
// variable + mutex + task queue), written from scratch for this project:
//
//   * a fixed set of worker threads is started in the constructor;
//   * enqueue() wraps any callable in a std::packaged_task and returns a
//     std::future for its result (exceptions included);
//   * workers block on a condition_variable until a task is available or the
//     pool is being destroyed;
//   * the destructor stops accepting new work, DRAINS the remaining queue
//     (so futures already handed out stay valid) and joins all workers.
//
// Semantics worth knowing (also documented in docs/api.md):
//   * enqueue() throws std::runtime_error after the pool began shutting down;
//   * task execution order between workers is unspecified by design - the
//     workloads in this project are written so that this never changes
//     results (see docs/model.md, "Determinism contract");
//   * queues/tasks are protected by one mutex; this pool targets coarse
//     tasks (milliseconds), not micro-tasks.
//
// License: MIT (see LICENSE).

#ifndef PNB_THREAD_POOL_HPP
#define PNB_THREAD_POOL_HPP

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace pnb {

class ThreadPool {
public:
    // Creates `threads` worker threads. Throws std::invalid_argument for 0.
    explicit ThreadPool(std::size_t threads) {
        if (threads == 0) {
            throw std::invalid_argument("ThreadPool: thread count must be >= 1");
        }
        workers_.reserve(threads);
        for (std::size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    // Drains the queue and joins all workers.
    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Submits f(args...) and returns a future to its result. The future
    // carries the returned value or the exception f threw.
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using result_type =
            std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        // std::function requires copyable callables; std::packaged_task is
        // move-only, so it is held through a shared_ptr (the classic shim).
        auto task = std::make_shared<std::packaged_task<result_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<result_type> fut = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_) {
                throw std::runtime_error(
                    "ThreadPool::enqueue called on a pool that is shutting down");
            }
            queue_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    // Number of worker threads (fixed at construction).
    std::size_t worker_count() const noexcept { return workers_.size(); }

    // Approximate snapshot of tasks waiting right now (does not count tasks
    // currently executing). Diagnostic use only.
    std::size_t queued_tasks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    void worker_loop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) {
                    // stopping_ && queue drained -> exit; the queue is always
                    // drained before workers exit, so handed-out futures
                    // always become ready.
                    return;
                }
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_ = false;
};

}  // namespace pnb

#endif  // PNB_THREAD_POOL_HPP
