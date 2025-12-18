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

#include <kth/network/protocols/protocol_reject_70002.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <kth/domain.hpp>
#include <kth/network/channel.hpp>
#include <kth/network/define.hpp>
#include <kth/network/p2p.hpp>
#include <kth/network/protocols/protocol_events.hpp>

namespace kth::network {

#define NAME "reject"
#define CLASS protocol_reject_70002

using namespace kd::message;
using namespace std::placeholders;

protocol_reject_70002::protocol_reject_70002(p2p& network, channel::ptr channel)
    : protocol_events(network, channel, NAME)
    , CONSTRUCT_TRACK(protocol_reject_70002) {}

// Start sequence.
// ----------------------------------------------------------------------------

void protocol_reject_70002::start() {
    protocol_events::start();
    SUBSCRIBE2(reject, handle_receive_reject, _1, _2);
}

// Protocol.
// ----------------------------------------------------------------------------

// TODO: mitigate log fill DOS.
bool protocol_reject_70002::handle_receive_reject(code const& ec, reject_const_ptr reject) {
    if (stopped(ec)) {
        return false;
    }

    if (ec) {
        spdlog::debug("[network] Failure receiving reject from [{}] {}", authority(), ec.message());
        stop(error::channel_stopped);
        return false;
    }

    auto const& message = reject->message();

    // Handle these in the version protocol.
    if (message == version::command) {
        return true;
    }

    std::string hash;
    if (message == block::command || message == transaction::command) {
        hash = " [" + encode_hash(reject->data()) + "].";
    }

    auto const code = reject->code();
    spdlog::debug("[network] Received {} reject ({}) from [{}] '{}'{}", message, uint16_t(code), authority(), reject->reason(), hash);
    return true;
}

} // namespace kth::network
