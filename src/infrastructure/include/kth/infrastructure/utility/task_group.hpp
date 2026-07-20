// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TASK_GROUP_HPP
#define KTH_INFRASTRUCTURE_TASK_GROUP_HPP

#include <atomic>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/asio_helper.hpp>

#if defined(KTH_ASIO_STANDALONE)
#include <asio/steady_timer.hpp>
#else
#include <boost/asio/steady_timer.hpp>
#endif

namespace kth {

// =============================================================================
// Task Group (Nursery Pattern for Structured Concurrency)
// =============================================================================
//
// A task_group manages a set of concurrent coroutines and ensures all complete
// before the group is destroyed. This implements the "nursery" pattern from
// structured concurrency (similar to Trio in Python or Kotlin coroutines).
//
// Key properties:
//   - All spawned tasks are tracked automatically
//   - join() waits for ALL tasks to complete
//   - No tasks are "lost" or detached
//   - Exception in any task can be propagated (future enhancement)
//
// Usage:
//   task_group tasks(executor);
//
//   tasks.spawn([&]() -> awaitable<void> {
//       co_await do_work_1();
//   });
//
//   tasks.spawn([&]() -> awaitable<void> {
//       co_await do_work_2();
//   });
//
//   co_await tasks.join();  // Waits for both tasks
//
// Integration with structured concurrency:
//   awaitable<void> parent_task() {
//       task_group children(co_await this_coro::executor);
//
//       children.spawn(child_1());
//       children.spawn(child_2());
//
//       co_await children.join();  // Parent waits for all children
//   }
//
// =============================================================================

class task_group {
public:
    explicit 
    task_group(std::string_view group_name, ::asio::any_io_executor executor)
        : state_(std::make_shared<shared_state>(group_name, executor))
    {}

    // Non-copyable, non-movable (prevents accidental misuse)
    task_group(task_group const&) = delete;
    task_group& operator=(task_group const&) = delete;
    task_group(task_group&&) = delete;
    task_group& operator=(task_group&&) = delete;

    ~task_group() {
        // In debug builds, assert that join() was called
        // In release, just log a warning if tasks are still active
        if (state_->active_count.load() > 0) {
            // Tasks still running - this is a programming error
            // The destructor should only be called after join()
            // However, with shared_state, the spawned coroutines will keep
            // the state alive until they complete, preventing use-after-free.
        }
    }

    // Spawn a coroutine into the group.
    // The coroutine will be tracked and join() will wait for it.
    // Note: This uses ONE internal detached spawn, but the task is tracked.
    //
    // Accepts either:
    //   - An awaitable<void> directly: tasks.spawn("name", some_coro())
    //   - A callable returning awaitable<void>: tasks.spawn("name", [&]() -> awaitable<void> { ... })
    template<typename Coro>
    void spawn(std::string_view task_name, Coro&& coro) {
        ++state_->active_count;
        ++state_->total_spawned;

        // Capture shared_ptr to keep state alive even if task_group is destroyed
        auto state = state_;
        std::string name{task_name};

        ::asio::co_spawn(state_->executor,
            [state, name = std::move(name), c = std::forward<Coro>(coro)]() mutable -> ::asio::awaitable<void> {
                spdlog::debug("[task_group:{}] START: {} (active: {})", state->name, name, state->active_count.load());
                try {
                    if constexpr (std::is_invocable_v<Coro>) {
                        // It's a callable (lambda, function) - invoke it to get the awaitable
                        co_await std::invoke(std::move(c));
                    } else {
                        // It's already an awaitable - just await it directly
                        co_await std::move(c);
                    }
                } catch (...) {
                    spdlog::debug("[task_group:{}] EXCEPTION: {}", state->name, name);
                    // Stash the first exception so join() can rethrow it.
                    // Subsequent exceptions are swallowed (logged above) — we
                    // can only carry one exception out of join() and the
                    // first one is the most useful for diagnosis. Exception
                    // capture is a slow path; a mutex here is cheaper than
                    // the cost of materializing the std::current_exception
                    // we'd be throwing away on contention.
                    std::lock_guard<std::mutex> lk(state->exception_mutex);
                    if ( ! state->first_exception) {
                        state->first_exception = std::current_exception();
                    }
                }
                spdlog::debug("[task_group:{}] END: {} (active: {})", state->name, name, state->active_count.load() - 1);
                state->decrement_and_signal();
            },
            ::asio::detached);  // Single controlled detached point
    }

