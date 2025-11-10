// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/deadline.hpp>

#include <functional>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/thread.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/infrastructure/utility/threadpool.hpp>
#endif

#include <utility>

namespace kth {

using std::placeholders::_1;

// The timer closure captures an instance of this class and the callback.
// Deadline is guaranteed to call handler exactly once unless canceled/reset.

#if ! defined(__EMSCRIPTEN__)
deadline::deadline(threadpool& pool)
    : duration_(asio::seconds(0))
    , timer_(pool.service())
    /*, CONSTRUCT_TRACK(deadline)*/
{}

deadline::deadline(threadpool& pool, asio::duration duration)
    : duration_(duration)
    , timer_(pool.service())
    /*, CONSTRUCT_TRACK(deadline)*/
{}
#endif

void deadline::start(handler handle) {
    start(std::move(handle), duration_);
}

void deadline::start(handler handle, asio::duration duration) {
    auto const timer_handler = std::bind(&deadline::handle_timer, shared_from_this(), _1, handle);

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);

    // Handling socket error codes creates exception safety.
    timer_.cancel();
    timer_.expires_after(duration);

    // async_wait will not invoke the handler within this function.
    timer_.async_wait(timer_handler);
    ///////////////////////////////////////////////////////////////////////////
}

// Cancellation calls handle_timer with asio::error::operation_aborted.
// We do not handle the cancelation result code, which will return success
// in the case of a race in which the timer is already canceled.
void deadline::stop() {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    unique_lock lock(mutex_);

    // Handling socket error codes creates exception safety.
    timer_.cancel();
    ///////////////////////////////////////////////////////////////////////////
}

// If the timer expires the callback is fired with a success code.
// If the timer fails the callback is fired with the normalized error code.
// If the timer is canceled no call is made.
void deadline::handle_timer(boost_code const& ec, handler const& handle) const {
    if ( ! ec) {
        handle(error::success);
    } else if (ec != asio::error::operation_aborted) {
        handle(error::boost_to_error_code(ec));
    }
}

} // namespace kth
