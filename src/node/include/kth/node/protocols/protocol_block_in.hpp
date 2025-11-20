// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_BLOCK_IN_HPP
#define KTH_NODE_PROTOCOL_BLOCK_IN_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <queue>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

struct temp_compact_block {
     domain::chain::header header;
     std::vector<domain::chain::transaction> transactions;
};

class full_node;

class KND_API protocol_block_in : public network::protocol_timer, track<protocol_block_in> {
public:
    using ptr = std::shared_ptr<protocol_block_in>;

    using compact_block_map = std::unordered_map<hash_digest, temp_compact_block>;
    /// Construct a block protocol instance.
    protocol_block_in(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    using hash_queue = std::queue<hash_digest>;


#if defined(KTH_STATISTICS_ENABLED)
    static
    void report(domain::chain::block const& block, full_node& node);
#else
    static
    void report(domain::chain::block const& block);
#endif

    void send_get_blocks(hash_digest const& stop_hash);
    void send_get_data(code const& ec, get_data_ptr message);

    bool handle_receive_block(code const& ec, block_const_ptr message);
    bool handle_receive_compact_block(code const& ec, compact_block_const_ptr message);
    bool handle_receive_block_transactions(code const& ec,block_transactions_const_ptr message);
    bool handle_receive_headers(code const& ec, headers_const_ptr message);
    bool handle_receive_inventory(code const& ec, inventory_const_ptr message);
    bool handle_receive_not_found(code const& ec, not_found_const_ptr message);
    void handle_store_block(code const& ec, block_const_ptr message);
    void handle_fetch_block_locator(code const& ec, get_headers_ptr message, hash_digest const& stop_hash);
    void handle_fetch_block_locator_compact_block(code const& ec, get_headers_ptr message, hash_digest const& stop_hash);

    void send_get_data_compact_block(code const& ec, hash_digest const& hash);

    void handle_timeout(code const& ec);
    void handle_stop(code const& ec);

    void organize_block(block_const_ptr message);

    // These are thread safe.
    full_node& node_;
    blockchain::safe_chain& chain_;
    const asio::duration block_latency_;
    bool const headers_from_peer_;
    bool const compact_from_peer_;
    bool const blocks_from_peer_;

    // This is protected by mutex.
    hash_queue backlog_;
    mutable upgrade_mutex mutex;

    compact_block_map compact_blocks_map_;

    bool compact_blocks_high_bandwidth_set_;

    // TODO(Mario): compact blocks version 1 hardcoded, change to 2 when segwit is implemented
};

} // namespace kth::node

#endif
