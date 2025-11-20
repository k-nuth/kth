// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_double_spend_proof_IN_HPP
#define KTH_NODE_PROTOCOL_double_spend_proof_IN_HPP

#include <cstdint>
#include <memory>
#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/define.hpp>

namespace kth::node {

class full_node;

class KND_API protocol_double_spend_proof_in : public network::protocol_events, track<protocol_double_spend_proof_in> {
public:
    using ptr = std::shared_ptr<protocol_double_spend_proof_in>;

    /// Construct a _double spend proofs protocol instance.
    protocol_double_spend_proof_in(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    void send_get_data(code const& ec, get_data_ptr message);

    bool handle_receive_inventory(code const& ec, inventory_const_ptr message);
    bool handle_receive_ds_proof_data(code const& ec, double_spend_proof_const_ptr message);
    void handle_store_ds_proof_data(code const& ec, double_spend_proof_const_ptr message);
    void handle_stop(code const&);

    // These are thread safe.
    blockchain::safe_chain& chain_;
    bool const ds_proofs_enabled_;
};

} // namespace kth::node

#endif // KTH_NODE_PROTOCOL_double_spend_proof_IN_HPP
