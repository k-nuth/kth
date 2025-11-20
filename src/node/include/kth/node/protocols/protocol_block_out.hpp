// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_BLOCK_OUT_HPP
#define KTH_NODE_PROTOCOL_BLOCK_OUT_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

class full_node;

class KND_API protocol_block_out : public network::protocol_events, track<protocol_block_out> {
public:
    using ptr = std::shared_ptr<protocol_block_out>;

    /// Construct a block protocol instance.
    protocol_block_out(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    size_t locator_limit();

    void send_next_data(inventory_ptr inventory);
    void send_block(code const& ec, block_const_ptr message, size_t height, inventory_ptr inventory);
    void send_merkle_block(code const& ec, merkle_block_const_ptr message, size_t height, inventory_ptr inventory);
    void send_compact_block(code const& ec, compact_block_const_ptr message, size_t height, inventory_ptr inventory);

    bool handle_receive_get_data(code const& ec, get_data_const_ptr message);
    bool handle_receive_get_blocks(code const& ec, get_blocks_const_ptr message);
    bool handle_receive_get_headers(code const& ec, get_headers_const_ptr message);
    bool handle_receive_send_headers(code const& ec, send_headers_const_ptr message);
    bool handle_receive_send_compact(code const& ec, send_compact_const_ptr message);

    bool handle_receive_get_block_transactions(code const& ec,  get_block_transactions_const_ptr message);

    void handle_fetch_locator_hashes(code const& ec, inventory_ptr message);
    void handle_fetch_locator_headers(code const& ec, headers_ptr message);

    void handle_stop(code const& ec);
    void handle_send_next(code const& ec, inventory_ptr inventory);
    bool handle_reorganized(code ec, size_t fork_height, block_const_ptr_list_const_ptr incoming, block_const_ptr_list_const_ptr outgoing);

    // These are thread safe.
    full_node& node_;
    blockchain::safe_chain& chain_;
    kth::atomic<hash_digest> last_locator_top_;
    std::atomic<bool> compact_to_peer_;
    std::atomic<bool> headers_to_peer_;
    std::atomic<bool> compact_high_bandwidth_;
    std::atomic<uint64_t> compact_version_;
};

} // namespace kth::node

#endif
