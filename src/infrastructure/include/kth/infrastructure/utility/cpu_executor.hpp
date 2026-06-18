// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CPU_EXECUTOR_HPP
#define KTH_INFRASTRUCTURE_CPU_EXECUTOR_HPP

#include <cstddef>
#include <thread>
#include <type_traits>
#include <utility>

#include <kth/infrastructure/utility/asio_helper.hpp>

namespace kth {

// =============================================================================
// CPU Executor (Dedicated Thread Pool for CPU-Bound Work)
// =============================================================================
//
// A specialized executor for CPU-bound operations that should not block the
// IO thread pool. This separates IO-bound async operations from CPU-intensive
// work like block validation, transaction verification, and cryptographic ops.
//
// Key properties:
//   - Separate thread pool from IO operations
//   - Sized to match hardware concurrency by default
//   - Async interface via awaitable for seamless coroutine integration
//   - Work can be posted from any thread
//
// Usage in coroutines:
//   cpu_executor cpu;
//
//   awaitable<void> validate_block(block_ptr block) {
//       // Offload CPU-intensive validation to CPU pool
//       bool valid = co_await cpu.execute([&block]() {
//           return block->is_valid();  // Runs on CPU pool thread
//       });
//
//       if (!valid) {
//           throw validation_error("Invalid block");
//       }
//   }
//
// Architecture:
//   ┌─────────────────┐       ┌─────────────────┐
//   │  IO Thread Pool │       │ CPU Thread Pool │
//   │  (network I/O)  │       │  (validation)   │
//   │                 │       │                 │
//   │  peer1.run()    │       │  validate()     │
//   │  peer2.run()    │  ───► │  verify_sig()   │
//   │  accept_loop()  │       │  hash_block()   │
//   │  ...            │       │  ...            │
//   └─────────────────┘       └─────────────────┘
//
// =============================================================================

class cpu_executor {
public:
    // Construct with specified number of threads
    // Default: hardware_concurrency (number of CPU cores)
    explicit cpu_executor(size_t threads = std::thread::hardware_concurrency())
        : pool_(threads > 0 ? threads : 1)
    {}

    // Non-copyable, non-movable
    cpu_executor(cpu_executor const&) = delete;
    cpu_executor& operator=(cpu_executor const&) = delete;
    cpu_executor(cpu_executor&&) = delete;
    cpu_executor& operator=(cpu_executor&&) = delete;

    ~cpu_executor() {
        stop();
        join();
    }

    // Execute a callable on the CPU pool and return result via awaitable.
    // This is the primary interface for offloading CPU work from coroutines.
    //
    // Example:
    //   auto result = co_await cpu.execute([]() {
    //       return expensive_computation();
    //   });
    //
    template<typename F>
    auto execute(F&& work) -> ::asio::awaitable<std::invoke_result_t<F>> {
        using result_type = std::invoke_result_t<F>;

        co_return co_await ::asio::co_spawn(
            pool_.get_executor(),
            [w = std::forward<F>(work)]() -> ::asio::awaitable<result_type> {
                if constexpr (std::is_void_v<result_type>) {
                    std::invoke(std::move(w));
                    co_return;
                } else {
                    co_return std::invoke(std::move(w));
                }
            },
            ::asio::use_awaitable
        );
    }

    // Post work to the CPU pool without waiting for result.
    // Use when you don't need the result in the calling coroutine.
    template<typename F>
    void post(F&& work) {
        ::asio::post(pool_.get_executor(), std::forward<F>(work));
    }

    // Get the underlying executor for advanced use cases
    [[nodiscard]]
    auto get_executor() {
        return pool_.get_executor();
    }

    // Stop accepting new work
    void stop() {
        pool_.stop();
    }

    // Wait for all work to complete
    void join() {
        pool_.join();
    }

    // Get number of threads in the pool
    [[nodiscard]]
    size_t thread_count() const noexcept {
        return thread_count_;
    }

private:
    ::asio::thread_pool pool_;
    size_t thread_count_{std::thread::hardware_concurrency()};
};

// =============================================================================
// Global CPU Executor Access
// =============================================================================
//
// For convenience, a global CPU executor can be used. However, prefer passing
// the executor explicitly for better testability and control.
//
// Usage:
//   auto& cpu = kth::global_cpu_executor();
//   co_await cpu.execute([]() { ... });
//
// =============================================================================

inline cpu_executor& global_cpu_executor() {
    static cpu_executor instance;
    return instance;
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_CPU_EXECUTOR_HPP
