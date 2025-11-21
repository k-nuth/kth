// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sessions/session.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <kth/domain.hpp>
#include <kth/network/acceptor.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/connector.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/proxy.hpp>
#include <kth/network/protocols/protocol_version_31402.hpp>
#include <kth/network/protocols/protocol_version_70002.hpp>
#include <kth/network/settings.hpp>

namespace kth::network {

#define CLASS session
#define NAME "session"

using namespace std::placeholders;

session::session(p2p& network, bool notify_on_connect)
    : pool_(network.thread_pool())
    , settings_(network.network_settings())
    , stopped_(true)
    , notify_on_connect_(notify_on_connect)
    , network_(network)
    , dispatch_(pool_, NAME) {}

session::~session() {
    KTH_ASSERT_MSG(stopped(), "The session was not stopped.");
}

// Properties.
// ----------------------------------------------------------------------------
// protected

size_t session::address_count() const {
    return network_.address_count();
}

size_t session::connection_count() const {
    return network_.connection_count();
}

code session::fetch_address(address& out_address) const {
    return network_.fetch_address(out_address);
}

bool session::blacklisted(authority const& authority) const {
    auto const ip_compare = [&](const infrastructure::config::authority& blocked) {
        return authority.ip() == blocked.ip();
    };

    auto const& list = settings_.blacklist;
    return std::any_of(list.begin(), list.end(), ip_compare);
}

bool session::stopped() const {
    return stopped_;
}

bool session::stopped(code const& ec) const {
    return stopped() || ec == error::service_stopped;
}

// Socket creators.
// ----------------------------------------------------------------------------

acceptor::ptr session::create_acceptor() {
    return std::make_shared<acceptor>(pool_, settings_);
}

connector::ptr session::create_connector() {
    return std::make_shared<connector>(pool_, settings_);
}

// Pending connect.
// ----------------------------------------------------------------------------

code session::pend(connector::ptr connector) {
    return network_.pend(connector);
}

void session::unpend(connector::ptr connector) {
    network_.unpend(connector);
}

// Pending handshake.
// ----------------------------------------------------------------------------

code session::pend(channel::ptr channel) {
    return network_.pend(channel);
}

void session::unpend(channel::ptr channel) {
    network_.unpend(channel);
}

bool session::pending(uint64_t version_nonce) const {
    return network_.pending(version_nonce);
}

// Start sequence.
// ----------------------------------------------------------------------------
// Must not change context before subscribing.

void session::start(result_handler handler) {
    if ( ! stopped()) {
        handler(error::operation_failed);
        return;
    }

    stopped_ = false;
    subscribe_stop(BIND1(handle_stop, _1));

    // This is the end of the start sequence.
    handler(error::success);
}

void session::handle_stop(code const& ec) {
    // This signals the session to stop creating connections, but does not
    // close the session. Channels stop, resulting in session loss of scope.
    stopped_ = true;
}

// Subscribe Stop.
// ----------------------------------------------------------------------------

void session::subscribe_stop(result_handler handler) {
    network_.subscribe_stop(handler);
}

// Registration sequence.
// ----------------------------------------------------------------------------
// Must not change context in start or stop sequences (use bind).

void session::register_channel(channel::ptr channel, result_handler handle_started, result_handler handle_stopped) {
    if (stopped()) {
        handle_started(error::service_stopped);
        handle_stopped(error::service_stopped);
        return;
    }

    start_channel(channel, BIND4(handle_start, _1, channel, handle_started, handle_stopped));
}

void session::start_channel(channel::ptr channel, result_handler handle_started) {
    channel->set_notify(notify_on_connect_);
    channel->set_nonce(pseudo_random_broken_do_not_use::next(1, max_uint64));

    // The channel starts, invokes the handler, then starts the read cycle.
    channel->start(BIND3(handle_starting, _1, channel, handle_started));
}

void session::handle_starting(code const& ec, channel::ptr channel, result_handler handle_started) {
    if (ec) {
        spdlog::debug("[network] Channel failed to start [{}] {}", channel->authority(), ec.message());
        handle_started(ec);
        return;
    }

    attach_handshake_protocols(channel, BIND3(handle_handshake, _1, channel, handle_started));
}

void session::attach_handshake_protocols(channel::ptr channel, result_handler handle_started) {
    // Reject messages are not handled until bip61 (70002).
    // The negotiated_version is initialized to the configured maximum.
    if (channel->negotiated_version() >= domain::message::version::level::bip61) {
        attach<protocol_version_70002>(channel)->start(handle_started);
    } else {
        attach<protocol_version_31402>(channel)->start(handle_started);
    }
}

void session::handle_handshake(code const& ec, channel::ptr channel, result_handler handle_started) {
    if (ec) {
        spdlog::debug("[network] Failure in handshake with [{}] {}", channel->authority(), ec.message());

        handle_started(ec);
        return;
    }

    handshake_complete(channel, handle_started);
}

void session::handshake_complete(channel::ptr channel, result_handler handle_started) {
    // This will fail if the IP address or nonce is already connected.
    handle_started(network_.store(channel));
}

void session::handle_start(code const& ec, channel::ptr channel, result_handler handle_started, result_handler handle_stopped) {
    // Must either stop or subscribe the channel for stop before returning.
    // All closures must eventually be invoked as otherwise it is a leak.
    // Therefore upon start failure expect start failure and stop callbacks.
    if (ec) {
        channel->stop(ec);
        handle_stopped(ec);
    } else {
        channel->subscribe_stop(BIND3(handle_remove, _1, channel, handle_stopped));
    }

    // This is the end of the registration sequence.
    handle_started(ec);
}

void session::handle_remove(code const& ec, channel::ptr channel, result_handler handle_stopped) {
    network_.remove(channel);
    handle_stopped(error::success);
}

} // namespace kth::network
