// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_PARKING_HPP
#define KTH_BLOCKCHAIN_PARKING_HPP

#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/database/header_index.hpp>

namespace kth::blockchain::parking {

// Deep-reorg parking (BCHN -parkdeepreorg / -automaticunparking).
//
// Greater cumulative work is not, by itself, enough to move the node onto
// another branch. A branch that would rewind more than one block from the
// *validated* chain is "parked": it stays in the index, keeps accumulating
// work, and only activates once it clears the threshold below. This is what
// stops a node from following a short-lived heavier fork, and it is why an
// attacker needs a sustained majority rather than a momentary work lead.
//
// BCHN parks a block at AcceptBlock and clears the flag in FindMostWorkChain.
// KTH is header-first: the decision is recomputed from chain work every time a
// branch is evaluated as a reorg candidate, so there is no park bit to persist
// and no unpark step — a branch that clears the threshold is simply not parked
// on that evaluation.
//
// Heights and indices here are those of the *validated* chain (BCHN's m_chain),
// not the header tip. A branch that forks above the validated tip rewinds no
// validated block, so it is never parked: switching to it costs nothing.

using index_t = database::header_index::index_t;

// BCHN AcceptBlock: park iff activating the branch would rewind more than one
// block, i.e. `pindexFork->nHeight + 1 < m_chain.Height()`.
[[nodiscard]]
constexpr bool is_deep_reorg(int32_t fork_height, int32_t validated_tip_height) {
    return fork_height + 1 < validated_tip_height;
}

// Work a parked branch must exceed to activate (BCHN FindMostWorkChain's
// `requiredWork`): the validated tip's work plus, again, everything the
// validated chain gained since the fork — twice the post-fork work.
//
// For forks one to three blocks deep the penalty is instead half a single
// block's work. A near-tip race is ordinary network behaviour, not an attack,
// and demanding a 2x lead there would leave the node stranded on a branch the
// rest of the network has already abandoned.
//
// `fork_idx` must be an ancestor of `validated_tip_idx`; both must be valid.
[[nodiscard]]
uint256_t required_work(database::header_index const& index,
                        index_t fork_idx,
                        index_t validated_tip_idx);

} // namespace kth::blockchain::parking

#endif // KTH_BLOCKCHAIN_PARKING_HPP
