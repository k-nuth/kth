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
// maturity, fees (inputs >= outputs), coinbase value (<= subsidy + fees) and the
// per-transaction / per-block SigChecks limits. It does NOT re-check block-level
// rules already covered by the fast path or applied elsewhere (merkle root, ABLA
// block size, tx duplicate / BIP30, coinbase input shape). The batch is NOT
// applied/persisted here — the caller applies the UTXO delta only after this
// returns error::success.
//
// Returns error::success, or the first consensus error encountered.
KB_API
code validate_block_batch(
    block_chain& chain,
    settings const& settings,
    domain::config::network network,
    std::vector<block_const_ptr> const& blocks,
    size_t start_height);

// One input's SigChecks contribution to a validated batch, in emission order
// (block by block, then transaction by transaction, then input by input — so a
// transaction's inputs are contiguous and each block's inputs form a run).
struct sigcheck_entry {
    size_t block_index;   // index into block_limits; MUST be < block_limits.size()
    size_t tx_index;      // per-batch transaction ordinal; groups a tx's inputs
    uint64_t sigchecks;
};

// Enforce the BCH SigChecks consensus limits over a batch's per-input counts: at
// most `tx_limit` per transaction and at most `block_limits[block_index]` per
// block. `entries` must be in emission order. Returns error::success, or
// error::transaction_sigchecks_limit / error::block_sigchecks_limit on the first
// limit exceeded. Pure (no I/O) — unit-tested in test/batch_sigchecks.cpp.
// Precondition (contract-checked): every entry's block_index < block_limits.size().
[[nodiscard]] KB_API
code enforce_sigcheck_limits(
    std::vector<sigcheck_entry> const& entries,
    std::vector<uint64_t> const& block_limits,
    uint64_t tx_limit);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP
