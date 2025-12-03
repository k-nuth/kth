// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_WORK_HPP
#define KTH_INFRASTRUCTURE_WORK_HPP

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/sequencer.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

namespace kth {

/// This class is thread safe.
/// Executor-based work dispatcher for async operations.
class KI_API work : noncopyable {
public:
    using ptr = std::shared_ptr<work>;
    using executor_type = ::asio::any_io_executor;
    using strand_type = ::asio::strand<executor_type>;

    /// Create an instance.
    work(threadpool& pool, std::string const& name);

    /// Local execution for any operation, equivalent to std::bind.
    template <typename Handler, typename... Args>
    static 
    void bound(Handler&& handler, Args&&... args) {
        std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...)();
    }

    /// Concurrent execution for any operation.
    template <typename Handler, typename... Args>
    void concurrent(Handler&& handler, Args&&... args) {
        // Post ensures the job does not execute in the current thread.
        ::asio::post(executor_, std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Sequential execution for synchronous operations.
    template <typename Handler, typename... Args>
    void ordered(Handler&& handler, Args&&... args) {
        // Use a strand to prevent concurrency and post vs. dispatch to ensure
        // that the job is not executed in the current thread.
        ::asio::post(strand_, std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Non-concurrent execution for synchronous operations.
    template <typename Handler, typename... Args>
    void unordered(Handler&& handler, Args&&... args) {
        // Use a strand wrapper to prevent concurrency and a post
        // to deny ordering while ensuring execution on another thread.
        ::asio::post(executor_, ::asio::bind_executor(strand_, std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...)));
    }

    /// Begin sequential execution for a set of asynchronous operations.
    /// The operation will be queued until the lock is free and then executed.
    template <typename Handler, typename... Args>
    void lock(Handler&& handler, Args&&... args) {
        // Use a sequence to track the asynchronous operation to completion,
        // ensuring each asynchronous op executes independently and in order.
        sequence_.lock(std::bind(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Complete sequential execution.
    void unlock() {
        sequence_.unlock();
    }

    /// Get the executor.
    [[nodiscard]]
    executor_type get_executor() const {
        return executor_;
    }

private:
    std::string const name_;
    executor_type executor_;
    strand_type strand_;
    sequencer sequence_;
};

} // namespace kth

#endif
