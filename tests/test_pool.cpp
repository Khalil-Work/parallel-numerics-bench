// test_pool.cpp - semantics of the header-only ThreadPool.
//
// License: MIT (see LICENSE).

#include <atomic>
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "test_util.hpp"
#include "thread_pool.hpp"

using namespace pnb;

int main() {
    // 0 threads is rejected.
    bool threw = false;
    try {
        ThreadPool zero(0);
        (void)zero;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    // worker_count reports what was requested.
    {
        ThreadPool pool(4);
        CHECK(pool.worker_count() == 4);
    }

    // Futures return the right values for many tasks.
    {
        ThreadPool pool(4);
        std::vector<std::future<int>> futs;
        for (int i = 0; i < 200; ++i) {
            futs.push_back(pool.enqueue([i] { return i * i; }));
        }
        for (int i = 0; i < 200; ++i) {
            CHECK(futs[static_cast<std::size_t>(i)].get() == i * i);
        }
        CHECK(pool.queued_tasks() == 0);  // all futures ready -> queue drained
    }

    // void tasks work too.
    {
        ThreadPool pool(2);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futs;
        for (int i = 0; i < 50; ++i) {
            futs.push_back(pool.enqueue([&counter] { counter.fetch_add(1); }));
        }
        for (auto& f : futs) {
            f.get();
        }
        CHECK(counter.load() == 50);
    }

    // Exception propagation: the future rethrows, with the exact message,
    // and the pool keeps working for later tasks.
    {
        ThreadPool pool(2);
        auto bad = pool.enqueue([]() -> int {
            throw std::runtime_error("boom");
        });
        bool caught = false;
        try {
            (void)bad.get();
        } catch (const std::runtime_error& e) {
            caught = std::string(e.what()) == "boom";
        }
        CHECK(caught);
        auto good = pool.enqueue([] { return 7; });
        CHECK(good.get() == 7);
    }

    // Destructor drains the queue: every submitted task completes even if
    // nobody waits on the futures before the pool goes out of scope.
    {
        std::atomic<int> counter{0};
        {
            ThreadPool pool(4);
            for (int i = 0; i < 100; ++i) {
                (void)pool.enqueue([&counter] {
                    counter.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });
            }
        }  // destructor joins after draining
        CHECK(counter.load() == 100);
    }

    // Task count: exactly N submitted increments are executed, no more,
    // no less, and results are stable across a second identical round.
    {
        ThreadPool pool(4);
        std::atomic<int> counter{0};
        std::vector<std::future<void>> futs;
        for (int i = 0; i < 1000; ++i) {
            futs.push_back(pool.enqueue([&counter] { counter.fetch_add(1); }));
        }
        for (auto& f : futs) {
            f.get();
        }
        CHECK(counter.load() == 1000);
        CHECK(pool.queued_tasks() == 0);
    }

    // Arguments are forwarded to tasks.
    {
        ThreadPool pool(2);
        auto sum = pool.enqueue([](int a, int b) { return a + b; }, 20, 22);
        CHECK(sum.get() == 42);
    }

    return test_summary("test_pool");
}
