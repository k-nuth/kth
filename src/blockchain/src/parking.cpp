// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/parking.hpp>

#include <kth/infrastructure/utility/assert.hpp>

namespace kth::blockchain::parking {

using database::header_index;

uint256_t required_work(header_index const& index,
                        index_t fork_idx,
                        index_t validated_tip_idx) {

    // Preconditions, enforced in release too: everything below reads the index by
    // these values and subtracts one work from the other. A null or out-of-range
    // index is an out-of-bounds read, and a fork that is not on the tip's chain
    // makes the subtraction wrap — a threshold no branch can ever clear, or one
    // every branch clears. Neither is something to carry on from quietly.
    KTH_CONTRACT(fork_idx != header_index::null_index);
    KTH_CONTRACT(validated_tip_idx != header_index::null_index);
    KTH_CONTRACT(fork_idx < index.size());
    KTH_CONTRACT(validated_tip_idx < index.size());
    KTH_CONTRACT(index.get_height(fork_idx) <= index.get_height(validated_tip_idx));
    KTH_CONTRACT(index.get_ancestor(validated_tip_idx, index.get_height(fork_idx)) == fork_idx);

    auto const fork_work = index.get_chain_work(fork_idx);
    auto const tip_work = index.get_chain_work(validated_tip_idx);

    // The general rule: twice what the validated chain gained since the fork.
    auto const full = tip_work + (tip_work - fork_work);

    auto const depth = index.get_height(validated_tip_idx) - index.get_height(fork_idx);
    if (depth < 1 || depth > 3) {
        return full;
    }

    // Shallow fork: charge half of one block's work instead. Walking back to the
    // fork's child makes the penalty one block wide at any of the three depths,
    // which is how BCHN's fallthrough chain over `case 3: case 2: case 1:`
    // reaches the same value.
    auto extra_idx = validated_tip_idx;
    for (int32_t i = 1; i < depth; ++i) {
        extra_idx = index.get_parent_index(extra_idx);
        if (extra_idx == header_index::null_index) {
            // Broken ancestry: charge the full penalty rather than let a branch
            // through on a threshold computed from a chain we cannot walk.
            return full;
        }
    }

    return tip_work + ((index.get_chain_work(extra_idx) - fork_work) >> 1);
}

} // namespace kth::blockchain::parking
