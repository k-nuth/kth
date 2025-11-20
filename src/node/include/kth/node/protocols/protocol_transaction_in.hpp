// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_TRANSACTION_IN_HPP
#define KTH_NODE_PROTOCOL_TRANSACTION_IN_HPP

#include <cstdint>
#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

class full_node;

class KND_API protocol_transaction_in : public network::protocol_events, track<protocol_transaction_in> {
public:
    using ptr = std::shared_ptr<protocol_transaction_in>;

    /// Construct a transaction protocol instance.
    protocol_transaction_in(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    void send_get_transactions(transaction_const_ptr message);
    void send_get_data(code const& ec, get_data_ptr message);

    bool handle_receive_inventory(code const& ec, inventory_const_ptr message);
    bool handle_receive_transaction(code const& ec, transaction_const_ptr message);
    void handle_store_transaction(code const& ec, transaction_const_ptr message);

    void handle_stop(code const&);

    // These are thread safe.
    blockchain::safe_chain& chain_;
    const uint64_t minimum_relay_fee_;
    bool const relay_from_peer_;
    bool const refresh_pool_;
};

} // namespace kth::node

#endif
