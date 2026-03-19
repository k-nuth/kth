// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <kth/infrastructure/utility/task_group.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

#if defined(KTH_ASIO_STANDALONE)
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/as_tuple.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#else
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/as_tuple.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>
#endif

using namespace kth;
using namespace std::chrono_literals;

// Use the appropriate error_code type
#if defined(KTH_ASIO_STANDALONE)
using error_code_t = std::error_code;
#else
using error_code_t = boost::system::error_code;
#endif

// Start Test Suite: task_group tests

TEST_CASE("task_group spawn single task join completes", "[task_group tests]") {
    threadpool pool("test", 2);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        std::atomic<bool> task_completed{false};

        group.spawn("test", [&]() -> ::asio::awaitable<void> {
            // Simulate some async work
            ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
            timer.expires_after(10ms);
            co_await timer.async_wait(::asio::use_awaitable);
            task_completed = true;
        });

        co_await group.join();

        REQUIRE(task_completed);
        REQUIRE(group.total_spawned() == 1);
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    auto status = future.wait_for(5s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group spawn multiple tasks join waits for all", "[task_group tests]") {
    threadpool pool("test", 4);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        std::atomic<int> completed_count{0};
        constexpr int task_count = 10;

        for (int i = 0; i < task_count; ++i) {
            group.spawn("test", [&, i]() -> ::asio::awaitable<void> {
                // Different delays to simulate varying work
                ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                timer.expires_after(std::chrono::milliseconds(5 + i * 2));
                co_await timer.async_wait(::asio::use_awaitable);
                ++completed_count;
            });
        }

        co_await group.join();

        REQUIRE(completed_count == task_count);
        REQUIRE(group.total_spawned() == task_count);
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    auto status = future.wait_for(5s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group join does not deadlock when tasks complete before join starts", "[task_group tests]") {
    threadpool pool("test", 4);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        std::atomic<int> completed_count{0};
        constexpr int task_count = 5;

        // Spawn tasks that complete very quickly
        for (int i = 0; i < task_count; ++i) {
            group.spawn("test", [&]() -> ::asio::awaitable<void> {
                ++completed_count;
                co_return;  // Complete immediately
            });
        }

        // Add a small delay to increase chance of race condition
        // (tasks may complete before join() starts waiting)
        ::asio::steady_timer timer(executor);
        timer.expires_after(50ms);
        co_await timer.async_wait(::asio::use_awaitable);

        // join() should still complete even if all tasks already finished
        co_await group.join();

        REQUIRE(completed_count == task_count);
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    // If there's a deadlock, this will timeout
    auto status = future.wait_for(5s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group handles rapid task completion without deadlock", "[task_group tests]") {
    threadpool pool("test", 8);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        std::atomic<int> completed_count{0};
        constexpr int task_count = 100;

        // Spawn many tasks that all try to complete at roughly the same time
        for (int i = 0; i < task_count; ++i) {
            group.spawn("test", [&, i]() -> ::asio::awaitable<void> {
                // Tiny random-ish delay based on task index
                if (i % 3 == 0) {
                    ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                    timer.expires_after(1ms);
                    co_await timer.async_wait(::asio::use_awaitable);
                }
                ++completed_count;
            });
        }

        co_await group.join();

        REQUIRE(completed_count == task_count);
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    auto status = future.wait_for(10s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group exception in task still allows join to complete", "[task_group tests]") {
    threadpool pool("test", 2);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        std::atomic<bool> task1_completed{false};
        std::atomic<bool> task2_completed{false};

        group.spawn("test", [&]() -> ::asio::awaitable<void> {
            task1_completed = true;
            throw std::runtime_error("test exception");
        });

        group.spawn("test", [&]() -> ::asio::awaitable<void> {
            ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
            timer.expires_after(10ms);
            co_await timer.async_wait(::asio::use_awaitable);
            task2_completed = true;
        });

        co_await group.join();

        // Both tasks should be considered "done" (one threw, one completed)
        REQUIRE(task1_completed);
        REQUIRE(task2_completed);
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    auto status = future.wait_for(5s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group has_active_tasks returns correct value", "[task_group tests]") {
    threadpool pool("test", 2);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        task_group group("test_group", executor);

        REQUIRE_FALSE(group.has_active_tasks());

        std::atomic<bool> can_finish{false};

        group.spawn("test", [&]() -> ::asio::awaitable<void> {
            while (!can_finish) {
                ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                timer.expires_after(5ms);
                co_await timer.async_wait(::asio::use_awaitable);
            }
        });

        // Give the task time to start
        ::asio::steady_timer timer(executor);
        timer.expires_after(20ms);
        co_await timer.async_wait(::asio::use_awaitable);

        REQUIRE(group.has_active_tasks());
        REQUIRE(group.active_count() == 1);

        can_finish = true;
        co_await group.join();

        REQUIRE_FALSE(group.has_active_tasks());
        REQUIRE(group.active_count() == 0);

        done.set_value();
    }, ::asio::detached);

    auto status = future.wait_for(5s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

TEST_CASE("task_group stress test mimics p2p_node shutdown", "[task_group tests][stress]") {
    // Run the test multiple times to increase chance of hitting race conditions
    for (int iteration = 0; iteration < 10; ++iteration) {
        threadpool pool("test", 8);
        auto executor = pool.get_executor();

        std::promise<void> done;
        auto future = done.get_future();

        ::asio::co_spawn(executor, [&, iteration]() -> ::asio::awaitable<void> {
            task_group group("test_group", executor);

            std::atomic<int> completed_count{0};
            // Simulate p2p_node with 32 peer connections
            constexpr int task_count = 32;

            // Spawn all "peer" tasks
            for (int i = 0; i < task_count; ++i) {
                group.spawn("test", [&, i]() -> ::asio::awaitable<void> {
                    // Simulate varying peer lifetimes
                    if (i % 4 != 0) {
                        ::asio::steady_timer timer(co_await ::asio::this_coro::executor);
                        timer.expires_after(std::chrono::milliseconds(i % 10));
                        co_await timer.async_wait(::asio::use_awaitable);
                    }
                    ++completed_count;
                });
            }

            // Small delay to let some tasks complete (simulating stop() being called)
            ::asio::steady_timer timer(executor);
            timer.expires_after(5ms);
            co_await timer.async_wait(::asio::use_awaitable);

            // This is where p2p_node was deadlocking
            co_await group.join();

            REQUIRE(completed_count == task_count);
            REQUIRE(group.active_count() == 0);

            done.set_value();
        }, ::asio::detached);

        // 2 second timeout per iteration - if it hangs, it's a deadlock
        auto status = future.wait_for(2s);
        INFO("Iteration " << iteration);
        REQUIRE(status == std::future_status::ready);

        pool.stop();
        pool.join();
    }
}

// =============================================================================
// Polling task must notify waiting task on exit
// =============================================================================
// Pattern: Task A waits on a channel, Task B polls a condition.
// When B's condition becomes true and B exits, it MUST send a message to A's
// channel, otherwise A will wait forever and join() will deadlock.
// =============================================================================

TEST_CASE("task_group polling task must notify channel waiters on exit", "[task_group tests]") {
    constexpr bool SEND_NOTIFICATION = true;  // Set to false to see deadlock

    threadpool pool("test", 4);
    auto executor = pool.get_executor();

    std::promise<void> done;
    auto future = done.get_future();

    // Simulates the "stopped" condition (like network.stopped())
    std::atomic<bool> stopped{false};

    // Channel for coordinator events (like coordinator_events in orchestrator)
    using event_channel = ::asio::experimental::channel<void(error_code_t, int)>;

    ::asio::co_spawn(executor, [&]() -> ::asio::awaitable<void> {
        event_channel coordinator_events(co_await ::asio::this_coro::executor, 16);

        task_group group("shutdown_test", executor);
        std::atomic<bool> coordinator_received_stop{false};

        // Task 1: Waiting task - waits for events on channel
        group.spawn("waiting_task", [&]() -> ::asio::awaitable<void> {
            spdlog::debug("[test:waiting_task] Waiting for events...");
            while (true) {
                auto [ec, event] = co_await coordinator_events.async_receive(
                    ::asio::as_tuple(::asio::use_awaitable));
                if (ec) {
                    spdlog::debug("[test:waiting_task] Channel closed: {}", ec.message());
                    break;
                }
                if (event == -1) {  // -1 = stop signal
                    spdlog::debug("[test:waiting_task] Received stop signal");
                    coordinator_received_stop = true;
                    break;
                }
                spdlog::debug("[test:waiting_task] Received event: {}", event);
            }
            spdlog::debug("[test:waiting_task] Exiting");
        });

        // Task 2: Polling task - polls a condition and must notify waiting task on exit
        group.spawn("polling_task", [&]() -> ::asio::awaitable<void> {
            spdlog::debug("[test:polling_task] Started, polling condition...");
            ::asio::steady_timer timer(co_await ::asio::this_coro::executor);

            while (!stopped.load()) {
                timer.expires_after(std::chrono::milliseconds(10));
                co_await timer.async_wait(::asio::use_awaitable);
            }

            spdlog::debug("[test:polling_task] Condition is true, exiting loop");

            // Critical: notify the waiting task before exiting
            if constexpr (SEND_NOTIFICATION) {
                spdlog::debug("[test:polling_task] Sending stop notification to waiting task");
                coordinator_events.try_send(error_code_t{}, -1);  // -1 = stop signal
            } else {
                spdlog::debug("[test:polling_task] NOT sending notification - will cause deadlock!");
            }

            spdlog::debug("[test:polling_task] Exiting");
        });

        // Simulate some work, then trigger shutdown
        ::asio::steady_timer work_timer(executor);
        work_timer.expires_after(std::chrono::milliseconds(50));
        co_await work_timer.async_wait(::asio::use_awaitable);

        spdlog::debug("[test] Setting stopped = true (simulating sync complete + Ctrl-C)");
        stopped = true;

        // Wait for all tasks - this is where deadlock would occur without the fix
        spdlog::debug("[test] Calling group.join()...");
        co_await group.join();
        spdlog::debug("[test] group.join() returned!");

        if constexpr (SEND_NOTIFICATION) {
            REQUIRE(coordinator_received_stop);
        }

        done.set_value();
    }, ::asio::detached);

    // If SEND_NOTIFICATION is false, this will timeout (deadlock)
    auto status = future.wait_for(2s);
    REQUIRE(status == std::future_status::ready);
    pool.stop();
    pool.join();
}

// End Test Suite
