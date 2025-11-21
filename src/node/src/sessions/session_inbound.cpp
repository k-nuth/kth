// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sessions/session_inbound.hpp>

#include <kth/blockchain.hpp>
#if ! defined(__EMSCRIPTEN__)
#include <kth/network.hpp>
#endif
#include <kth/node/full_node.hpp>
#include <kth/node/protocols/protocol_block_in.hpp>
#include <kth/node/protocols/protocol_block_out.hpp>
#include <kth/node/protocols/protocol_double_spend_proof_in.hpp>
#include <kth/node/protocols/protocol_double_spend_proof_out.hpp>
#include <kth/node/protocols/protocol_transaction_in.hpp>
#include <kth/node/protocols/protocol_transaction_out.hpp>

namespace kth::node {

using namespace kth::blockchain;
using namespace kth::domain::message;
using namespace kth::network;
using namespace std::placeholders;

session_inbound::session_inbound(full_node& network, safe_chain& chain)
    : session<network::session_inbound>(network, true)
    , chain_(chain)
    , CONSTRUCT_TRACK(node::session_inbound)
{
    spdlog::info("[node] Starting inbound session on port ({}).", network.network_settings().inbound_port);
}

void session_inbound::attach_protocols(channel::ptr channel) {
    auto const version = channel->negotiated_version();

    if (version >= version::level::bip31) {
        attach<protocol_ping_60001>(channel)->start();
    } else {
        attach<protocol_ping_31402>(channel)->start();
    }

    if (version >= domain::message::version::level::bip61) {
        attach<protocol_reject_70002>(channel)->start();
    }

    attach<protocol_address_31402>(channel)->start();
    attach<protocol_block_in>(channel, chain_)->start();
    attach<protocol_block_out>(channel, chain_)->start();
    attach<protocol_double_spend_proof_in>(channel, chain_)->start();
    attach<protocol_double_spend_proof_out>(channel, chain_)->start();
    attach<protocol_transaction_in>(channel, chain_)->start();
    attach<protocol_transaction_out>(channel, chain_)->start();
}

} // namespace kth::node
