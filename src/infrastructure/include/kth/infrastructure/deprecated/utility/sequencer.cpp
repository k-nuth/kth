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

#include <kth/infrastructure/deprecated/utility/sequencer.hpp>

#include <utility>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

sequencer::sequencer(executor_type executor)
    : executor_(std::move(executor))
    , executing_(false)
{}

sequencer::~sequencer() {
    KTH_ASSERT_MSG(actions_.empty(), "sequencer not cleared");
}

void sequencer::lock(action&& handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////
    {
        unique_lock locker(mutex_);

        if (executing_) {
            actions_.push(std::move(handler));
            return;
        }
        executing_ = true;
    } //unlock()
    ///////////////////////////////////////////////////////////////////////

    ::asio::post(executor_, std::move(handler));
}


void sequencer::unlock() {
    using std::swap;

    action handler;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////
    {
        unique_lock locker(mutex_);

        KTH_ASSERT_MSG(executing_, "called unlock but sequence not locked");

        if (actions_.empty()) {
            executing_ = false;
            return;
        }

        executing_ = true;
        swap(handler, actions_.front());
        actions_.pop();
    } // unlock()
    ///////////////////////////////////////////////////////////////////////

    if (handler) {
        ::asio::post(executor_, std::move(handler));
    }
}

} // namespace kth
