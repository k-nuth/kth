// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/utility/socket.hpp>

#include <memory>

#include <kth/infrastructure/config/authority.hpp>
#include <kth/infrastructure/utility/asio.hpp>
#include <kth/infrastructure/utility/thread.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

namespace kth {

socket::socket(threadpool& thread)
    : thread_(thread)
    , socket_(thread_.get_executor())
    /*, CONSTRUCT_TRACK(socket) */
{}

infrastructure::config::authority socket::authority() const {
    boost_code ec;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    mutex_.lock_shared();

    auto const endpoint = socket_.remote_endpoint(ec);

    mutex_.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////

    return ec ? infrastructure::config::authority() : infrastructure::config::authority(endpoint);
}

asio::socket& socket::get() {
    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    shared_lock lock(mutex_);

    return socket_;
    ///////////////////////////////////////////////////////////////////////////
}

void socket::stop() {
    // Handling socket error codes creates exception safety.
    boost_code ignore;

    ///////////////////////////////////////////////////////////////////////////
    // Critical Section.
    unique_lock lock(mutex_);

    // Signal the end of oustanding async socket functions (read).
    socket_.shutdown(asio::socket::shutdown_both, ignore);

    // BUGBUG: this is documented to fail on Windows XP and Server 2003.
    // DO NOT CLOSE SOCKET, IT TERMINATES WORK IMMEDIATELY RESULTING IN LEAKS
    socket_.cancel(ignore);
    ///////////////////////////////////////////////////////////////////////////
}

} // namespace kth
