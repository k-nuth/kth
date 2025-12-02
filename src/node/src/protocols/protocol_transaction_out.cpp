// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/protocols/protocol_transaction_out.hpp>

#include <cstddef>
#include <functional>
#include <memory>

#include <boost/range/adaptor/reversed.hpp>

#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/full_node.hpp>

namespace kth::node {

#define NAME "transaction_out"
#define CLASS protocol_transaction_out

using namespace kth::blockchain;
using namespace kth::domain::chain;
using namespace kth::database;
using namespace kth::domain::message;
using namespace kth::network;
using namespace boost::adaptors;
using namespace std::placeholders;

protocol_transaction_out::protocol_transaction_out(full_node& network, channel::ptr channel, safe_chain& chain)
    : protocol_events(network, channel, NAME)
    , chain_(chain)

    // TODO: move fee filter to a derived class protocol_transaction_out_70013.
    , minimum_peer_fee_(0)

    // TODO: move relay to a derived class protocol_transaction_out_70001.
    , relay_to_peer_(peer_version()->relay())

    , CONSTRUCT_TRACK(protocol_transaction_out)
{}

// TODO: move not_found to derived class protocol_transaction_out_70001.

// Start.
//-----------------------------------------------------------------------------

void protocol_transaction_out::start() {
    protocol_events::start(BIND1(handle_stop, _1));

    // TODO: move relay to a derived class protocol_transaction_out_70001.
    // Prior to this level transaction relay is not configurable.
    if (relay_to_peer_) {
        // Subscribe to transaction pool notifications and relay txs.
        chain_.subscribe_transaction(BIND2(handle_transaction_pool, _1, _2));
    }

    // TODO: move fee filter to a derived class protocol_transaction_out_70013.
    SUBSCRIBE2(fee_filter, handle_receive_fee_filter, _1, _2);

    // TODO: move memory pool to a derived class protocol_transaction_out_60002.
    SUBSCRIBE2(memory_pool, handle_receive_memory_pool, _1, _2);
    SUBSCRIBE2(get_data, handle_receive_get_data, _1, _2);
}

// Receive send_headers.
//-----------------------------------------------------------------------------

// TODO: move fee_filters to a derived class protocol_transaction_out_70013.
bool protocol_transaction_out::handle_receive_fee_filter(code const& ec, fee_filter_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    // TODO: move fee filter to a derived class protocol_transaction_out_70013.
    // Transaction annoucements will be filtered by fee amount.
    minimum_peer_fee_ = message->minimum_fee();

    // The fee filter may be adjusted.
    return true;
}

// Receive mempool sequence.
//-----------------------------------------------------------------------------

// TODO: move memory_pool to a derived class protocol_transaction_out_60002.
bool protocol_transaction_out::handle_receive_memory_pool(code const& ec, memory_pool_const_ptr) {
    if (stopped(ec)) {
        return false;
    }

    // The handler may be invoked *multiple times* by one blockchain call.
    // TODO: move multiple mempool inv to protocol_transaction_out_70002.
    // TODO: move fee filter to a derived class protocol_transaction_out_70013.
    chain_.fetch_mempool(max_inventory, minimum_peer_fee_, BIND2(handle_fetch_mempool, _1, _2));

    // Drop this subscription after the first request.
    return false;
}

// Each invocation is limited to 50000 vectors and invoked from common thread.
void protocol_transaction_out::handle_fetch_mempool(code const& ec, inventory_ptr message) {
    if (stopped(ec) || message->inventories().empty()) {
        return;
    }

    SEND2(*message, handle_send, _1, message->command);
}

// Receive get_data sequence.
//-----------------------------------------------------------------------------

// THIS SUPPORTS REQUEST OF CONFIRMED TRANSACTIONS.
// TODO: subscribe to and handle get_block_transactions message.
// TODO: expose a new service bit that indicates complete current tx history.
// This would exclude transctions replaced by duplication as per BIP30.
bool protocol_transaction_out::handle_receive_get_data(code const& ec, get_data_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (message->inventories().size() > max_get_data) {
        spdlog::warn("[node] Invalid get_data size ({}) from [{}]", message->inventories().size(), authority());
        stop(error::channel_stopped);
        return false;
    }

    // Create a copy because message is const because it is shared.
    auto const response = std::make_shared<inventory>();

    // Reverse copy the transaction elements of the const inventory.
    for (auto const inventory: reverse(message->inventories())) {
        if (inventory.is_transaction_type()) {
            response->inventories().push_back(inventory);
        }
    }

    send_next_data(response);
    return true;
}

void protocol_transaction_out::send_next_data(inventory_ptr inventory) {
    if (inventory->inventories().empty()) {
        return;
    }

    // The order is reversed so that we can pop from the back.
    auto const& entry = inventory->inventories().back();

    switch (entry.type()) {
        case inventory::type_id::transaction: {
            chain_.fetch_transaction(entry.hash(), false, BIND5(send_transaction, _1, _2, _3, _4, inventory));
            break;
        } default: {
            KTH_ASSERT_MSG(false, "improperly-filtered inventory");
        }
    }
}

// TODO: send block_transaction message as applicable.
void protocol_transaction_out::send_transaction(code const& ec, transaction_const_ptr message, size_t position, size_t /*height*/, inventory_ptr inventory) {
    if (stopped(ec)) {
        return;
    }

    // Treat already confirmed transactions as not found.
    auto confirmed = ! ec
                    && position != position_max
                    ;

    if (ec == error::not_found || confirmed) {
        spdlog::debug("[node] Transaction requested by [{}] not found.", authority());

        // TODO: move not_found to derived class protocol_block_out_70001.
        KTH_ASSERT( ! inventory->inventories().empty());
        not_found const reply{ inventory->inventories().back() };
        SEND2(reply, handle_send, _1, reply.command);
        handle_send_next(error::success, inventory);
        return;
    }

    if (ec) {
        spdlog::error("[node] Internal failure locating transaction requested by [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    SEND2(*message, handle_send_next, _1, inventory);
}

void protocol_transaction_out::handle_send_next(code const& ec, inventory_ptr inventory) {
    if (stopped(ec)) {
        return;
    }

    KTH_ASSERT( ! inventory->inventories().empty());
    inventory->inventories().pop_back();

    // Break off recursion.
    DISPATCH_CONCURRENT1(send_next_data, inventory);
}

// Subscription.
//-----------------------------------------------------------------------------

bool protocol_transaction_out::handle_transaction_pool(code const& ec, transaction_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::error("[node] Failure handling transaction notification: {}", ec.message());
        stop(ec);
        return false;
    }

    // TODO: make this a collection and send empty in this case.
    // Nothing to do, a channel is stopping but it's not this one.
    if ( ! message) {
        return true;
    }

    // Do not announce transactions to peer if too far behind.
    // Typically the tx would not validate anyway, but this is more consistent.
    if (chain_.is_stale()) {
        return true;
    }

    if (message->validation.originator == nonce()) {
        return true;
    }

    // TODO: move fee_filter to a derived class protocol_transaction_out_70013.
    if (message->fees() < minimum_peer_fee_) {
        return true;
    }

    inventory::type_id id;
    id = inventory::type_id::transaction;
    inventory const announce {{id, message->hash()}};
    SEND2(announce, handle_send, _1, announce.command);

    ////spdlog::debug("[node]
    ////] Announced tx [{}{}{}].", encode_hash(message->hash()), "] to ["
    ////, authority());
    return true;
}

void protocol_transaction_out::handle_stop(code const&) {
    chain_.unsubscribe();

    spdlog::debug("[network] Stopped transaction_out protocol for [{}].", authority());
}

} // namespace kth::node