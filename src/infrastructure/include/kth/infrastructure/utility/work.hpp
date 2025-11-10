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
#include <kth/infrastructure/utility/monitor.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>
#include <kth/infrastructure/utility/sequencer.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

namespace kth {

#define ORDERED "ordered"
#define UNORDERED "unordered"
#define CONCURRENT "concurrent"
#define SEQUENCE "sequence"

/// This  class is thread safe.
/// boost asio class wrapper to enable work heap management.
class KI_API work
  : noncopyable
{
public:
    using ptr = std::shared_ptr<work>;

    /// Create an instance.
    work(threadpool& pool, std::string const& name);

    /// Local execution for any operation, equivalent to std::bind.
    template <typename Handler, typename... Args>
    static void bound(Handler&& handler, Args&&... args) {
        std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...)();
    }

    /// Concurrent execution for any operation.
    template <typename Handler, typename... Args>
    void concurrent(Handler&& handler, Args&&... args) {
        // Service post ensures the job does not execute in the current thread.
        ::asio::post(service_, std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Sequential execution for synchronous operations.
    template <typename Handler, typename... Args>
    void ordered(Handler&& handler, Args&&... args) {
        // Use a strand to prevent concurrency and post vs. dispatch to ensure
        // that the job is not executed in the current thread.
        ::asio::post(strand_, std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Non-concurrent execution for synchronous operations.
    template <typename Handler, typename... Args>
    void unordered(Handler&& handler, Args&&... args) {
        // Use a strand wrapper to prevent concurrency and a service post
        // to deny ordering while ensuring execution on another thread.
        // TODO: Review bind_executor vs deprecated wrap() for behavioral differences
        // See: https://github.com/k-nuth/kth-mono/issues/76
        ::asio::post(service_, ::asio::bind_executor(strand_, std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...)));
    }

    /// Begin sequential execution for a set of asynchronous operations.
    /// The operation will be queued until the lock is free and then executed.
    template <typename Handler, typename... Args>
    void lock(Handler&& handler, Args&&... args) {
        // Use a sequence to track the asynchronous operation to completion,
        // ensuring each asynchronous op executes independently and in order.
        sequence_.lock(std::bind_front(std::forward<Handler>(handler), std::forward<Args>(args)...));
    }

    /// Complete sequential execution.
    void unlock() {
        sequence_.unlock();
    }

    ////size_t ordered_backlog();
    ////size_t unordered_backlog();
    ////size_t concurrent_backlog();
    ////size_t sequential_backlog();
    ////size_t combined_backlog();

private:
    ////template <typename Handler>
    ////auto inject(Handler&& handler, std::string const& context,
    ////    monitor::count_ptr counter) -> std::function<void()>
    ////{
    ////    auto label = name_ + "_" + context;
    ////    auto capture = std::make_shared<monitor>(counter, std::move(label));
    ////    return [=]() { capture->invoke(handler); };

    ////    //// TODO: use this to prevent handler copy into closure.
    ////    ////return std::bind(&monitor::invoke<Handler>, capture,
    ////    ////    std::forward<Handler>(handler));
    ////}

    // These are thread safe.
    std::string const name_;
    ////monitor::count_ptr ordered_;
    ////monitor::count_ptr unordered_;
    ////monitor::count_ptr concurrent_;
    ////monitor::count_ptr sequential_;
    asio::context& service_;
    asio::context::strand strand_;
    sequencer sequence_;
};

} // namespace kth

#endif
