// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/protocols/protocol_block_sync.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/full_node.hpp>
#include <kth/node/utility/reservation.hpp>

namespace kth::node {

#define NAME "block_sync"
#define CLASS protocol_block_sync

using namespace kth::domain::message;
using namespace kth::network;
using namespace std::placeholders;

// The interval in which block download rate is tested.
static const asio::seconds expiry_interval(5);

// Depends on protocol_header_sync, which requires protocol version 31800.
protocol_block_sync::protocol_block_sync(full_node& network, channel::ptr channel, reservation::ptr row)
    : protocol_timer(network, channel, true, NAME)
    , reservation_(row)
    , CONSTRUCT_TRACK(protocol_block_sync)
{}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_block_sync::start(event_handler handler) {
    auto const complete = synchronize<event_handler>(BIND2(blocks_complete, _1, handler), 1, NAME);

    protocol_timer::start(expiry_interval, BIND2(handle_event, _1, complete));

    SUBSCRIBE3(block, handle_receive_block, _1, _2, complete);

    // This is the end of the start sequence.
    send_get_blocks(complete, true);
}

// Peer sync sequence.
// ----------------------------------------------------------------------------

void protocol_block_sync::send_get_blocks(event_handler complete, bool reset) {
    if (stopped()) {
        return;
    }

    if (reservation_->stopped()) {
        spdlog::debug("[node] Stopping complete slot ({}).", reservation_->slot());
        complete(error::success);
        return;
    }

    // We may be a new channel (reset) or may have a new packet.
    auto const request = reservation_->request(reset);

    // Or we may be the same channel and with hashes already requested.
    if (request.inventories().empty()) {
        return;
    }

    spdlog::debug("[node] Sending request of {} hashes for slot ({}).", request.inventories().size(), reservation_->slot());

    SEND2(request, handle_send, _1, request.command);
}

// The message subscriber implements an optimization to bypass queueing of
// block messages. This requires that this handler never call back into the
// subscriber. Otherwise a deadlock will result. This in turn requires that
// the 'complete' parameter handler never call into the message subscriber.
bool protocol_block_sync::handle_receive_block(code const& ec, block_const_ptr message, event_handler complete) {
    if (stopped(ec)) {
        return false;
    }

    spdlog::info("[node] protocol_block_sync::handle_receive_block ***********************************************************");

#if ! defined(KTH_DB_READONLY)
    // Add the block to the blockchain store.
    reservation_->import(message);
#endif

    if (reservation_->toggle_partitioned()) {
        spdlog::debug("[node] Restarting partitioned slot ({}).", reservation_->slot());
        complete(error::channel_stopped);
        return false;
    }

    // Request more blocks if our reservation has been expanded.
    send_get_blocks(complete, false);
    return true;
}

// This is fired by the base timer and stop handler.
void protocol_block_sync::handle_event(code const& ec, event_handler complete) {
    if (stopped(ec)) {
        return;
    }

    if (ec && ec != error::channel_timeout) {
        spdlog::debug("[node] Failure in block sync timer for slot ({}) {}", reservation_->slot(), ec.message());
        complete(ec);
        return;
    }

    // This results from other channels taking this channel's hashes in
    // combination with this channel's peer not responding to the last request.
    // Causing a successful stop here prevents channel startup just to stop.
    if (reservation_->stopped()) {
        spdlog::debug("[node] Stopping complete slot ({}).", reservation_->slot());
        complete(error::success);
        return;
    }

    if (reservation_->expired()) {
        spdlog::debug("[node] Restarting slow slot ({})", reservation_->slot());
        complete(error::channel_timeout);
        return;
    }
}

void protocol_block_sync::blocks_complete(code const& ec, event_handler handler) {
    // We are no longer receiving blocks, so exclude from average.
    reservation_->reset();

    // This is the end of the peer sync sequence.
    // If there is no error the hash list must be empty and remain so.
    handler(ec);

    // The session does not need to handle the stop.
    stop(error::channel_stopped);
}

} // namespace kth::node