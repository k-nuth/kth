// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP
#define KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP

#include <cstddef>
#include <optional>
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

// Which block of a batch a refusal is about.
//
// The code alone cannot say: a batch of a hundred blocks refused with
// coinbase_value_limit names no block, and a caller that has to act on the
// refusal — reject one block, keep the ones before it — has to guess or give up.
// Filled only on a non-success return; untouched on success.
struct batch_rejection {
    // Index into `blocks` of the block the error is attributed to. The height
    // is start_height + *block. Empty when the failure is not any block's: a
    // storage fault, a state that could not be derived, a resolution that
    // could not run. Those come back as local codes (operation_failed,
    // not_found), never as a consensus verdict, and attribute nothing.
    //
    // Deterministic. The serial phases walk the batch in height order and stop
    // at the first block that fails. The parallel script phase is made to agree
    // with that: no transaction of a higher block is skipped until every
    // transaction of the lowest failing block has been judged, and the
    // attributed transaction is the first failing one in emission order — so
    // two runs over the same batch name the same block and the same code,
    // whatever the thread interleaving.
    //
    // Nothing is claimed about the blocks AFTER the attributed one: validation
    // stopped there. Nothing is claimed about the blocks BEFORE it beyond what
    // the phase that stopped had checked — a structural refusal at block k has
    // not run scripts for blocks 0..k-1. A caller that keeps a prefix must
    // validate it again as a batch of its own.
    std::optional<size_t> block;
};

// The same validation, reporting which block it refused. The old form above is
// this one with the report dropped.
KB_API
code validate_block_batch(
    block_chain& chain,
    settings const& settings,
    domain::config::network network,
    std::vector<block_const_ptr> const& blocks,
    size_t start_height,
    batch_rejection& rejection);

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

// The same, with the per-transaction limit taken from the block each
// transaction belongs to (tx_limits is indexed like block_limits): blocks below
// the May 2020 activation carry max_tx_sigchecks_before_activation, blocks at or
// above it carry max_tx_sigchecks. A batch may hold both.
[[nodiscard]] KB_API
code enforce_sigcheck_limits(
    std::vector<sigcheck_entry> const& entries,
    std::vector<uint64_t> const& block_limits,
    std::vector<uint64_t> const& tx_limits);

// The same, naming the block whose limit was exceeded: `failed_block` is set to
// the offending entry's block_index on a non-success return and left alone on
// success. Entries are in emission order, so the block named is the lowest one
// that breaks a limit.
[[nodiscard]] KB_API
code enforce_sigcheck_limits(
    std::vector<sigcheck_entry> const& entries,
    std::vector<uint64_t> const& block_limits,
    std::vector<uint64_t> const& tx_limits,
    size_t& failed_block);

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_VALIDATE_BATCH_VALIDATE_HPP
