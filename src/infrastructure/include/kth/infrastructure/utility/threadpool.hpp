// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_THREADPOOL_HPP
#define KTH_INFRASTRUCTURE_THREADPOOL_HPP

#if ! defined(__EMSCRIPTEN__)
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/noncopyable.hpp>

#endif

#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

#if ! defined(__EMSCRIPTEN__)
/**
 * This class and the asio service it exposes are thread safe.
 * A collection of threads which can be passed operations through io_context.
 */
class KI_API threadpool
    : noncopyable
{
public:

    /**
     * Threadpool constructor, spawns the specified number of threads.
     * @param[in]   number_threads  Number of threads to spawn.
     * @param[in]   priority        Priority of threads to spawn.
     */
    explicit
    threadpool(std::string const& name, size_t number_threads = 0, thread_priority priority = thread_priority::normal);

    ~threadpool();

    /**
     * There are no threads configured in the threadpool.
     */
    bool empty() const;

    /**
     * The number of threads configured in the threadpool.
     */
    size_t size() const;

    /**
     * Add the specified number of threads to this threadpool.
     * @param[in]   number_threads  Number of threads to add.
     * @param[in]   priority        Priority of threads to add.
     */
    void spawn(size_t number_threads = 1, thread_priority priority = thread_priority::normal);

    /**
     * Abandon outstanding operations without dispatching handlers.
     * WARNING: This call is unsave and should be avoided.
     */
    void abort();

    /**
     * Destroy the work keep alive, allowing threads be joined.
     * Caller should next call join.
     */
    void shutdown();

    /**
     * Wait for all threads in the pool to terminate.
     * This is safe to call from any thread in the threadpool or otherwise.
     */
    void join();

    /**
     * Underlying boost::io_context object.
     */
    asio::context& service();

    /**
     * Underlying boost::io_context object.
     */
    const asio::context& service() const;

private:
    void spawn_once(thread_priority priority=thread_priority::normal);

    // This is thread safe.
    asio::context service_;

    // These are protected by mutex.

    std::string name_;
    std::atomic<size_t> size_;
    std::vector<asio::thread> threads_;
    mutable upgrade_mutex threads_mutex_;
    std::shared_ptr<asio::work_guard> work_;
    mutable upgrade_mutex work_mutex_;
};

#else

struct threadpool {
    explicit
    threadpool(std::string const& name, size_t number_threads = 0, thread_priority priority = thread_priority::normal)
    {}

    void shutdown() {}
    void join() {}
    size_t size() const {
        return 1;
    }
    bool empty() const {
        return false;
    }
};

#endif // ! defined(__EMSCRIPTEN__)

} // namespace kth

#endif

