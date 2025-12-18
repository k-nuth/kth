// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <kth/infrastructure/utility/threadpool.hpp>

using namespace kth;

TEST_CASE("threadpool construction", "[threadpool]") {
    SECTION("default construction uses hardware concurrency") {
        threadpool pool;
        CHECK(pool.size() == std::thread::hardware_concurrency());
    }

    SECTION("explicit thread count") {
        threadpool pool(4);
        CHECK(pool.size() == 4);
    }

    SECTION("zero threads defaults to hardware concurrency") {
        threadpool pool(0);
        CHECK(pool.size() == std::thread::hardware_concurrency());
    }
}

TEST_CASE("threadpool executor", "[threadpool]") {
    threadpool pool(2);

    SECTION("get_executor returns valid executor") {
        auto exec = pool.get_executor();
        // Should be able to post work to it
        std::atomic<bool> executed{false};
        ::asio::post(exec, [&]() {
            executed = true;
        });

        // Give it time to execute
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pool.stop();
        pool.join();

        CHECK(executed.load());
    }

    SECTION("executor returns valid executor") {
        auto exec = pool.executor();
        std::atomic<bool> executed{false};
        ::asio::post(exec, [&]() {
            executed = true;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pool.stop();
        pool.join();

        CHECK(executed.load());
    }
}

TEST_CASE("threadpool work execution", "[threadpool]") {
    threadpool pool(4);

    SECTION("single task execution") {
        std::atomic<int> counter{0};

        ::asio::post(pool.get_executor(), [&]() {
            counter++;
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        pool.stop();
        pool.join();

        CHECK(counter.load() == 1);
    }

    SECTION("multiple tasks execution") {
        std::atomic<int> counter{0};
        int const num_tasks = 100;

        for (int i = 0; i < num_tasks; ++i) {
            ::asio::post(pool.get_executor(), [&]() {
                counter++;
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        pool.stop();
        pool.join();

        CHECK(counter.load() == num_tasks);
    }

    SECTION("concurrent execution") {
        std::atomic<int> max_concurrent{0};
        std::atomic<int> current{0};
        int const num_tasks = 10;

        for (int i = 0; i < num_tasks; ++i) {
            ::asio::post(pool.get_executor(), [&]() {
                int val = ++current;
                // Update max if this is highest concurrent
                int expected = max_concurrent.load();
                while (val > expected && !max_concurrent.compare_exchange_weak(expected, val)) {}

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                --current;
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        pool.stop();
        pool.join();

        // With 4 threads, we should see some concurrency
        CHECK(max_concurrent.load() > 1);
    }
}

TEST_CASE("threadpool stop and join", "[threadpool]") {
    using namespace std::chrono_literals;

    SECTION("join without stop waits for all pending work") {
        threadpool pool(2);
        std::atomic<int> completed{0};

        // Post multiple tasks
        for (int i = 0; i < 5; ++i) {
            ::asio::post(pool.get_executor(), [&]() {
                std::this_thread::sleep_for(10ms);
                ++completed;
            });
        }

        // join() without stop() waits for ALL work to complete
        pool.join();

        CHECK(completed.load() == 5);
    }

    SECTION("stop cancels pending work that has not started") {
        threadpool pool(1);  // Single thread to control execution order
        std::atomic<bool> first_started{false};
        std::atomic<bool> first_done{false};
        std::atomic<bool> second_done{false};

        // First task - takes a while
        ::asio::post(pool.get_executor(), [&]() {
            first_started = true;
            std::this_thread::sleep_for(50ms);
            first_done = true;
        });

        // Second task - should be cancelled by stop()
        ::asio::post(pool.get_executor(), [&]() {
            second_done = true;
        });

        // Wait for first task to start (so second is queued but not running)
        while (!first_started.load()) {
            std::this_thread::sleep_for(1ms);
        }

        // stop() cancels pending work (second task)
        pool.stop();
        pool.join();

        CHECK(first_done.load());       // First task completed (was already running)
        CHECK_FALSE(second_done.load()); // Second task was cancelled (was pending)
    }

    SECTION("stop does not block - requires separate join") {
        threadpool pool(2);
        std::atomic<bool> work_running{false};
        std::atomic<bool> work_done{false};

        ::asio::post(pool.get_executor(), [&]() {
            work_running = true;
            std::this_thread::sleep_for(100ms);
            work_done = true;
        });

        // Wait for work to start
        while (!work_running.load()) {
            std::this_thread::sleep_for(1ms);
        }

        // stop() should return immediately, not wait for work
        pool.stop();

        // Work should still be running (stop doesn't join)
        CHECK_FALSE(work_done.load());

        // Now join to wait for completion
        pool.join();
        CHECK(work_done.load());
    }

    SECTION("join after stop waits for running work to complete") {
        threadpool pool(2);
        std::atomic<bool> work_started{false};
        std::atomic<bool> work_done{false};

        ::asio::post(pool.get_executor(), [&]() {
            work_started = true;
            std::this_thread::sleep_for(50ms);
            work_done = true;
        });

        // Wait for work to start before stopping
        while (!work_started.load()) {
            std::this_thread::sleep_for(1ms);
        }

        pool.stop();
        pool.join();

        // Work that was already running should complete
        CHECK(work_done.load());
    }
}