    // Wait for all spawned tasks to complete.
    // This MUST be called before the task_group is destroyed.
    [[nodiscard]]
    ::asio::awaitable<void> join() {
        // 2026-02-06: Simple polling approach for reliability.
        // Channel-based signaling had race conditions causing deadlocks.
        // Polling every 1ms is simple and reliable.
        auto executor = co_await ::asio::this_coro::executor;
        ::asio::steady_timer timer(executor);
        uint64_t poll_count = 0;
        while (state_->active_count.load() > 0) {
            ++poll_count;
            // 2026-02-07: Log every ~1 second (1000 polls) for debugging hangs
            if (poll_count % 1000 == 0) {
                spdlog::debug("[task_group:{}] join() polling: {} active tasks remaining (poll #{})",
                    state_->name, state_->active_count.load(), poll_count);
            }
            timer.expires_after(std::chrono::milliseconds(1));
            co_await timer.async_wait(::asio::use_awaitable);
        }
        // If any task threw, re-throw its exception now that everyone has
        // wound down. We move it out of the slot first so a re-entrant
        // join() on the same state doesn't re-fire the same exception.
        std::exception_ptr ex;
        {
            std::lock_guard<std::mutex> lk(state_->exception_mutex);
            ex = std::move(state_->first_exception);
            state_->first_exception = nullptr;
        }
        if (ex) {
            std::rethrow_exception(ex);
        }
        co_return;
    }

    // Check if there are active tasks
    [[nodiscard]]
    bool has_active_tasks() const noexcept {
        return state_->active_count.load() > 0;
    }

    // Get number of currently active tasks
    [[nodiscard]]
    size_t active_count() const noexcept {
        return state_->active_count.load();
    }

    // Get total number of tasks spawned (including completed)
    [[nodiscard]]
    size_t total_spawned() const noexcept {
        return state_->total_spawned.load();
    }

private:
    // Shared state that outlives the task_group if needed
    struct shared_state {
        explicit shared_state(std::string_view group_name, ::asio::any_io_executor exec)
            : name(group_name)
            , executor(std::move(exec))
        {}

        void decrement_and_signal() {
            // 2026-02-06: Simple atomic decrement. join() polls active_count.
            active_count.fetch_sub(1);
        }

        std::string name;
        ::asio::any_io_executor executor;
        std::atomic<size_t> active_count{0};
        std::atomic<size_t> total_spawned{0};
        // First exception thrown by any spawned task. join() re-throws it
        // after all tasks complete so callers see the failure instead of
        // it being silently swallowed in the detached coroutine.
        // std::exception_ptr is not trivially copyable, so it lives behind
        // a mutex rather than in a std::atomic.
        std::mutex exception_mutex;
        std::exception_ptr first_exception;
    };

    std::shared_ptr<shared_state> state_;
};

// =============================================================================
// Scoped Task Group (RAII wrapper)
// =============================================================================
//
// Automatically joins on scope exit. Useful for ensuring structured concurrency
// even when exceptions occur.
//
// Usage:
//   {
//       scoped_task_group tasks(executor);
//       tasks.spawn(work_1());
//       tasks.spawn(work_2());
//   }  // Automatically waits here (blocking!)
//
// WARNING: The destructor blocks! Use with care.
//          Prefer explicit co_await join() in coroutines.
//
// =============================================================================

class scoped_task_group {
public:
    explicit scoped_task_group(std::string_view group_name, ::asio::any_io_executor executor)
        : executor_(executor)
        , group_(group_name, std::move(executor))
    {}

    ~scoped_task_group() {
        if (group_.has_active_tasks()) {
            // Block until all tasks complete
            // This is a blocking call - use sparingly!
            std::promise<void> done;
            auto future = done.get_future();

            ::asio::co_spawn(executor_,
                [this, &done]() -> ::asio::awaitable<void> {
                    co_await group_.join();
                    done.set_value();
                },
                ::asio::detached);

            future.wait();
        }
    }

    template<typename Coro>
    void spawn(std::string_view task_name, Coro&& coro) {
        group_.spawn(task_name, std::forward<Coro>(coro));
    }

    [[nodiscard]]
    ::asio::awaitable<void> join() {
        return group_.join();
    }

    [[nodiscard]] size_t active_count() const noexcept {
        return group_.active_count();
    }

private:
    ::asio::any_io_executor executor_;
    task_group group_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_TASK_GROUP_HPP
