// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sessions/session_manual.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_address_31402.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_ping_60001.hpp>
#include <kth/network/protocols/protocol_reject_70002.hpp>

namespace kth::network {

#define CLASS session_manual

using namespace std::placeholders;

session_manual::session_manual(p2p& network, bool notify_on_connect)
    : session(network, notify_on_connect)
    , CONSTRUCT_TRACK(session_manual) {}

// Start sequence.
// ----------------------------------------------------------------------------
// Manual connections are always enabled.
// Handshake pend not implemented for manual connections (connect to self ok).

void session_manual::start(result_handler handler) {
    spdlog::info("[network] Starting manual session.");

    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_manual::handle_started(code const& ec, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    // This is the end of the start sequence.
    handler(error::success);
}

// Connect sequence/cycle.
// ----------------------------------------------------------------------------

void session_manual::connect(std::string const& hostname, uint16_t port) {
    auto const unhandled = [](code, channel::ptr) {};
    connect(hostname, port, unhandled);
}

void session_manual::connect(std::string const& hostname, uint16_t port, channel_handler handler) {
    start_connect(error::success, hostname, port, settings_.manual_attempt_limit, handler);
}

// The first connect is a sequence, which then spawns a cycle.
void session_manual::start_connect(code const&, std::string const& hostname, uint16_t port, uint32_t attempts, channel_handler handler) {
    if (stopped()) {
        spdlog::debug("[network] Suspended manual connection.");
        handler(error::service_stopped, nullptr);
        return;
    }

    auto const retries = floor_subtract(attempts, 1u);
    auto const connector = create_connector();
    pend(connector);

    // MANUAL CONNECT OUTBOUND
    connector->connect(hostname, port, BIND7(handle_connect, _1, _2, hostname, port, retries, connector, handler));
}

void session_manual::handle_connect(code const& ec, channel::ptr channel, std::string const& hostname, uint16_t port, uint32_t remaining, connector::ptr connector, channel_handler handler) {
    unpend(connector);

    if (ec) {
        spdlog::warn("[network] Failure connecting [{}] manually: {}", infrastructure::config::endpoint(hostname, port), ec.message());

        // Retry forever if limit is zero.
        remaining = settings_.manual_attempt_limit == 0 ? 1 : remaining;

        if (remaining > 0) {
            // Retry with conditional delay in case of network error.
            dispatch_delayed(cycle_delay(ec), BIND5(start_connect, _1, hostname, port, remaining, handler));
            return;
        }

        spdlog::warn("[network] Suspending manual connection to [{}] after {} failed attempts.", infrastructure::config::endpoint(hostname, port), settings_.manual_attempt_limit);

        // This is the failure end of the connect sequence.
        handler(ec, nullptr);
        return;
    }

    register_channel(channel,
        BIND5(handle_channel_start, _1, hostname, port, channel, handler),
        BIND3(handle_channel_stop, _1, hostname, port));
}

void session_manual::handle_channel_start(code const& ec, std::string const& hostname, uint16_t port, channel::ptr channel, channel_handler handler) {
    // The start failure is also caught by handle_channel_stop.
    // Treat a start failure like a stop, but preserve the start handler.
    if (ec) {
        spdlog::info("[network] Manual channel failed to start [{}] {}", channel->authority(), ec.message());
        return;
    }

    spdlog::info("[network] Connected manual channel [{}] as [{}] ({})", infrastructure::config::endpoint(hostname, port), channel->authority(), connection_count());

    // This is the success end of the connect sequence.
    handler(error::success, channel);
    attach_protocols(channel);
}

void session_manual::attach_protocols(channel::ptr channel) {
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

void session_manual::handle_channel_stop(code const& ec, std::string const& hostname, uint16_t port) {
    spdlog::debug("[network] Manual channel stopped: {}", ec.message());

    // Special case for already connected, do not keep trying.
    // After a stop we don't use the caller's start handler, but keep connecting.
    if (ec != error::address_in_use) {
        connect(hostname, port);
    }
}

} // namespace kth::network