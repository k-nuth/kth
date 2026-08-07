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
// Space and sigchecks held back for the coinbase the caller prepends, matching
// BCHN's BlockAssembler. getblocktemplate uses these; a caller with a larger
// coinbase (extra outputs, e.g. an SV2 CoinbaseOutputDataSize request) raises
// the size reserve so the selection leaves room for it.
constexpr uint64_t default_coinbase_reserve_size = 1000;
constexpr uint64_t default_coinbase_reserve_sigchecks = 100;

struct block_template_context {
    uint64_t max_block_size;        // consensus block size limit
    uint64_t max_block_sigchecks;   // consensus block sigchecks limit
    size_t height;                  // template height (tip + 1)
    uint32_t median_time_past;      // tip's MTP (BIP113 finality time)
    uint64_t coinbase_reserve_size = default_coinbase_reserve_size;
    uint64_t coinbase_reserve_sigchecks = default_coinbase_reserve_sigchecks;
};

// Assemble a block template from `entries`, mirroring BCHN's BlockAssembler
// (miner.cpp addTxs): order candidates by individual fee-rate and add each once
// all its in-mempool parents are in, so the result is dependency-complete under
// CTOR without CPFP / ancestor-package scoring. Enforces the block size and
// sigchecks limits (with a coinbase reserve) and per-tx finality at the
// template height.
//
// `entries` must be a coherent view of the pool: a set the pool actually held
// at some instant. The dependency rule above is only meaningful on a set closed
// under "in-mempool parents" — a prevout whose txid is absent is read as
// already confirmed, so a child collected without its parent would be selected
// alone, and the block would spend an output that is neither in it nor on the
// chain (#611). This takes the entries rather than the pool precisely so that
// obligation lands on the caller holding the exclusion, where it can be met;
// walking a live concurrent map cannot meet it.
KB_API
block_template build_block_template(std::vector<mempool_entry> const& entries,
                                    block_template_context const& ctx);

// A full mining template: the transaction selection plus every header-level field
// a miner needs to assemble and solve the next block. This is what the getblock-
// template / getblocktemplatelight RPC and its C-API counterpart serialize. The
// caller builds a coinbase paying `coinbase_value` and prepends it to `selection`.
struct mining_template {
    uint32_t version;                   // suggested block version
    hash_digest previous_block_hash;    // tip hash (big-endian display is caller's job)
    size_t height;                      // template height (tip + 1)
    uint32_t bits;                      // required work, compact nBits
    uint32_t min_time;                  // earliest valid timestamp (MTP + 1)
    uint32_t current_time;              // suggested timestamp (clamped to >= min_time)
    uint64_t coinbase_value;            // subsidy(height) + selected fees, in satoshis
    uint64_t size_limit;                // consensus max block size
    uint64_t sigchecks_limit;           // consensus max block sigchecks
    block_template selection;           // CTOR-ordered txs + totals (no coinbase)
};

// Assemble a mining_template from the next-block parameters and the selection.
// Pure (no chain access), so it is unit-tested directly: min_time = MTP + 1,
// current_time = max(min_time, now), coinbase_value = subsidy(height) + fees.
KB_API
mining_template make_mining_template(
    uint32_t version, hash_digest const& previous_block_hash, size_t height,
    uint32_t bits, uint32_t median_time_past, uint32_t now,
    uint64_t size_limit, uint64_t sigchecks_limit, block_template selection);

// A snapshot of mining-relevant chain state, for the getmininginfo RPC and its
// C-API counterpart.
// A composite diagnostic, deliberately not a snapshot: `blocks`, `difficulty`
// and `chain` come from one published chain view, while `pooled_tx` and the two
// flags are read live. It stays answerable during a transition precisely because
// that is when someone is looking at it, and describing it as internally atomic
// would be a claim it cannot keep.
struct mining_info {
    size_t blocks;                      // current block height (tip)
    double difficulty;                  // difficulty of the next required work
    size_t pooled_tx;                   // transactions in the mempool
    domain::config::network chain;      // network the node is on
    bool transition_in_progress;        // a batch or reorganization is mutating
    bool caught_up;                     // connected chain has reached its headers
    bool fresh;                         // that tip is within the configured age
};

// The Bitcoin difficulty a compact nBits target represents (target 1 == 1.0).
// Pure, so it is unit-tested directly.
KB_API
double difficulty_from_bits(uint32_t bits);

} // namespace kth::blockchain

#endif
