// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/connector.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define NAME "connector"

using namespace kth::config;
using namespace std::placeholders;

connector::connector(threadpool& pool, settings const& settings)
    : stopped_(false)
    , pool_(pool)
    , settings_(settings)
    , dispatch_(pool, NAME)
    , resolver_(pool.service())
    , CONSTRUCT_TRACK(connector)
{}

connector::~connector() {
    KTH_ASSERT_MSG(stopped(), "The connector was not stopped.");
}

void connector::stop(code const&) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if ( ! stopped()) {
        mutex_.unlock_upgrade_and_lock();
        //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

        // This will asynchronously invoke the handler of the pending resolve.
        resolver_.cancel();

        if (timer_) {
            timer_->stop();
        }

        stopped_ = true;
        //---------------------------------------------------------------------
        mutex_.unlock();
        return;
    }

    mutex_.unlock_upgrade();
    ///////////////////////////////////////////////////////////////////////////
}

// private
bool connector::stopped() const {
    return stopped_;
}

void connector::connect(infrastructure::config::endpoint const& endpoint, connect_handler handler) {
    connect(endpoint.host(), endpoint.port(), handler);
}

void connector::connect(infrastructure::config::authority const& authority, connect_handler handler) {
    connect(authority.to_hostname(), authority.port(), handler);
}

void connector::connect(std::string const& hostname, uint16_t port, connect_handler handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_upgrade();

    if (stopped()) {
        mutex_.unlock_upgrade();
        //---------------------------------------------------------------------
        dispatch_.concurrent(handler, error::service_stopped, nullptr);
        return;
    }

    mutex_.unlock_upgrade_and_lock();
    //+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    // async_resolve will not invoke the handler within this function.
    resolver_.async_resolve(hostname, std::to_string(port), std::bind(&connector::handle_resolve, shared_from_this(), _1, _2, handler));

    mutex_.unlock();
    ///////////////////////////////////////////////////////////////////////////
}

void connector::handle_resolve(boost_code const& ec, asio::results_type results, connect_handler handler) {
    using namespace ::asio;

    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_shared();

    if (stopped()) {
        mutex_.unlock_shared();
        //---------------------------------------------------------------------
        dispatch_.concurrent(handler, error::service_stopped, nullptr);
        return;
    }

    if (ec) {
        mutex_.unlock_shared();
        //---------------------------------------------------------------------
        dispatch_.concurrent(handler, error::resolve_failed, nullptr);
        return;
    }

    auto const socket = std::make_shared<kth::socket>(pool_);
    timer_ = std::make_shared<deadline>(pool_, settings_.connect_timeout());

    // Manage the timer-connect race, returning upon first completion.
    auto const join_handler = synchronize(handler, 1, NAME, synchronizer_terminate::on_error);

    // timer.async_wait will not invoke the handler within this function.
    timer_->start(std::bind(&connector::handle_timer, shared_from_this(), _1, socket, join_handler));

    // async_connect will not invoke the handler within this function.
    // The bound delegate ensures handler completion before loss of scope.
    async_connect(socket->get(), results, std::bind(&connector::handle_connect, shared_from_this(), _1, _2, socket, join_handler));

    mutex_.unlock_shared();
    ///////////////////////////////////////////////////////////////////////////
}

// private:
void connector::handle_connect(boost_code const& ec, asio::endpoint const&,
    socket::ptr socket, connect_handler handler) {
    if (ec) {
        handler(error::boost_to_error_code(ec), nullptr);
        return;
    }

    // Ensure that channel is not passed as an r-value.
    auto const created = std::make_shared<channel>(pool_, socket, settings_);
    handler(error::success, created);
}

// private:
void connector::handle_timer(code const& ec, socket::ptr socket,
    connect_handler handler) {
    handler(ec ? ec : error::channel_timeout, nullptr);
}

} // namespace kth::network
