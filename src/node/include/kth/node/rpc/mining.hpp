// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_MINING_HPP
#define KTH_NODE_RPC_MINING_HPP

#include <string>
#include <string_view>
#include <vector>

#include <kth/blockchain/pools/block_template.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/block.hpp>

// The pure (chain-free) logic behind the mining RPCs, split out so it can be
// unit-tested without a live block_chain: the handlers are just fetch/submit
// glue around these.

namespace kth::node::rpc {

// Serialize a mining template as the getblocktemplatelight "result" object
// (compact bits expanded to the 256-bit target, hashes in display order),
// tagged with `job_id`.
std::string render_mining_template(
    blockchain::mining_template const& tmpl, std::string_view job_id);

// Reassemble the full block a submitblocklight refers to: the miner's coinbase
// first, then the job's cached selection (CTOR order preserved).
domain::message::block assemble_block(
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs);

// Serialize a mining_info as the getmininginfo "result" object.
std::string render_mining_info(blockchain::mining_info const& info);

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_MINING_HPP
