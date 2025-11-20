// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_TRANSACTION_OUT_HPP
#define KTH_NODE_PROTOCOL_TRANSACTION_OUT_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

class full_node;

class KND_API protocol_transaction_out : public network::protocol_events, track<protocol_transaction_out> {
public:
    using ptr = std::shared_ptr<protocol_transaction_out>;

    /// Construct a transaction protocol instance.
    protocol_transaction_out(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    void send_next_data(inventory_ptr inventory);
    void send_transaction(code const& ec, transaction_const_ptr message, size_t position, size_t height, inventory_ptr inventory);

    bool handle_receive_get_data(code const& ec, get_data_const_ptr message);
    bool handle_receive_fee_filter(code const& ec, fee_filter_const_ptr message);
    bool handle_receive_memory_pool(code const& ec, memory_pool_const_ptr message);
    void handle_fetch_mempool(code const& ec, inventory_ptr message);
    void handle_stop(code const& ec);
    void handle_send_next(code const& ec, inventory_ptr inventory);
    bool handle_transaction_pool(code const& ec, transaction_const_ptr message);

    // These are thread safe.
    blockchain::safe_chain& chain_;
    std::atomic<uint64_t> minimum_peer_fee_;
    bool const relay_to_peer_;
    // bool const enable_witness_;
};

} // namespace kth::node

#endif
