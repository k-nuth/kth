// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP
#define KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP

#include <cstddef>
#include <vector>

#include <kth/domain.hpp>

#include <kth/domain/config/network.hpp>

#include <kth/blockchain/define.hpp>

namespace kth::blockchain {

class block_chain;
struct settings;

// Fully validate a contiguous batch of blocks [start_height .. start_height+M-1]
// that extend a chain whose UTXO-Z set is complete through start_height-1. This
// is the post-checkpoint (full) validation stage: it verifies scripts, fees,
// sigchecks and coinbase value against the consensus rules.
//
// Design (parallel):
//   - The UTXO-Z set is a fixed read-only snapshot at start_height-1; reads run
//     concurrently. A prevout resolves from UTXO-Z OR from an output created by
//     another block *within the batch* (BCH CTOR: any tx may spend any same-batch
//     output that is not a same-block/immature coinbase).
//   - Double-spend: every spent outpoint must be unique across the batch, and a
//     UTXO-Z prevout must not already be spent.
//   - Script/signature verification is independent per input once prevouts are
//     resolved, so it runs in parallel across every input of the whole batch.
//
// Flags are computed PER HEIGHT from chain_state (built for the first block and
// advanced with from_pool_ptr), so each block validates under the forks actually
// active at its height — not a fixed all-forks set.
//
// This stage verifies: input scripts/signatures, in-batch double spends, coinbase
// maturity, fees (inputs >= outputs) and coinbase value (<= subsidy + fees). It
// does NOT re-check block-level rules already covered by the fast path or applied
// elsewhere (merkle root, ABLA block size, per-block sigcheck accounting, tx
// duplicate / BIP30, coinbase input shape). The batch is NOT applied/persisted
// here — the caller applies the UTXO delta only after this returns error::success.
//
// Returns error::success, or the first consensus error encountered.
KB_API
code validate_block_batch(
    block_chain& chain,
    settings const& settings,
    domain::config::network network,
    std::vector<block_const_ptr> const& blocks,
    size_t start_height);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP
