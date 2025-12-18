// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_THREADPOOL_HPP
#define KTH_INFRASTRUCTURE_THREADPOOL_HPP

#include <cstddef>
#include <thread>

#include <kth/infrastructure/utility/asio_helper.hpp>

namespace kth {

struct threadpool {
    explicit 
    threadpool(size_t num_threads = std::thread::hardware_concurrency())
        : num_threads_(num_threads == 0 ? std::thread::hardware_concurrency() : num_threads)
        , pool_(num_threads_)
    {}

    ~threadpool() = default;

    threadpool(threadpool const&) = delete;
    threadpool& operator=(threadpool const&) = delete;
    threadpool(threadpool&&) = delete;
    threadpool& operator=(threadpool&&) = delete;

    [[nodiscard]]
    ::asio::thread_pool::executor_type get_executor() {
        return pool_.get_executor();
    }

    [[nodiscard]]
    ::asio::any_io_executor executor() {
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

private:
    size_t num_threads_;
    ::asio::thread_pool pool_;
};

} // namespace kth

#endif // KTH_INFRASTRUCTURE_THREADPOOL_HPP
