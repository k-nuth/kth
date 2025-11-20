// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_BLOCK_SYNC_HPP
#define KTH_NODE_PROTOCOL_BLOCK_SYNC_HPP

#include <cstddef>
#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>
#include <kth/node/utility/reservation.hpp>

namespace kth::node {

class full_node;

/// Blocks sync protocol, thread safe.
class KND_API protocol_block_sync : public network::protocol_timer, public track<protocol_block_sync> {
public:
    using ptr = std::shared_ptr<protocol_block_sync>;

    /// Construct a block sync protocol instance.
    protocol_block_sync(full_node& network, network::channel::ptr channel, reservation::ptr row);

    /// Start the protocol.
    virtual void start(event_handler handler);

private:
    void send_get_blocks(event_handler complete, bool reset);
    void handle_event(code const& ec, event_handler complete);
    void blocks_complete(code const& ec, event_handler handler);
    bool handle_receive_block(code const& ec, block_const_ptr message, event_handler complete);

    reservation::ptr reservation_;
};

} // namespace kth::node

#endif
