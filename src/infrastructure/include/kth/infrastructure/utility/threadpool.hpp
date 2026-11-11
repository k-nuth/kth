// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_THREADPOOL_HPP
#define KTH_INFRASTRUCTURE_THREADPOOL_HPP

#include <cstddef>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>
#include <asio/thread_pool.hpp>

#if defined(KTH_BROWSER_NODE)
#include <kth/infrastructure/utility/browser_runtime.hpp>
namespace kth {
struct threadpool {
    using context_type = ::asio::io_context;
    explicit threadpool(std::string_view, size_t = 1) {}
    auto get_executor() { return browser::runtime::current().context().get_executor(); }
    auto executor() { return get_executor(); }
    // The node joins its coroutine nurseries before releasing this borrowed
    // Worker executor; it must never stop or synchronously join the event loop.
    void stop() {}
    void join() {}
    size_t size() const noexcept { return 1; }
    context_type& get() { return browser::runtime::current().context(); }
};
}
#else
namespace kth {

// Wrapper over asio::thread_pool that tracks the thread count.
struct threadpool {
    using context_type = ::asio::thread_pool;
    explicit
    threadpool(std::string_view name, size_t num_threads = std::thread::hardware_concurrency())
        : name_(name)
        , num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads)
        , pool_(num_threads_)
    {
        spdlog::info("[threadpool] '{}' created with {} threads", name_, num_threads_);
    }

    ~threadpool() {
        spdlog::info("[threadpool] '{}' destroyed ({} threads)", name_, num_threads_);
    }

    threadpool(threadpool const&) = delete;
    threadpool& operator=(threadpool const&) = delete;
    threadpool(threadpool&&) = delete;
    threadpool& operator=(threadpool&&) = delete;

    [[nodiscard]]
    auto get_executor() {
        return pool_.get_executor();
    }

    [[nodiscard]]
    auto executor() {
        return pool_.get_executor();
    }

    void stop() {
        pool_.stop();
    }

    void join() {
        pool_.join();
    }

    [[nodiscard]]
    size_t size() const noexcept {
        return num_threads_;
    }

    // Access the underlying asio::thread_pool
    [[nodiscard]]
    ::asio::thread_pool& get() noexcept {
        return pool_;
    }

private:
    std::string_view name_;
    size_t num_threads_;
    ::asio::thread_pool pool_;
};

} // namespace kth

#endif // KTH_BROWSER_NODE
#endif // KTH_INFRASTRUCTURE_THREADPOOL_HPP
