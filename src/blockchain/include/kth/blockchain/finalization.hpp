// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_FINALIZATION_HPP
#define KTH_BLOCKCHAIN_FINALIZATION_HPP

#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/database/header_index.hpp>

namespace kth::blockchain {

// Tracks the finalized block, mirroring BCHN's CChainState::pindexFinalized
// (validation.cpp FindBlockToFinalize / IsBlockFinalized).
//
// The finalized block is the deepest block on the active chain that is both
//   (a) at least `max_reorg_depth` blocks below the active block-validation tip
//       (BCHN -maxreorgdepth, default 10), and
//   (b) old enough: its header was received at least `finalization_delay`
//       seconds ago (BCHN -finalizationdelay, default 2h). A reception time of 0
//       means "loaded from the store at startup" and is always eligible.
//
// It advances only forward during normal operation and constrains reorgs: no
// reorg may cross it (see FindMostWorkChain / ContextualCheckBlockHeader in
// BCHN). Finalization is disabled when max_reorg_depth < 0.
//
// This type holds only the finalized index and reads the header_index; it does
// not own or mutate chain state. Thread-safety is the caller's responsibility
// (advance/retreat are single-writer, like the rest of the header sync path).
struct KB_API finalization {
    using index_t = database::header_index::index_t;
    static constexpr index_t null_index = database::header_index::null_index;

    finalization(database::header_index const& index,
                 int32_t max_reorg_depth,
                 int64_t finalization_delay_seconds,
                 int64_t startup_time_unix);

    // Advance the finalized pointer. `active_tip_idx` anchors the active chain
    // (its ancestors are the active chain). `block_valid_height` is how far blocks
    // have been validated. `now` is wall-clock unix seconds. No-op if nothing new
    // is eligible; never moves the pointer backward.
    void maybe_advance(index_t active_tip_idx, int32_t block_valid_height, int64_t now);

    // The finalized block, or null_index if nothing is finalized yet.
    [[nodiscard]] index_t finalized() const { return finalized_idx_; }

    // Height of the finalized block, or -1 if nothing is finalized yet.
    [[nodiscard]] int32_t finalized_height() const;

    // BCHN IsBlockFinalized: true iff `idx` is an ancestor of (or equal to) the
    // finalized block — i.e. it is buried at/below the finalization point.
    [[nodiscard]] bool is_finalized(index_t idx) const;

    // True iff `idx` descends from (has as an ancestor) the finalized block, i.e.
    // sits on the finalized chain at/above the finalization point. Used to admit
    // headers/branches: BCHN rejects a header whose parent fails this. With no
    // finalized block yet, everything is admissible (returns true).
    [[nodiscard]] bool descends_from_finalized(index_t idx) const;

    // Retreat the finalized pointer when a finalized block is disconnected or
    // invalidated (BCHN sets pindexFinalized = pindexDelete->pprev). Only the
    // execution layer calls this.
    void retreat_to(index_t to_idx);

private:
    database::header_index const& index_;
    int32_t const max_reorg_depth_;
    int64_t const finalization_delay_;
    int64_t const startup_time_;
    index_t finalized_idx_{null_index};
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_FINALIZATION_HPP
