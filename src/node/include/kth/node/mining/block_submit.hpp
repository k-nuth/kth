// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_MINING_BLOCK_SUBMIT_HPP
#define KTH_NODE_MINING_BLOCK_SUBMIT_HPP

#include <vector>

#include <kth/domain.hpp>

namespace kth::node::mining {

// Reassemble the full block a light submission refers to: the miner's coinbase
// first, then the job's cached selection (CTOR order preserved).
domain::message::block assemble_block(
    domain::chain::header const& header,
    domain::chain::transaction const& coinbase,
    std::vector<transaction_const_ptr> const& job_txs);

// The merkle branch of the coinbase in the block assemble_block builds from
// `job_txs`: the hashes a miner folds its coinbase txid with, in order, to reach
// that block's merkle root. Empty when the selection is empty.
hash_list coinbase_merkle_branch(std::vector<transaction_const_ptr> const& job_txs);

} // namespace kth::node::mining

#endif // KTH_NODE_MINING_BLOCK_SUBMIT_HPP
