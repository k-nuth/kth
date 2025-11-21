// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sessions/session_inbound.hpp>

#include <cstddef>
#include <functional>
#include <kth/domain.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_address_31402.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_ping_60001.hpp>
#include <kth/network/protocols/protocol_reject_70002.hpp>

namespace kth::network {

#define CLASS session_inbound

using namespace std::placeholders;

session_inbound::session_inbound(p2p& network, bool notify_on_connect)
    : session(network, notify_on_connect)
    , connection_limit_(settings_.inbound_connections + settings_.outbound_connections + settings_.peers.size())
    , CONSTRUCT_TRACK(session_inbound) {}

// Start sequence.
// ----------------------------------------------------------------------------

void session_inbound::start(result_handler handler) {
    if (settings_.inbound_port == 0 || settings_.inbound_connections == 0) {
        spdlog::info("[network] Not configured for accepting incoming connections.");
        handler(error::success);
        return;
    }

    spdlog::info("[network] Starting inbound session on port ({}).", settings_.inbound_port);

    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_inbound::handle_started(code const& ec, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    acceptor_ = create_acceptor();

    // Relay stop to the acceptor.
    subscribe_stop(BIND1(handle_stop, _1));

    // START LISTENING ON PORT
    auto const error_code = acceptor_->listen(settings_.inbound_port);

    if (error_code) {
        spdlog::error("[network] Error starting listener: {}", ec.message());
        handler(error_code);
        return;
    }

    start_accept(error::success);

    // This is the end of the start sequence.
    handler(error::success);
}

void session_inbound::handle_stop(code const& ec) {
    // Signal the stop of listener/accept attempt.
    acceptor_->stop(ec);
}

// Accept sequence.
// ----------------------------------------------------------------------------

void session_inbound::start_accept(code const&) {
    if (stopped()) {
        spdlog::debug("[network] Suspended inbound connection.");
        return;
    }

    // ACCEPT THE NEXT INCOMING CONNECTION
    acceptor_->accept(BIND2(handle_accept, _1, _2));
}

void session_inbound::handle_accept(code const& ec, channel::ptr channel) {
    if (stopped(ec)) {
        spdlog::debug("[network] Suspended inbound connection.");
        return;
    }

    // Start accepting with conditional delay in case of network error.
    dispatch_delayed(cycle_delay(ec), BIND1(start_accept, _1));

    if (ec) {
        spdlog::debug("[network] Failure accepting connection: {}", ec.message());
        return;
    }

    if (blacklisted(channel->authority())) {
        spdlog::debug("[network] Rejected inbound connection from [{}] due to blacklisted address.", channel->authority());
        return;
    }

    // Inbound connections can easily overflow in the case where manual and/or
    // outbound connections at the time are not yet connected as configured.
    if (connection_count() >= connection_limit_) {
        spdlog::debug("[network] Rejected inbound connection from [{}] due to connection limit.", channel->authority());
        return;
    }

    register_channel(channel,
        BIND2(handle_channel_start, _1, channel),
        BIND1(handle_channel_stop, _1));
}

void session_inbound::handle_channel_start(code const& ec, channel::ptr channel) {
    if (ec) {
        spdlog::debug("[network] Inbound channel failed to start [{}] {}", channel->authority(), ec.message());
        return;
    }

    // Relegate to debug due to typical frequency.
    spdlog::debug("[network] Connected inbound channel [{}] ({})", channel->authority(), connection_count());

    attach_protocols(channel);
};

void session_inbound::attach_protocols(channel::ptr channel) {
    auto const version = channel->negotiated_version();

    if (version >= domain::message::version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }

    if (version >= domain::message::version::level::bip61) {
        attach<protocol_reject_70002>(channel)->start();
    }

    attach<protocol_address_31402>(channel)->start();
}

void session_inbound::handle_channel_stop(code const& ec) {
    spdlog::debug("[network] Inbound channel stopped: {}", ec.message());
}

// Channel start sequence.
// ----------------------------------------------------------------------------
// Check pending outbound connections for loopback to this inbound.

void session_inbound::handshake_complete(channel::ptr channel, result_handler handle_started) {
    if (pending(channel->peer_version()->nonce())) {
        spdlog::debug("[network] Rejected connection from [{}] as loopback.", channel->authority());
        handle_started(error::accept_failed);
        return;
    }

    session::handshake_complete(channel, handle_started);
}

} // namespace kth::network