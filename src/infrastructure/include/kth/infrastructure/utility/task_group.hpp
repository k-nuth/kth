// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_TASK_GROUP_HPP
#define KTH_INFRASTRUCTURE_TASK_GROUP_HPP

#include <atomic>
#include <cstddef>
#include <concepts>
#include <memory>
#include <type_traits>
#include <utility>

#include <kth/infrastructure/utility/asio_helper.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>

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
    explicit task_group(::asio::any_io_executor executor)
        : state_(std::make_shared<shared_state>(executor))
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
    //   - An awaitable<void> directly: tasks.spawn(some_coro())
    //   - A callable returning awaitable<void>: tasks.spawn([&]() -> awaitable<void> { ... })
    template<typename Coro>
    void spawn(Coro&& coro) {
        ++state_->active_count;
        ++state_->total_spawned;

        // Capture shared_ptr to keep state alive even if task_group is destroyed
        auto state = state_;

        ::asio::co_spawn(state_->executor,
            [state, c = std::forward<Coro>(coro)]() mutable -> ::asio::awaitable<void> {
                try {
                    if constexpr (std::is_invocable_v<Coro>) {
                        // It's a callable (lambda, function) - invoke it to get the awaitable
                        co_await std::invoke(std::move(c));
                    } else {
                        // It's already an awaitable - just await it directly
                        co_await std::move(c);
                    }
                } catch (...) {
                    // TODO: Store exception for later propagation
                    // For now, just decrement and signal
                }
                state->decrement_and_signal();
            },
            ::asio::detached);  // Single controlled detached point
    }

    // Wait for all spawned tasks to complete.
    // This MUST be called before the task_group is destroyed.
    [[nodiscard]]
    ::asio::awaitable<void> join() {
        // Loop until all tasks complete.
        // We need to loop because there's a race between checking active_count
        // and starting to wait on the channel - tasks might complete in between.
        while (state_->active_count.load() > 0) {
            // Wait for signal that a task completed
            auto [ec] = co_await state_->done_channel.async_receive(
                ::asio::as_tuple(::asio::use_awaitable));

            // If channel is closed or errored, exit
            if (ec) {
                break;
            }
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
        explicit shared_state(::asio::any_io_executor exec)
            : executor(std::move(exec))
            , done_channel(executor, 1)
        {}

        void decrement_and_signal() {
            auto const prev = active_count.fetch_sub(1);
            if (prev == 1) {
                // Last task completed - cancel and close the channel.
                // cancel() wakes up any pending async_receive with operation_aborted.
                // close() prevents new operations.
                // Note: close() alone does NOT cancel pending async operations!
                done_channel.cancel();
                done_channel.close();
            }
        }

        ::asio::any_io_executor executor;
        std::atomic<size_t> active_count{0};
        std::atomic<size_t> total_spawned{0};
        concurrent_event_channel done_channel;
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
    explicit scoped_task_group(::asio::any_io_executor executor)
        : executor_(executor)
        , group_(std::move(executor))
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
    void spawn(Coro&& coro) {
        group_.spawn(std::forward<Coro>(coro));
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
