// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_PROTOCOL_double_spend_proof_OUT_HPP
#define KTH_NODE_PROTOCOL_double_spend_proof_OUT_HPP

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

class KND_API protocol_double_spend_proof_out : public network::protocol_events, track<protocol_double_spend_proof_out> {
public:
    using ptr = std::shared_ptr<protocol_double_spend_proof_out>;

    /// Construct a DSProof protocol instance.
    protocol_double_spend_proof_out(full_node& network, network::channel::ptr channel, blockchain::safe_chain& chain);

    /// Start the protocol.
    virtual void start();

private:
    void send_next_data(inventory_ptr inventory);
    void send_ds_proof(code const& ec, double_spend_proof_const_ptr message, inventory_ptr inventory);

    bool handle_receive_get_data(code const& ec, get_data_const_ptr message);
    void handle_stop(code const& ec);
    void handle_send_next(code const& ec, inventory_ptr inventory);
    bool handle_ds_proof_pool(code const& ec, double_spend_proof_const_ptr message);

    // These are thread safe.
    blockchain::safe_chain& chain_;
    bool const ds_proofs_enabled_;
};

} // namespace kth::node

#endif // KTH_NODE_PROTOCOL_double_spend_proof_OUT_HPP
