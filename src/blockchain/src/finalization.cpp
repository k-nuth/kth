// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/finalization.hpp>

namespace kth::blockchain {

using database::header_index;

finalization::finalization(header_index const& index,
                           int32_t max_reorg_depth,
                           int64_t finalization_delay_seconds,
                           int64_t startup_time_unix)
    : index_(index)
    , max_reorg_depth_(max_reorg_depth)
    , finalization_delay_(finalization_delay_seconds)
    , startup_time_(startup_time_unix)
{}

void finalization::maybe_advance(index_t active_tip_idx, int32_t block_valid_height, int64_t now) {
    // Finalization disabled (BCHN -maxreorgdepth = -1).
    if (max_reorg_depth_ < 0) {
        return;
    }

    if (active_tip_idx == null_index) {
        return;
    }

    // Startup guard: don't finalize anything until the node has been up for at
    // least finalization_delay (BCHN protects a freshly started node).
    if (now < startup_time_ + finalization_delay_) {
        return;
    }

    // Depth rule: the candidate is the active-chain block max_reorg_depth below
    // the validated tip.
    int32_t const candidate_height = block_valid_height - max_reorg_depth_;
    if (candidate_height < 0) {
        return;
    }

    // Nothing above the current finalized block to finalize. Guards against a
    // caller passing a lower block_valid_height (the walk below would otherwise
    // move the pointer backward). During normal forward sync this never trips.
    if (finalized_idx_ != null_index && candidate_height <= finalized_height()) {
        return;
    }

    index_t candidate = index_.get_ancestor(active_tip_idx, candidate_height);
    if (candidate == null_index) {
        return;
    }

    // Time rule: walk down from the candidate toward the current finalized block
    // and finalize the first block whose header is old enough. Stopping at the
    // current finalized block keeps the pointer monotonic (never moves backward).
    index_t idx = candidate;
    while (idx != null_index && idx != finalized_idx_) {
        uint32_t const received = index_.get_received_time(idx);
        // received == 0 => loaded from the store => always eligible.
        if (now >= int64_t(received) + finalization_delay_) {
            finalized_idx_ = idx;
            return;
        }
        idx = index_.get_parent_index(idx);
    }
}

int32_t finalization::finalized_height() const {
    if (finalized_idx_ == null_index) {
        return -1;
    }
    return index_.get_height(finalized_idx_);
}

bool finalization::is_finalized(index_t idx) const {
    if (finalized_idx_ == null_index || idx == null_index) {
        return false;
    }
    // idx is an ancestor of (or equal to) the finalized block.
    int32_t const h = index_.get_height(idx);
    return index_.get_ancestor(finalized_idx_, h) == idx;
}

bool finalization::descends_from_finalized(index_t idx) const {
    // Nothing finalized yet: everything is admissible.
    if (finalized_idx_ == null_index) {
        return true;
    }
    if (idx == null_index) {
        return false;
    }
    int32_t const fh = index_.get_height(finalized_idx_);
    if (index_.get_height(idx) < fh) {
        return false;
    }
    // The finalized block is an ancestor of (or equal to) idx.
    return index_.get_ancestor(idx, fh) == finalized_idx_;
}

void finalization::retreat_to(index_t to_idx) {
    finalized_idx_ = to_idx;
}

} // namespace kth::blockchain
