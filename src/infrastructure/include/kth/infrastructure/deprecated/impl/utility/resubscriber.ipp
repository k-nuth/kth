// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// ============================================================================
// DEPRECATED - This file is scheduled for removal
// ============================================================================
// This file is part of the legacy P2P implementation that is being replaced
// by modern C++23 coroutines and Asio. See doc/asio.md for migration details.
//
// DO NOT USE THIS FILE IN NEW CODE.
// Replacement: Use p2p_node.hpp, peer_session.hpp, protocols_coro.hpp
// ============================================================================

#ifndef KTH_INFRASTRUCTURE_RESUBSCRIBER_IPP
#define KTH_INFRASTRUCTURE_RESUBSCRIBER_IPP

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/deprecated/utility/dispatcher.hpp>
#include <kth/infrastructure/utility/thread.hpp>
////#include <kth/infrastructure/utility/track.hpp>

namespace kth {

template <typename... Args>
resubscriber<Args...>::resubscriber(threadpool& pool,
    std::string const& class_name)
  : stopped_(true), dispatch_(pool, class_name)
    /*, track<resubscriber<Args...>>(class_name)*/
{
}

template <typename... Args>
resubscriber<Args...>::~resubscriber()
{
    KTH_ASSERT_MSG(subscriptions_.empty(), "resubscriber not cleared");
}

template <typename... Args>
void resubscriber<Args...>::start() {
#if ! defined(__EMSCRIPTEN__)
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    subscribe_mutex_.lock_upgrade();

    if (stopped_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        subscribe_mutex_.unlock_upgrade_and_lock();
        stopped_ = false;
        subscribe_mutex_.unlock();
        //---------------------------------------------------------------------
        return;
    }

    subscribe_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock<std::shared_mutex> lock(subscribe_mutex_);
        if ( ! stopped_) {
            return;
        }
    }

    std::unique_lock<std::shared_mutex> lock(subscribe_mutex_);
    stopped_ = false;
#endif
}

template <typename... Args>
void resubscriber<Args...>::stop() {
#if ! defined(__EMSCRIPTEN__)
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    subscribe_mutex_.lock_upgrade();

    if ( ! stopped_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        subscribe_mutex_.unlock_upgrade_and_lock();
        stopped_ = true;
        subscribe_mutex_.unlock();
        //---------------------------------------------------------------------
        return;
    }

    subscribe_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock<std::shared_mutex> lock(subscribe_mutex_);
        if (stopped_) {
            return;
        }
    }

    std::unique_lock<std::shared_mutex> lock(subscribe_mutex_);
    stopped_ = true;
#endif
}

template <typename... Args>
void resubscriber<Args...>::subscribe(handler&& notify, Args... stopped_args) {
#if ! defined(__EMSCRIPTEN__)
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    subscribe_mutex_.lock_upgrade();

    if ( ! stopped_) {
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
        subscribe_mutex_.unlock_upgrade_and_lock();
        subscriptions_.push_back(std::forward<handler>(notify));
        subscribe_mutex_.unlock();
        //---------------------------------------------------------------------
        return;
    }

    subscribe_mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
#else
    {
        std::shared_lock<std::shared_mutex> lock(subscribe_mutex_);
        if (stopped_) {
            return;
        }
    }
    std::unique_lock<std::shared_mutex> lock(subscribe_mutex_);
    subscriptions_.push_back(std::forward<handler>(notify));
#endif
    notify(stopped_args...);
}

template <typename... Args>
void resubscriber<Args...>::invoke(Args... args) {
    do_invoke(args...);
}

template <typename... Args>
void resubscriber<Args...>::relay(Args... args) {
    // This enqueues work while maintaining order.
    dispatch_.ordered(&resubscriber<Args...>::do_invoke, this->shared_from_this(), args...);
}

// private
template <typename... Args>
void resubscriber<Args...>::do_invoke(Args... args) {
#if ! defined(__EMSCRIPTEN__)
    // Critical Section (prevent concurrent handler execution)
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(invoke_mutex_);

    // Critical Section (protect stop)
    ///////////////////////////////////////////////////////////////////////////
    subscribe_mutex_.lock();

    // Move subscribers from the member list to a temporary list.
    list subscriptions;
    std::swap(subscriptions, subscriptions_);

    subscribe_mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    // Subscriptions may be created while this loop is executing.
    // Invoke subscribers from temporary list and resubscribe as indicated.
    for (auto const& handler: subscriptions) {
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // DEADLOCK RISK, handler must not return to invoke.
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        if (handler(args...)) {
            // Critical Section
            ///////////////////////////////////////////////////////////////////
            subscribe_mutex_.lock_upgrade();

            if (stopped_)
            {
                subscribe_mutex_.unlock_upgrade();
                //-------------------------------------------------------------
                continue;
            }

            subscribe_mutex_.unlock_upgrade_and_lock();
            //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
            subscriptions_.push_back(handler);

            subscribe_mutex_.unlock();
            ///////////////////////////////////////////////////////////////////
        }
    }
    ///////////////////////////////////////////////////////////////////////////
#else
    list subscriptions;
    {
        std::unique_lock lock(invoke_mutex_);
        std::shared_lock<std::shared_mutex> s_lock(subscribe_mutex_);

        // Move subscribers from the member list to a temporary list.
        std::swap(subscriptions, subscriptions_);
    }

    for (auto const& handler: subscriptions) {
        if (handler(args...)) {
            std::unique_lock<std::shared_mutex> u_lock(subscribe_mutex_);

            if (!stopped_) {
                subscriptions_.push_back(handler);
            }
        }
    }
#endif
}

} // namespace kth

#endif
