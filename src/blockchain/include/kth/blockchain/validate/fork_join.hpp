// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
#define KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace kth::blockchain {

// The number of threads a fork-join over `count` independent items uses: the
// hardware's, never more than the items, and one in the browser node, whose
// Worker owns validation and creates no native threads.
inline size_t fork_join_workers(size_t count) noexcept {
#if defined(KTH_BROWSER_NODE)
    size_t const hw = 1;
#else
    size_t const hw = std::thread::hardware_concurrency();
#endif
    return std::max<size_t>(1, std::min<size_t>(hw ? hw : 1, count));
}

namespace detail {

// One fork-join, on the stack of its caller. The threads of the group reach it
// only while it is listed or while they are counted in `inside`, and the caller
// does not return before both are over.
struct fork_join_call {
    // The next index nobody has taken.
    std::atomic<size_t> next{0};
    std::atomic<bool> threw{false};
    size_t count = 0;
    void const* body = nullptr;
    void (*invoke)(void const*, size_t) = nullptr;
    // Guarded by the group: the threads it still has room for, and the threads
    // running its calls.
    size_t wanted = 0;
    size_t inside = 0;

    // Runs indices until none is left. Stops taking them once a call threw.
    void drain() noexcept {
        while ( ! threw.load(std::memory_order_acquire)) {
            auto const i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= count) {
                return;
            }
            try {
                invoke(body, i);
            } catch (...) {
                // A thread must not let an exception escape (std::terminate).
                threw.store(true, std::memory_order_release);
            }
        }
    }
};

#if ! defined(KTH_BROWSER_NODE)
// The threads every fork-join of the process runs on, besides its caller.
// Started by the first call that needs them and joined at exit.
//
// This is the one place the fork-joins meet, and what they share is a list of
// the calls that still have room for a thread, under a mutex: a call is listed
// by its caller, taken by the threads and unlisted by its caller, which then
// waits for the threads still inside. The work itself shares nothing here.
struct fork_join_group {
    static fork_join_group& instance() {
        static fork_join_group group;
        return group;
    }

    fork_join_group(fork_join_group const&) = delete;
    fork_join_group& operator=(fork_join_group const&) = delete;

    // Runs the call on this thread and on up to `helpers` threads of the group.
    void run(fork_join_call& call, size_t helpers) {
        if (helpers != 0) {
            {
                std::lock_guard const lock(mutex_);
                call.wanted = helpers;
                listed_.push_back(&call);
            }
            if (helpers == 1) {
                work_.notify_one();
            } else {
                work_.notify_all();
            }
        }
        call.drain();
        if (helpers == 0) {
            return;
        }
        // Every index is taken, or a call threw: no thread enters from here on,
        // and the ones inside finish the call they are running.
        std::unique_lock lock(mutex_);
        call.wanted = 0;
        std::erase(listed_, &call);
        done_.wait(lock, [&call] { return call.inside == 0; });
    }

private:
    fork_join_group() {
        // The caller of a fork-join is the other thread.
        size_t const threads = std::max<size_t>(2, std::thread::hardware_concurrency()) - 1;
        threads_.reserve(threads);
        for (size_t t = 0; t < threads; ++t) {
            try {
                threads_.emplace_back([this] { work(); });
            } catch (...) {
                // No thread to start: the callers do that share themselves.
                break;
            }
        }
    }

    ~fork_join_group() {
        {
            std::lock_guard const lock(mutex_);
            stopping_ = true;
        }
        work_.notify_all();
        for (auto& thread : threads_) {
            thread.join();
        }
    }

    void work() {
        std::unique_lock lock(mutex_);
        while (true) {
            work_.wait(lock, [this] { return stopping_ || ! listed_.empty(); });
            if (stopping_) {
                return;
            }
            auto& call = *listed_.front();
            if (--call.wanted == 0) {
                listed_.erase(listed_.begin());
            }
            ++call.inside;
            lock.unlock();
            call.drain();
            lock.lock();
            if (--call.inside == 0) {
                done_.notify_all();
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable work_;
    std::condition_variable done_;
    std::vector<fork_join_call*> listed_;
    std::vector<std::thread> threads_;
    bool stopping_ = false;
};
#endif

} // namespace detail

// Calls body(i) for every i in [0, count) and returns once every call has
// finished, so nothing outlives the call. The calling thread runs calls itself
// and is helped by a fixed group of threads that lives as long as the process:
// no thread is started or joined per call. The calls run concurrently, so each
// must only read shared state and write the output slot of its own index; the
// caller reads the slots after the return. Each call runs from start to end on
// one thread. Nothing is reported through ordering: a caller that wants the
// first failure scans its slots by index.
//
// The caller never waits for a thread to become free: it takes indices until
// none is left and then waits only for the calls already being run. A body that
// forks again therefore cannot wait for the group it is running on.
//
// False when a call threw; the calls not yet started are then skipped and the
// slots are not to be trusted.
template <typename Body>
[[nodiscard]]
bool fork_join_for(size_t count, Body const& body) {
    if (count == 0) {
        return true;
    }
    detail::fork_join_call call;
    call.count = count;
    call.body = std::addressof(body);
    call.invoke = [](void const* b, size_t i) { (*static_cast<Body const*>(b))(i); };

#if defined(KTH_BROWSER_NODE)
    call.drain();
#else
    size_t const helpers = fork_join_workers(count) - 1;
    if (helpers == 0) {
        call.drain();
    } else {
        detail::fork_join_group::instance().run(call, helpers);
    }
#endif
    return ! call.threw.load(std::memory_order_acquire);
}

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_FORK_JOIN_HPP
