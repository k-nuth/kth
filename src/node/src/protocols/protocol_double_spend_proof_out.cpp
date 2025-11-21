// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/protocols/protocol_double_spend_proof_out.hpp>

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

#define NAME "double_spend_proof_out"
#define CLASS protocol_double_spend_proof_out

using namespace kth::blockchain;
using namespace kth::domain::chain;
using namespace kth::database;
using namespace kth::domain::message;
using namespace kth::network;
using namespace boost::adaptors;
using namespace std::placeholders;

protocol_double_spend_proof_out::protocol_double_spend_proof_out(full_node& node, channel::ptr channel, safe_chain& chain)
    : protocol_events(node, channel, NAME)
    , chain_(chain)
    , ds_proofs_enabled_(node.node_settings().ds_proofs_enabled)
    , CONSTRUCT_TRACK(protocol_double_spend_proof_out)
{}

// Start.
//-----------------------------------------------------------------------------

void protocol_double_spend_proof_out::start() {
    protocol_events::start(BIND1(handle_stop, _1));

    // Prior to this level DSProof relay is not configurable.
    if (ds_proofs_enabled_) {
        // Subscribe to DSProof pool notifications and relay txs.
        chain_.subscribe_ds_proof(BIND2(handle_ds_proof_pool, _1, _2));
    }

    SUBSCRIBE2(get_data, handle_receive_get_data, _1, _2);
}

// Receive get_data sequence.
//-----------------------------------------------------------------------------

bool protocol_double_spend_proof_out::handle_receive_get_data(code const& ec, get_data_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if ( ! ds_proofs_enabled_) {
        spdlog::debug("[node] Got DSProof GetData but node.ds_proofs is disabled (settings) from [{}]", authority());
        return false;
    }

    if (message->inventories().size() > max_get_data) {
        spdlog::warn("[node] Invalid get_data size ({}) from [{}]", message->inventories().size(), authority());
        stop(error::channel_stopped);
        return false;
    }

    // Create a copy because message is const because it is shared.
    auto const response = std::make_shared<inventory>();

    // Reverse copy the DSProof elements of the const inventory.
    for (auto const inventory: reverse(message->inventories())) {
        if (inventory.is_double_spend_proof_type()) {
            response->inventories().push_back(inventory);
        }
    }

    send_next_data(response);
    return true;
}

void protocol_double_spend_proof_out::send_next_data(inventory_ptr inventory) {
    if (inventory->inventories().empty()) {
        return;
    }

    // The order is reversed so that we can pop from the back.
    auto const& entry = inventory->inventories().back();

    switch (entry.type()) {
        case inventory::type_id::double_spend_proof: {
            chain_.fetch_ds_proof(entry.hash(), BIND3(send_ds_proof, _1, _2, inventory));
            break;
        } default: {
            KTH_ASSERT_MSG(false, "improperly-filtered inventory");
        }
    }
}

void protocol_double_spend_proof_out::send_ds_proof(code const& ec, double_spend_proof_const_ptr message, inventory_ptr inventory) {
    if (stopped(ec)) {
        return;
    }

    if (ec == error::not_found) {
        spdlog::debug("[node] DSProof requested by [{}] not found.", authority());

        KTH_ASSERT( ! inventory->inventories().empty());
        const not_found reply{ inventory->inventories().back() };
        SEND2(reply, handle_send, _1, reply.command);
        handle_send_next(error::success, inventory);
        return;
    }

    if (ec) {
        spdlog::error("[node] Internal failure locating DSProof requested by [{}] {}", authority(), ec.message());
        stop(ec);
        return;
    }

    SEND2(*message, handle_send_next, _1, inventory);
}

void protocol_double_spend_proof_out::handle_send_next(code const& ec, inventory_ptr inventory) {
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

bool protocol_double_spend_proof_out::handle_ds_proof_pool(code const& ec, double_spend_proof_const_ptr message) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::error("[node] Failure handling DSProof notification: {}", ec.message());
        stop(ec);
        return false;
    }

    // TODO: make this a collection and send empty in this case.
    // Nothing to do, a channel is stopping but it's not this one.
    if ( ! message) {
        return true;
    }

    // Do not announce DSProofs to peer if too far behind.
    // Typically the tx would not validate anyway, but this is more consistent.
    if (chain_.is_stale()) {
        return true;
    }

    inventory::type_id id = inventory::type_id::double_spend_proof;
    inventory const announce {{id, message->hash()}};
    SEND2(announce, handle_send, _1, announce.command);

    ////spdlog::debug("[node]
    ////] Announced tx [{}{}{}].", encode_hash(message->hash()), "] to ["
    ////, authority());
    return true;
}

void protocol_double_spend_proof_out::handle_stop(code const&) {
    chain_.unsubscribe();

    spdlog::debug("[network] Stopped double_spend_proof_out protocol for [{}].", authority());
}

} // namespace kth::node