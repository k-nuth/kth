// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// ============================================================================
// DEPRECATED - This file is scheduled for removal
// ============================================================================
// This file is part of the legacy P2P implementation that is being replaced
// by modern C++23 coroutines and Asio. See doc/asio.md for migration details.
//
// DO NOT USE THIS FILE IN NEW CODE.
// Replacement: Use p2p_node.hpp, peer_session.hpp, protocols_coro.hpp
// ============================================================================

#include <kth/network/message_subscriber.hpp>

#include <istream>
#include <memory>
#include <string>
#include <kth/domain.hpp>

#define INITIALIZE_SUBSCRIBER(pool, value) value##_subscriber_(std::make_shared<value##_subscriber_type>(pool, #value "_sub"))
#define RELAY_CODE(code, value) value##_subscriber_->relay(code, {})

// This allows us to block the peer while handling the message.
#define CASE_HANDLE_MESSAGE(reader, version, value) \
    case message_type::value: \
        return handle<domain::message::value>(reader, version, value##_subscriber_)

#define CASE_RELAY_MESSAGE(reader, version, value) \
    case message_type::value: \
        return relay<domain::message::value>(reader, version, value##_subscriber_)

#define START_SUBSCRIBER(value) value##_subscriber_->start()

#define STOP_SUBSCRIBER(value) value##_subscriber_->stop()

namespace kth::network {

using namespace domain::message;

message_subscriber::message_subscriber(threadpool& pool)
    : INITIALIZE_SUBSCRIBER(pool, address)
    , INITIALIZE_SUBSCRIBER(pool, alert)
    , INITIALIZE_SUBSCRIBER(pool, block)
    , INITIALIZE_SUBSCRIBER(pool, block_transactions)
    , INITIALIZE_SUBSCRIBER(pool, compact_block)
    , INITIALIZE_SUBSCRIBER(pool, double_spend_proof)
    , INITIALIZE_SUBSCRIBER(pool, fee_filter)
    , INITIALIZE_SUBSCRIBER(pool, filter_add)
    , INITIALIZE_SUBSCRIBER(pool, filter_clear)
    , INITIALIZE_SUBSCRIBER(pool, filter_load)
    , INITIALIZE_SUBSCRIBER(pool, get_address)
    , INITIALIZE_SUBSCRIBER(pool, get_blocks)
    , INITIALIZE_SUBSCRIBER(pool, get_block_transactions)
    , INITIALIZE_SUBSCRIBER(pool, get_data)
    , INITIALIZE_SUBSCRIBER(pool, get_headers)
    , INITIALIZE_SUBSCRIBER(pool, headers)
    , INITIALIZE_SUBSCRIBER(pool, inventory)
    , INITIALIZE_SUBSCRIBER(pool, memory_pool)
    , INITIALIZE_SUBSCRIBER(pool, merkle_block)
    , INITIALIZE_SUBSCRIBER(pool, not_found)
    , INITIALIZE_SUBSCRIBER(pool, ping)
    , INITIALIZE_SUBSCRIBER(pool, pong)
    , INITIALIZE_SUBSCRIBER(pool, reject)
    , INITIALIZE_SUBSCRIBER(pool, send_compact)
    , INITIALIZE_SUBSCRIBER(pool, send_headers)
    , INITIALIZE_SUBSCRIBER(pool, transaction)
    , INITIALIZE_SUBSCRIBER(pool, verack)
    , INITIALIZE_SUBSCRIBER(pool, version)
    , INITIALIZE_SUBSCRIBER(pool, xversion)
    // , INITIALIZE_SUBSCRIBER(pool, xverack)
{}

void message_subscriber::broadcast(code const& ec) {
    RELAY_CODE(ec, address);
    RELAY_CODE(ec, alert);
    RELAY_CODE(ec, block);
    RELAY_CODE(ec, block_transactions);
    RELAY_CODE(ec, compact_block);
    RELAY_CODE(ec, double_spend_proof);
    RELAY_CODE(ec, fee_filter);
    RELAY_CODE(ec, filter_add);
    RELAY_CODE(ec, filter_clear);
    RELAY_CODE(ec, filter_load);
    RELAY_CODE(ec, get_address);
    RELAY_CODE(ec, get_blocks);
    RELAY_CODE(ec, get_block_transactions);
    RELAY_CODE(ec, get_data);
    RELAY_CODE(ec, get_headers);
    RELAY_CODE(ec, headers);
    RELAY_CODE(ec, inventory);
    RELAY_CODE(ec, memory_pool);
    RELAY_CODE(ec, merkle_block);
    RELAY_CODE(ec, not_found);
    RELAY_CODE(ec, ping);
    RELAY_CODE(ec, pong);
    RELAY_CODE(ec, reject);
    RELAY_CODE(ec, send_compact);
    RELAY_CODE(ec, send_headers);
    RELAY_CODE(ec, transaction);
    RELAY_CODE(ec, verack);
    RELAY_CODE(ec, version);
    RELAY_CODE(ec, xversion);
    // RELAY_CODE(ec, xverack);
}

code message_subscriber::load(message_type type, uint32_t version, byte_reader& reader) const {
    switch (type) {
        CASE_RELAY_MESSAGE(reader, version, address);
        CASE_RELAY_MESSAGE(reader, version, alert);
        CASE_HANDLE_MESSAGE(reader, version, block);
        CASE_RELAY_MESSAGE(reader, version, block_transactions);
        CASE_RELAY_MESSAGE(reader, version, compact_block);
        CASE_RELAY_MESSAGE(reader, version, double_spend_proof);
        CASE_RELAY_MESSAGE(reader, version, fee_filter);
        CASE_RELAY_MESSAGE(reader, version, filter_add);
        CASE_RELAY_MESSAGE(reader, version, filter_clear);
        CASE_RELAY_MESSAGE(reader, version, filter_load);
        CASE_RELAY_MESSAGE(reader, version, get_address);
        CASE_RELAY_MESSAGE(reader, version, get_blocks);
        CASE_RELAY_MESSAGE(reader, version, get_block_transactions);
        CASE_RELAY_MESSAGE(reader, version, get_data);
        CASE_RELAY_MESSAGE(reader, version, get_headers);
        CASE_RELAY_MESSAGE(reader, version, headers);
        CASE_RELAY_MESSAGE(reader, version, inventory);
        CASE_RELAY_MESSAGE(reader, version, memory_pool);
        CASE_RELAY_MESSAGE(reader, version, merkle_block);
        CASE_RELAY_MESSAGE(reader, version, not_found);
        CASE_RELAY_MESSAGE(reader, version, ping);
        CASE_RELAY_MESSAGE(reader, version, pong);
        CASE_RELAY_MESSAGE(reader, version, reject);
        CASE_RELAY_MESSAGE(reader, version, send_compact);
        CASE_RELAY_MESSAGE(reader, version, send_headers);
        CASE_HANDLE_MESSAGE(reader, version, transaction);
        CASE_HANDLE_MESSAGE(reader, version, verack);
        CASE_HANDLE_MESSAGE(reader, version, version);
        CASE_HANDLE_MESSAGE(reader, version, xversion);
        // CASE_HANDLE_MESSAGE(reader, version, xverack);
        case message_type::unknown:
        default:
            return error::not_found;
    }
}

void message_subscriber::start() {
    START_SUBSCRIBER(address);
    START_SUBSCRIBER(alert);
    START_SUBSCRIBER(block);
    START_SUBSCRIBER(block_transactions);
    START_SUBSCRIBER(compact_block);
    START_SUBSCRIBER(double_spend_proof);
    START_SUBSCRIBER(fee_filter);
    START_SUBSCRIBER(filter_add);
    START_SUBSCRIBER(filter_clear);
    START_SUBSCRIBER(filter_load);
    START_SUBSCRIBER(get_address);
    START_SUBSCRIBER(get_blocks);
    START_SUBSCRIBER(get_block_transactions);
    START_SUBSCRIBER(get_data);
    START_SUBSCRIBER(get_headers);
    START_SUBSCRIBER(headers);
    START_SUBSCRIBER(inventory);
    START_SUBSCRIBER(memory_pool);
    START_SUBSCRIBER(merkle_block);
    START_SUBSCRIBER(not_found);
    START_SUBSCRIBER(ping);
    START_SUBSCRIBER(pong);
    START_SUBSCRIBER(reject);
    START_SUBSCRIBER(send_compact);
    START_SUBSCRIBER(send_headers);
    START_SUBSCRIBER(transaction);
    START_SUBSCRIBER(verack);
    START_SUBSCRIBER(version);
    START_SUBSCRIBER(xversion);
    // START_SUBSCRIBER(xverack);
}

void message_subscriber::stop() {
    STOP_SUBSCRIBER(address);
    STOP_SUBSCRIBER(alert);
    STOP_SUBSCRIBER(block);
    STOP_SUBSCRIBER(block_transactions);
    STOP_SUBSCRIBER(compact_block);
    STOP_SUBSCRIBER(double_spend_proof);
    STOP_SUBSCRIBER(fee_filter);
    STOP_SUBSCRIBER(filter_add);
    STOP_SUBSCRIBER(filter_clear);
    STOP_SUBSCRIBER(filter_load);
    STOP_SUBSCRIBER(get_address);
    STOP_SUBSCRIBER(get_blocks);
    STOP_SUBSCRIBER(get_block_transactions);
    STOP_SUBSCRIBER(get_data);
    STOP_SUBSCRIBER(get_headers);
    STOP_SUBSCRIBER(headers);
    STOP_SUBSCRIBER(inventory);
    STOP_SUBSCRIBER(memory_pool);
    STOP_SUBSCRIBER(merkle_block);
    STOP_SUBSCRIBER(not_found);
    STOP_SUBSCRIBER(ping);
    STOP_SUBSCRIBER(pong);
    STOP_SUBSCRIBER(reject);
    STOP_SUBSCRIBER(send_compact);
    STOP_SUBSCRIBER(send_headers);
    STOP_SUBSCRIBER(transaction);
    STOP_SUBSCRIBER(verack);
    STOP_SUBSCRIBER(version);
    STOP_SUBSCRIBER(xversion);
    // STOP_SUBSCRIBER(xverack);
}

} // namespace kth::network
