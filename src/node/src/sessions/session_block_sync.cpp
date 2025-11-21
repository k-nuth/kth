// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sessions/session_block_sync.hpp>

#include <cstddef>
#include <memory>

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/protocols/protocol_block_sync.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/utility/check_list.hpp>
#include <kth/node/utility/reservation.hpp>

namespace kth::node {

#define CLASS session_block_sync
#define NAME "session_block_sync"

using namespace kth::blockchain;
using namespace kth::domain::config;
using namespace kth::domain::message;
using namespace kth::network;
using namespace std::placeholders;

// The interval in which all-channel block download performance is tested.
static const asio::seconds regulator_interval(5);

session_block_sync::session_block_sync(full_node& network, check_list& hashes, fast_chain& chain, settings const& settings)
    : session<kth::network::session_outbound>(network, false)
    , chain_(chain)
    , reservations_(hashes, chain, settings)
    , CONSTRUCT_TRACK(session_block_sync)
{}

// Start sequence.
// ----------------------------------------------------------------------------

void session_block_sync::start(result_handler handler) {
    // TODO: create session_timer base class and pass interval via start.
    timer_ = std::make_shared<deadline>(pool_, regulator_interval);
    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_block_sync::handle_started(code const& ec, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    // TODO: expose block count from reservations and emit here.
    spdlog::info("[node] Getting blocks.");

    // Copy the reservations table.
    auto const table = reservations_.table();

    if (table.empty()) {
        handler(error::success);
        return;
    }

    if ( ! reservations_.start()) {
        spdlog::debug("[node] Failed to set write lock.");
        handler(error::operation_failed);
        return;
    }

    auto const complete = synchronize<result_handler>(BIND2(handle_complete, _1, handler), table.size(), NAME);

    // This is the end of the start sequence.
    for (auto const row : table) {
        new_connection(row, complete);
    }

    ////reset_timer();
}

// Block sync sequence.
// ----------------------------------------------------------------------------

void session_block_sync::new_connection(reservation::ptr row, result_handler handler) {
    if (stopped()) {
        spdlog::debug("[node] Suspending block slot ({}).", row ->slot());
        return;
    }

    spdlog::debug("[node] Starting block slot ({}).", row->slot());

    // BLOCK SYNC CONNECT
    session_batch::connect(BIND4(handle_connect, _1, _2, row, handler));
}

void session_block_sync::handle_connect(code const& ec, channel::ptr channel, reservation::ptr row, result_handler handler) {
    if (ec) {
        spdlog::debug("[node] Failure connecting block slot ({}) {}", row->slot(), ec.message());
        new_connection(row, handler);
        return;
    }

    spdlog::debug("[node] Connected block slot ({}) [{}]", row->slot(), channel->authority());

    register_channel(channel,
        BIND4(handle_channel_start, _1, channel, row, handler),
        BIND2(handle_channel_stop, _1, row));
}

void session_block_sync::attach_handshake_protocols(channel::ptr channel, result_handler handle_started) {
    // Don't use configured services or relay for block sync.
    auto const relay = false;
    auto const own_version = settings_.protocol_maximum;
    auto const own_services = version::service::none;
    auto const invalid_services = settings_.invalid_services;
    auto const minimum_version = settings_.protocol_minimum;
    auto const minimum_services = version::service::node_network;

    // The negotiated_version is initialized to the configured maximum.
    if (channel->negotiated_version() >= version::level::bip61) {
        attach<protocol_version_70002>(channel, own_version, own_services, invalid_services, minimum_version, minimum_services, relay) ->start(handle_started);
    } else {
        attach<protocol_version_31402>(channel, own_version, own_services, invalid_services, minimum_version, minimum_services)->start(handle_started);
    }
}

void session_block_sync::handle_channel_start(code const& ec, channel::ptr channel, reservation::ptr row, result_handler handler) {
    // Treat a start failure just like a completion failure.
    if (ec) {
        handle_channel_complete(ec, row, handler);
        return;
    }

    attach_protocols(channel, row, handler);
}

void session_block_sync::attach_protocols(channel::ptr channel, reservation::ptr row, result_handler handler) {
    if (channel->negotiated_version() >= version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }

    attach<protocol_address_31402>(channel)->start();
    attach<protocol_block_sync>(channel, row)->start(BIND3(handle_channel_complete, _1, row, handler));
}

void session_block_sync::handle_channel_complete(code const& ec, reservation::ptr row, result_handler handler) {
    if (ec) {
        // There is no failure scenario, we ignore the result code here.
        new_connection(row, handler);
        return;
    }

    timer_->stop();
    reservations_.remove(row);

    spdlog::debug("[node] Completed block slot ({})", row->slot());

    // This is the end of the block sync sequence.
    handler(error::success);
}

void session_block_sync::handle_channel_stop(code const& ec, reservation::ptr row) {
    spdlog::info("[node] Channel stopped on block slot ({}) {}", row->slot(), ec.message());
}

void session_block_sync::handle_complete(code const& ec, result_handler handler) {
    // Always stop but give sync priority over stop for reporting.
    auto const stop = reservations_.stop();

    if (ec) {
        spdlog::debug("[node] Failed to complete block sync: {}", ec.message());
        handler(ec);
        return;
    }

    if ( ! stop) {
        spdlog::debug("[node] Failed to reset write lock: {}", ec.message());
        handler(error::operation_failed);
        return;
    }

    spdlog::debug("[node] Completed block sync.");
    handler(ec);
}

// Timer.
// ----------------------------------------------------------------------------

// private:
void session_block_sync::reset_timer() {
    if (stopped()) {
        return;
    }

    timer_->start(BIND1(handle_timer, _1));
}

void session_block_sync::handle_timer(code const& ec) {
    if (stopped()) {
        return;
    }

    spdlog::debug("[node] Fired session_block_sync timer: {}", ec.message());

    ////// TODO: If (total database time as a fn of total time) add a channel.
    ////// TODO: push into reservations_ implementation.
    ////// TODO: add a boolean increment method to the synchronizer and pass here.
    ////size_t const id = reservations_.table().size();
    ////auto const row = std::make_shared<reservation>(reservations_, id);
    ////const synchronizer<result_handler> handler({}, 0, "name", true);
    ////if (add) new_connection(row, handler);
    ////// TODO: drop the slowest channel
    //////If (drop) reservations_.prune();

    reset_timer();
}

} // namespace kth::node