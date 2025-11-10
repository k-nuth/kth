// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/sequencer.hpp>

#include <utility>

#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/thread.hpp>

namespace kth {

sequencer::sequencer(asio::context& service)
    : service_(service), executing_(false)
{}

sequencer::~sequencer() {
    KTH_ASSERT_MSG(actions_.empty(), "sequencer not cleared");
}

// void sequencer::lock(action&& handler) {
//     auto post = false;

//     // Critical Section
//     ///////////////////////////////////////////////////////////////////////
//     mutex_.lock();

//     if (executing_) {
//         actions_.push(std::move(handler));
//     } else {
//         post = true;
//         executing_ = true;
//     }

//     mutex_.unlock();
//     ///////////////////////////////////////////////////////////////////////

//     if (post) {
//         ::asio::post(service_, std::move(handler));
//     }
// }

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

    ::asio::post(service_, std::move(handler));
}


void sequencer::unlock() {
    using std::swap;

    action handler;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////
    // mutex_.lock();
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
        ::asio::post(service_, std::move(handler));
    }
}

} // namespace kth
