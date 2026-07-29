// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_MINING_BLOCK_SUBMIT_HPP
#define KTH_NODE_MINING_BLOCK_SUBMIT_HPP

#include <vector>

#include <asio/awaitable.hpp>

#include <kth/domain.hpp>
#include <kth/infrastructure/error.hpp>

namespace kth::blockchain {
class block_chain;
} // namespace kth::blockchain

namespace kth::node::mining {

// Reassemble the full block a light submission refers to: the miner's coinbase
// first, then the job's cached selection (CTOR order preserved).
domain::message::block assemble_block(
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs);

// Reassemble via assemble_block and submit the block to the chain. Returns the
// organize result (success == accepted, otherwise the reject reason). Shared by
// the getblocktemplatelight submit path and the SV2 template provider.
::asio::awaitable<code> submit_block_light(
    blockchain::block_chain& chain,
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs);

} // namespace kth::node::mining

#endif // KTH_NODE_MINING_BLOCK_SUBMIT_HPP
