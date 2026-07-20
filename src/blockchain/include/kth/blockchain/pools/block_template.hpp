// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_BLOCK_TEMPLATE_HPP
#define KTH_BLOCKCHAIN_POOLS_BLOCK_TEMPLATE_HPP

#include <cstdint>
#include <vector>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool.hpp>

namespace kth::blockchain {

// The transactions selected for the next block plus the totals a caller needs
// to finish the block. `txs` excludes the coinbase and is CTOR-ordered (txid
// ascending), so the caller prepends its own coinbase at index 0. `total_fees`
// gives the coinbase value (subsidy + total_fees); the size / sigchecks totals
// cover only the selected txs (the coinbase reserve is accounted separately
// during selection).
struct block_template {
    std::vector<transaction_const_ptr> txs;
    uint64_t total_fees;
    uint64_t total_size;
    uint64_t total_sigchecks;
};

// Everything the builder needs about the block being assembled, taken from the
// chain state of the tip's next block. Kept as plain scalars so the builder is a
// pure function of (mempool, context), independent of chain_state.
struct block_template_context {
    uint64_t max_block_size;        // consensus block size limit
    uint64_t max_block_sigchecks;   // consensus block sigchecks limit
    size_t height;                  // template height (tip + 1)
    uint32_t median_time_past;      // tip's MTP (BIP113 finality time)
};

// Assemble a block template from the mempool, mirroring BCHN's BlockAssembler
// (miner.cpp addTxs): order candidates by individual fee-rate and add each once
// all its in-mempool parents are in, so the result is dependency-complete under
// CTOR without CPFP / ancestor-package scoring. Enforces the block size and
// sigchecks limits (with a coinbase reserve) and per-tx finality at the
// template height.
KB_API
block_template build_block_template(mempool const& pool, block_template_context const& ctx);

} // namespace kth::blockchain

#endif
