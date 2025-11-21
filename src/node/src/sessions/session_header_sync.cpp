// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sessions/session_header_sync.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/protocols/protocol_header_sync.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/settings.hpp>
#include <kth/node/utility/check_list.hpp>

namespace kth::node {

#define CLASS session_header_sync
#define NAME "session_header_sync"

using namespace kth::blockchain;
using namespace kth::domain::config;
using namespace kth::database;
using namespace kth::domain::message;
using namespace kth::network;
using namespace std::placeholders;

// The minimum rate back off factor, must be < 1.0.
static constexpr float back_off_factor = 0.75f;

// The starting minimum header download rate, exponentially backs off.
static constexpr uint32_t headers_per_second = 10000;

// Sort is required here but not in configuration settings.
session_header_sync::session_header_sync(full_node& network, check_list& hashes, fast_chain& blockchain, infrastructure::config::checkpoint::list const& checkpoints)
    : session<kth::network::session_outbound>(network, false)
    , hashes_(hashes)
    , minimum_rate_(headers_per_second)
    , chain_(blockchain)
    , checkpoints_(infrastructure::config::checkpoint::sort(checkpoints))
    , CONSTRUCT_TRACK(session_header_sync)
{
    static_assert(back_off_factor < 1.0, "invalid back-off factor");
}

// Start sequence.
// ----------------------------------------------------------------------------

void session_header_sync::start(result_handler handler) {
    session::start(CONCURRENT_DELEGATE2(handle_started, _1, handler));
}

void session_header_sync::handle_started(code const& ec, result_handler handler) {
    if (ec) {
        handler(ec);
        return;
    }

    // TODO: expose header count and emit here.
    spdlog::info("[node] Getting headers.");

    if ( ! initialize()) {
        handler(error::operation_failed);
        return;
    }

    auto const complete = synchronize(handler, headers_.size(), NAME);

    // This is the end of the start sequence.
    for (auto const row: headers_) {
        new_connection(row, complete);
    }
}

// Header sync sequence.
// ----------------------------------------------------------------------------

void session_header_sync::new_connection(header_list::ptr row, result_handler handler) {
    if (stopped()) {
        spdlog::debug("[node] Suspending header slot ({}).", row->slot());
        return;
    }

    spdlog::debug("[node] Starting header slot ({}).", row->slot());

    // HEADER SYNC CONNECT
    session_batch::connect(BIND4(handle_connect, _1, _2, row, handler));
}

void session_header_sync::handle_connect(code const& ec, channel::ptr channel, header_list::ptr row, result_handler handler) {
    if (ec) {
        spdlog::debug("[node] Failure connecting header slot ({}) {}", row->slot(), ec.message());
        new_connection(row ,handler);
        return;
    }

    spdlog::debug("[node] Connected header slot ({}) [{}]", row->slot(), channel->authority());

    register_channel(channel,
        BIND4(handle_channel_start, _1, channel, row, handler),
        BIND2(handle_channel_stop, _1, row));
}

void session_header_sync::attach_handshake_protocols(channel::ptr channel, result_handler handle_started) {
    // Don't use configured services, relay or min version for header sync.
    auto const relay = false;
    auto const own_version = settings_.protocol_maximum;
    auto const own_services = version::service::none;
    auto const invalid_services = settings_.invalid_services;
    auto const minimum_version = version::level::headers;
    auto const minimum_services = version::service::node_network;

    // The negotiated_version is initialized to the configured maximum.
    if (channel->negotiated_version() >= version::level::bip61) {
        attach<protocol_version_70002>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services, relay)
            ->start(handle_started);
    } else {
        attach<protocol_version_31402>(channel, own_version, own_services,
            invalid_services, minimum_version, minimum_services)
            ->start(handle_started);
    }
}

void session_header_sync::handle_channel_start(code const& ec, channel::ptr channel, header_list::ptr row, result_handler handler) {
    // Treat a start failure just like a completion failure.
    if (ec) {
        handle_complete(ec, row, handler);
        return;
    }

    attach_protocols(channel, row, handler);
}

void session_header_sync::attach_protocols(channel::ptr channel, header_list::ptr row, result_handler handler) {
    KTH_ASSERT(channel->negotiated_version() >= version::level::headers);

    if (channel->negotiated_version() >= version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }

    attach<protocol_address_31402>(channel)->start();
    attach<protocol_header_sync>(channel, row, minimum_rate_)->start(BIND3(handle_complete, _1, row, handler));
}

void session_header_sync::handle_complete(code const& ec, header_list::ptr row, result_handler handler) {
    if (ec) {
        // Reduce the rate minimum so that we don't get hung up.
        minimum_rate_ = static_cast<uint32_t>(minimum_rate_ * back_off_factor);

        // There is no failure scenario, we ignore the result code here.
        new_connection(row, handler);
        return;
    }

    //*************************************************************************
    // TODO: as each header sink slot completes store headers to database and
    // assign its checkpoints to hashes_, terminating the slot.
    //*************************************************************************

    auto height = row->first_height();
    auto const& headers = row->headers();

    // Store the hash if there is a gap reservation.
    for (auto const& header: headers) {
        hashes_.enqueue(header.hash(), height++);
    }

    spdlog::debug("[node] Completed header slot ({})", row->slot());

    // This is the end of the header sync sequence.
    handler(error::success);
}

void session_header_sync::handle_channel_stop(code const& ec, header_list::ptr row) {
    spdlog::debug("[node] Channel stopped on header slot ({}) {}", row->slot(), ec.message());
}

// Utility.
// ----------------------------------------------------------------------------

bool session_header_sync::initialize() {
    if ( ! hashes_.empty()) {
        spdlog::error("[node] Block hash list must not be initialized.");
        return false;
    }
    //*************************************************************************
    // TODO: get top and pair up checkpoints into slots.
    auto const& front = checkpoints_.front();
    auto const& back = checkpoints_.back();
    headers_.push_back(std::make_shared<header_list>(0, front, back));
    //*************************************************************************

    return true;
}

} // namespace kth::node