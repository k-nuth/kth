// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/acceptor.hpp>

#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "acceptor"

using namespace std::placeholders;

static auto const reuse_address = asio::acceptor::reuse_address(true);

acceptor::acceptor(threadpool& pool, settings const& settings)
    : stopped_(true)
    , pool_(pool)
    , settings_(settings)
    , dispatch_(pool, NAME)
    , acceptor_(pool_.get_executor())
    , CONSTRUCT_TRACK(acceptor) {}

acceptor::~acceptor() {
    KTH_ASSERT_MSG(stopped(), "The acceptor was not stopped.");
}

void acceptor::stop(code const&) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if ( ! stopped()) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

        // This will asynchronously invoke the handler of the pending accept.
        acceptor_.cancel();

        stopped_ = true;
        //---------------------------------------------------------------------
        mutex_.unlock();
        return;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
}

// private
bool acceptor::stopped() const {
    return stopped_;
}

// This is hardwired to listen on IPv6.
code acceptor::listen(uint16_t port) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if ( ! stopped()) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        return error::operation_failed;
    }

    boost_code error;
    asio::endpoint endpoint(settings_.use_ipv6 ? asio::tcp::v6() : asio::tcp::v4(), settings_.inbound_port);

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    acceptor_.open(endpoint.protocol(), error);

    if ( ! error) {
        acceptor_.set_option(reuse_address, error);
    }

    if ( ! error) {
        acceptor_.bind(endpoint, error);
    }

    if ( ! error) {
        acceptor_.listen(asio::max_connections, error);
    }

    stopped_ = false;

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////

    return error::boost_to_error_code(error);
}

void acceptor::accept(accept_handler handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped()) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        dispatch_.concurrent(handler, error::service_stopped, nullptr);
        return;
    }

    auto const socket = std::make_shared<kth::socket>(pool_);

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    // async_accept will not invoke the handler within this function.
    // The bound delegate ensures handler completion before loss of scope.
    // TODO: if the accept is invoked on a thread of the acceptor, as opposed
    // to the thread of the socket, then this is unnecessary.
    acceptor_.async_accept(socket->get(),
        std::bind(&acceptor::handle_accept, shared_from_this(), _1, socket, handler));

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////
}

// private:
void acceptor::handle_accept(boost_code const& ec, socket::ptr socket, accept_handler handler) {
    if (ec) {
        handler(error::boost_to_error_code(ec), nullptr);
        return;
    }

    // Ensure that channel is not passed as an r-value.
    auto const created = std::make_shared<channel>(pool_, socket, settings_);
    handler(error::success, created);
}

} // namespace kth::network
