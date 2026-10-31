// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_ABLA_PIPELINE_HPP
#define KTH_BLOCKCHAIN_ABLA_PIPELINE_HPP

#if defined(KTH_CURRENCY_BCH)
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include <kth/domain.hpp>
#include <kth/domain/chain/abla.hpp>
#include <kth/domain/config/network.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/header_index.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/database/databases/header_abla_entry.hpp>

namespace kth::blockchain {

class block_chain;

// ABLA through the live pipeline.
//
// THE INVARIANT. The ABLA state OF block h, S_h, is
//
//     S_h = abla::next(S_{h-1}, cfg, size_h, tau_h)
//
// where S_{h-1} carries its own size and its own interval tau_{h-1}: the
// transition responds to the PREVIOUS block's size at the PREVIOUS interval, and
// tau_h only describes the state it produces. The limit that governs block h is
// (eps_h + beta_h) * tau_h / 600 and does not depend on size_h, so it is known
// before block h is.
//
// Consumers hold exactly one of two things, never a third:
//
//   * chain_state(h).abla_state() == S_h. For a connected block its block_size
//     is the block's size; for the CANDIDATE of the pool (height tip + 1) the
//     size is not known and is 0. dynamic_max_block_size() reads the limit of
//     that state directly, so nobody advances it again.
//   * the persisted record at height h == (size_h, eps_h, beta_h), the state of
//     the block AT that height. The interval is not stored: it is derived from
//     the schedule of the branch the record is read on.
//
// A consumer that needs the state of the next block advances ONCE from the
// state of the previous one (advance_abla_state); there is no other place a
// transition is applied.
//
// "Unknown" is an empty optional, not a made-up state. A record with both
// control fields zero predates state persistence. After ABLA initialization,
// every cadence requires the actual parent state: inventing a 600-tick floor
// could persist it as trusted state and carry the invention across activation.
// Only genesis and the algorithm's real initial activation use the floor.
// An invalid record is an error, never absence and never a floor reset.

/// The state a persisted record describes at `interval`; an empty optional
/// denotes no recorded state, while invalid fields return an error.
[[nodiscard]] KB_API
std::expected<std::optional<domain::chain::abla::state>, code> abla_state_from_record(
    settings const& settings, uint64_t block_size, uint64_t control_block_size,
    uint64_t elastic_buffer_size, uint64_t interval);

/// S_height from S_{height-1} (`parent`, nullopt when unknown or for genesis).
/// `block_size` is the size of the block at `height`, or 0 for a candidate whose
/// size is not known. `interval` is the block's own tau. nullopt when the
/// required parent is unknown/invalid or arithmetic fails; callers must not
/// invent a limit then.
[[nodiscard]] KB_API
std::optional<domain::chain::abla::state> advance_abla_state(
    settings const& settings, domain::config::network network,
    std::optional<domain::chain::abla::state> const& parent,
    size_t height, uint64_t block_size, uint64_t interval);

/// The state of every block of a connect batch, S_start .. S_{start+n-1}.
///
/// `sizes[i]` is the serialized size of the block at start_height + i. The
/// parent S_{start-1} is the persisted record of the connected tip; the
/// intervals come from the schedule of the branch ending at `last_idx` (an index
/// entry of the batch's last block), so a batch crossing the activation steps
/// from 600 to 60 where the branch does. Fails when the record cannot be read:
/// a read that failed is not an absent state. `settings` are the rules the batch
/// is judged by (the caller's, which validate_block_batch already takes).
[[nodiscard]] KB_API
std::expected<std::vector<domain::chain::abla::state>, code> abla_states_for_batch(
    block_chain const& chain, settings const& settings, domain::config::network network,
    uint32_t start_height, std::span<uint64_t const> sizes,
    header_index::index_t last_idx);

/// The records to persist for `states` (heights start_height ...).
[[nodiscard]] KB_API
std::vector<database::header_abla_record> abla_records_for(
    uint32_t start_height, std::span<domain::chain::abla::state const> states);

} // namespace kth::blockchain
#endif // KTH_CURRENCY_BCH
#endif // KTH_BLOCKCHAIN_ABLA_PIPELINE_HPP
