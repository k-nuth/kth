// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/threadpool.hpp>

// #include <iostream>
#include <thread>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

threadpool::threadpool(std::string const& name, size_t number_threads, thread_priority priority)
    : name_(name)
    , size_(0)
{
    // std::println("threadpool::threadpool() - name: {} - thread id: {}", name_, std::this_thread::get_id());
    spawn(number_threads, priority);
}

threadpool::~threadpool() {
    // std::println("threadpool::~threadpool() - name: {} - thread id: {}", name_, std::this_thread::get_id());
    shutdown();
    join();
}

// Should not be called during spawn.
bool threadpool::empty() const {
    return size() != 0;
}

// Should not be called during spawn.
size_t threadpool::size() const {
    return size_.load();
}

// This is not thread safe.
void threadpool::spawn(size_t number_threads, thread_priority priority) {
    // This allows the pool to be restarted.
    service_.reset();
    // std::println("threadpool::spawn() - name: {} - thread id: {}", name_, std::this_thread::get_id());

    for (size_t i = 0; i < number_threads; ++i) {
        spawn_once(priority);
    }
}

void threadpool::spawn_once(thread_priority priority) {
    // std::println("threadpool::spawn_once() - name: {} - thread id: {}", name_, std::this_thread::get_id());

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    work_mutex_.lock_upgrade();

    // Work prevents the service from running out of work and terminating.
    if ( ! work_) {
        work_mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        work_ = std::make_shared<asio::service::work>(service_);

        work_mutex_.unlock_and_lock_upgrade();
        //-----------------------------------------------------------------
    }

    work_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(threads_mutex_);

    threads_.emplace_back([this, priority]() {
        set_priority(priority);
        // std::println("threadpool::spawn_once() *** BEFORE run() *** - name: {} - thread id: {}", name_, std::this_thread::get_id());
        service_.run();
        // std::println("threadpool::spawn_once() *** AFTER  run() *** - name: {} - thread id: {}", name_, std::this_thread::get_id());
    });

    ++size_;
    ///////////////////////////////////////////////////////////////////////////
}

void threadpool::abort() {
    // std::println("threadpool::abort() *** BEFORE stop *** - name: {} - thread id: {}", name_, std::this_thread::get_id());
    service_.stop();
    // std::println("threadpool::abort() *** AFTER stop *** - name: {} - thread id: {}", name_, std::this_thread::get_id());
}

void threadpool::shutdown() {
    abort();
    // std::println("threadpool::shutdown() *** BEFORE lock *** - name: {} - thread id: {}", name_, std::this_thread::get_id());

    // {
    // ///////////////////////////////////////////////////////////////////////////
    // // Critical Section
    // unique_lock lock(work_mutex_);

    // work_.reset();
    // ///////////////////////////////////////////////////////////////////////////
    // }
    // std::println("threadpool::shutdown() *** AFTER lock *** - name: {} - thread id: {}", name_, std::this_thread::get_id());
}

void threadpool::join() {
    // std::println("threadpool::join() *** BEFORE lock *** - name: {} - thread id: {}", name_, std::this_thread::get_id());

    {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section
    unique_lock lock(threads_mutex_);

    DEBUG_ONLY(auto const this_id = boost::this_thread::get_id();)
    // auto const this_id = boost::this_thread::get_id();

    // for (auto& thread: threads_) {
    //     KTH_ASSERT(this_id != thread.get_id());
    //     KTH_ASSERT(thread.joinable());

    //     std::println("threadpool::join() - this_id != thread.get_id(): {}", this_id != thread.get_id());
    //     std::println("threadpool::join() - thread.joinable(): {} - name: {} - thread id: {}", thread.joinable(), name_, thread.get_id());

    //     std::println("threadpool::join() *** BEFORE join *** - name: {} - thread id: {}", name_, thread.get_id());
    //     thread.join();
    //     std::println("threadpool::join() *** AFTER  join *** - name: {} - thread id: {}", name_, thread.get_id());
    // }

    for (auto i = threads_.rbegin(); i != threads_.rend(); ++i ) {
        auto& thread = *i;

        KTH_ASSERT(this_id != thread.get_id());
        KTH_ASSERT(thread.joinable());

        // std::println("threadpool::join() - this_id != thread.get_id(): {}", this_id != thread.get_id());
        // std::println("threadpool::join() - thread.joinable(): {} - name: {} - thread id: {}", thread.joinable(), name_, thread.get_id());

        // std::println("threadpool::join() *** BEFORE join *** - name: {} - thread id: {}", name_, thread.get_id());
        // thread.join();
        // std::println("threadpool::join() *** AFTER  join *** - name: {} - thread id: {}", name_, thread.get_id());

        // std::println("threadpool::join() *** BEFORE detach *** - name: {} - thread id: {}", name_, thread.get_id());
        thread.detach();
        // std::println("threadpool::join() *** AFTER  detach *** - name: {} - thread id: {}", name_, thread.get_id());
    }

    threads_.clear();
    size_.store(0);
    ///////////////////////////////////////////////////////////////////////////
    }

    // std::println("threadpool::join() *** AFTER lock *** - name: {} - thread id: {}", name_, std::this_thread::get_id());

}

asio::service& threadpool::service() {
    return service_;
}

const asio::service& threadpool::service() const {
    return service_;
}

} // namespace kth
