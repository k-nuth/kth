// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/network/sessions/session_seed.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <kth/domain.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_ping_31402.hpp>
#include <kth/network/protocols/protocol_ping_60001.hpp>
#include <kth/network/protocols/protocol_reject_70002.hpp>
#include <kth/network/protocols/protocol_seed_31402.hpp>
#include <kth/network/protocols/protocol_version_31402.hpp>
#include <kth/network/protocols/protocol_version_70002.hpp>

namespace kth::network {

#define CLASS session_seed
#define NAME "session_seed"

/// If seeding occurs it must generate an increase hosts or will fail.
static size_t const minimum_host_increase = 1;

using namespace std::placeholders;
session_seed::session_seed(p2p& network)
    : session(network, false)
    , CONSTRUCT_TRACK(session_seed) {}

// Start sequence.
// ----------------------------------------------------------------------------

void session_seed::start(result_handler handler) {
    if (settings_.host_pool_capacity == 0) {
        spdlog::info("[network] Not configured to populate an address pool.");
        handler(error::success);
        return;
    }

    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_seed::handle_started(code const& ec, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    auto const start_size = address_count();

    if (start_size != 0) {
        spdlog::debug("[network] Seeding is not required because there are {} cached addresses.", start_size);
        handler(error::success);
        return;
    }

    if (settings_.seeds.empty()) {
        spdlog::error("[network] Seeding is required but no seeds are configured.");
        handler(error::operation_failed);
        return;
    }

    // This is NOT technically the end of the start sequence, since the handler
    // is not invoked until seeding operations are complete.
    start_seeding(start_size, handler);
}

void session_seed::attach_handshake_protocols(channel::ptr channel, result_handler handle_started) {
    // Don't use configured services or relay for seeding.
    auto const relay = false;
    auto const own_version = settings_.protocol_maximum;
    auto const own_services = domain::message::version::service::none;
    auto const invalid_services = settings_.invalid_services;
    auto const minimum_version = settings_.protocol_minimum;
    auto const minimum_services = domain::message::version::service::none;

    // Reject messages are not handled until bip61 (70002).
    // The negotiated_version is initialized to the configured maximum.
    if (channel->negotiated_version() >= domain::message::version::level::bip61) {
        attach<protocol_version_70002>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services, relay)
            ->start(handle_started);
    } else {
        attach<protocol_version_31402>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services)
            ->start(handle_started);
    }
}

// Seed sequence.
// ----------------------------------------------------------------------------

void session_seed::start_seeding(size_t start_size, result_handler handler) {
    auto const complete = BIND2(handle_complete, start_size, handler);
    auto const join_handler = synchronize(complete, settings_.seeds.size(), NAME, synchronizer_terminate::on_count);

    // We don't use parallel here because connect is itself asynchronous.
    for (auto const& seed : settings_.seeds) {
        start_seed(seed, join_handler);
    }
}

void session_seed::start_seed(infrastructure::config::endpoint const& seed, result_handler handler) {
    if (stopped()) {
        spdlog::debug("[network] Suspended seed connection");
        handler(error::channel_stopped);
        return;
    }

    spdlog::info("[network] Contacting seed [{}]", seed);

    auto const connector = create_connector();
    pend(connector);

    // OUTBOUND CONNECT
    connector->connect(seed, BIND5(handle_connect, _1, _2, seed, connector, handler));
}

void session_seed::handle_connect(code const& ec, channel::ptr channel, infrastructure::config::endpoint const& seed, connector::ptr connector, result_handler handler) {
    unpend(connector);

    if (ec) {
        spdlog::info("[network] Failure contacting seed [{}] {}", seed, ec.message());
        handler(ec);
        return;
    }

    if (blacklisted(channel->authority())) {
        spdlog::debug("[network] Seed [{}] on blacklisted address [{}]", seed, channel->authority());
        handler(error::address_blocked);
        return;
    }

    spdlog::info("[network] Connected seed [{}] as {}", seed, channel->authority());

    register_channel(channel,
        BIND3(handle_channel_start, _1, channel, handler),
        BIND1(handle_channel_stop, _1));
}

void session_seed::handle_channel_start(code const& ec, channel::ptr channel, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    attach_protocols(channel, handler);
}

void session_seed::attach_protocols(channel::ptr channel, result_handler handler) {
    auto const version = channel->negotiated_version();

    if (version >= domain::message::version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }

    if (version >= domain::message::version::level::bip61) {
        attach<protocol_reject_70002>(channel)->start();
    }

    attach<protocol_seed_31402>(channel)->start(handler);
}

void session_seed::handle_channel_stop(code const& ec) {
    spdlog::debug("[network] Seed channel stopped: {}", ec.message());
}

// This accepts no error code because individual seed errors are suppressed.
void session_seed::handle_complete(size_t start_size, result_handler handler) {
    // We succeed only if there is a host count increase.
    auto const increase = address_count() >= ceiling_add(start_size, minimum_host_increase);

    // This is the end of the seed sequence.
    handler(increase ? error::success : error::peer_throttling);
}

} // namespace kth::network