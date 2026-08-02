// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/header_organizer.hpp>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/stats.hpp>

#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_header.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

// =============================================================================
// Construction
// =============================================================================

header_organizer::header_organizer(header_index& index, settings const& settings,
                                   domain::config::network network)
    : index_(index)
    , validator_(settings, network)
    , reorg_limit_(settings.reorganization_limit)
{}

// =============================================================================
// Lifecycle
// =============================================================================

bool header_organizer::start() {
    stopped_ = false;
    return true;
}

bool header_organizer::stop() {
    stopped_ = true;
    return true;
}

// =============================================================================
// Initialization
// =============================================================================

void header_organizer::sync_tip() {
    // Tip = entry with maximum cumulative chain_work. The header_index
    // now stores chain_work as uint256_t (computed from header bits via
    // header_basis::proof), so this is the canonical Bitcoin tip even
    // when forks coexist in the index. Tie-break on the lowest index_t,
    // which corresponds to the entry that landed first.
    auto const size = index_.size();
    if (size == 0) {
        return;
    }

    index_t best_idx = 0;
    uint256_t best_work = index_.get_chain_work(best_idx);
    for (index_t i = 1; i < index_t(size); ++i) {
        auto const w = index_.get_chain_work(i);
        if (w > best_work) {
            best_work = w;
            best_idx = i;
        }
    }
    tip_index_ = best_idx;
    tip_hash_ = index_.get_hash(tip_index_);
    spdlog::info("[header_organizer] Synced tip: index {}, hash {}",
        tip_index_, encode_hash(tip_hash_));
}

// =============================================================================
// Header Addition
// =============================================================================

header_organize_result header_organizer::add_headers(domain::message::header::list const& headers) {
    header_organize_result result;

    if (stopped()) {
        result.error = error::service_stopped;
        return result;
    }

    if (headers.empty()) {
        result.error = error::success;
        return result;
    }

    spdlog::debug("[header_organizer] add_headers() called with {} headers", headers.size());

    // Current tip for validation
    if (tip_index_ == database::header_index::null_index) {
        spdlog::error("[header_organizer] add_headers() called with uninitialized tip_index_ — call sync_tip() first");
        return {error::operation_failed, 0, 0, 0};
    }

    // Determine the batch anchor: the index entry the first header builds on.
    //   (1) anchor == tip                        -> forward extension of the active chain.
    //   (2) anchor is a known entry below the tip -> a side branch (fork). It is
    //       validated and stored against its OWN ancestor (not the tip) so difficulty
    //       / MTP / checkpoint context is correct, and its cumulative work is compared
    //       against the tip. A strictly-heavier branch is flagged as a reorg candidate,
    //       but the active tip is NOT switched here — executing the switch (block
    //       disconnect + UTXO-Z rewind) is a separate layer.
    //   (3) anchor unknown                       -> orphan; validation surfaces the
    //       missing-parent error as before (previously the ABC/BCHN duplicate-retry
    //       case, which now falls out of the side-branch handling below).
    auto const first_prev_hash = headers.front().previous_block_hash();
    bool const extends_tip = (first_prev_hash == tip_hash_);

    index_t anchor_idx = tip_index_;
    if ( ! extends_tip) {
        anchor_idx = index_.find(first_prev_hash);
        if (anchor_idx == header_index::null_index) {
            // Unknown parent (orphan batch): validate against the tip so the
            // missing-parent error is raised and the batch rejected, as before.
            anchor_idx = tip_index_;
        } else {
            // Side branch. Bound the fork depth we bother storing/comparing so a peer
            // cannot grow the index with (PoW-backed) branches far below the tip.
            auto const tip_height = index_.get_height(tip_index_);
            auto const anchor_height = index_.get_height(anchor_idx);
            if (anchor_height < tip_height &&
                uint32_t(tip_height - anchor_height) > reorg_limit_) {
                spdlog::info("[header_organizer] Skipping header batch on a fork {} blocks below the tip "
                    "(reorg limit {}) — treated as stale", tip_height - anchor_height, reorg_limit_);
                result.error = error::stale_chain;
                result.index_size = index_.size();
                result.index_memory_bytes = index_.memory_usage();
                return result;
            }
        }
    }

    // Running parent starts at the anchor (the tip for a forward extension, the fork
    // ancestor for a side branch) and advances as each header is stored.
    index_t parent_idx = anchor_idx;
    index_t branch_head_idx = anchor_idx;
    int32_t height = index_.get_height(anchor_idx) + 1;

    spdlog::debug("[header_organizer] Starting validation at height {}, anchor_index {}, extends_tip {}",
        height, anchor_idx, extends_tip);

    for (auto const& header : headers) {
        // Compute hash first (needed for validation)
        KTH_STATS_TIME_START(hash);
        auto const hash = domain::chain::hash(header);
        KTH_STATS_TIME_END(global_sync_stats(), hash, hash_time_ns, hash_calls);

        // Validate header with full chain-state validation against its running parent.
        // This includes: PoW check, difficulty, checkpoint, version, MTP
        KTH_STATS_TIME_START(validate);
        auto const ec = validate_full(header, hash, height, parent_idx);
        KTH_STATS_TIME_END(global_sync_stats(), validate, validate_time_ns, validate_calls);

        if (ec) {
            spdlog::debug("[header_organizer] Header validation failed at height {}: {}",
                height, ec.message());
            result.error = ec;
            break;
        }

        KTH_STATS_TIME_START(index_add);
        auto const add_result = index_.add(hash, header);
        KTH_STATS_TIME_END(global_sync_stats(), index_add, index_add_time_ns, index_add_calls);

        parent_idx = add_result.index;
        branch_head_idx = add_result.index;
        ++height;

        if (add_result.inserted) {
            ++result.headers_added;
            index_.add_status(add_result.index, header_status::valid_header);
            if (add_result.capacity_warning) {
                spdlog::warn("[header_organizer] Header index at 95% capacity!");
            }
        } else {
            // Already known (duplicate/retry batch, or a re-announced branch header).
            spdlog::debug("[header_organizer] Header already exists at index {}", add_result.index);
        }
    }

    // Decide the active tip. A forward extension advances it linearly. A side branch
    // only becomes the tip if it has strictly greater cumulative work — and even then
    // the actual switch is deferred to the execution layer, so here we only flag the
    // reorg candidate and keep the coordinator syncing (stale_chain, no forward count).
    if (extends_tip) {
        tip_index_ = branch_head_idx;
        tip_hash_ = index_.get_hash(branch_head_idx);
    } else if ( ! result.error) {
        auto const branch_work = index_.get_chain_work(branch_head_idx);
        auto const tip_work = index_.get_chain_work(tip_index_);
        if (branch_work > tip_work) {
            auto const fork_idx = index_.find_fork(branch_head_idx, tip_index_);
            result.reorg_candidate = true;
            result.reorg_fork_height = (fork_idx == header_index::null_index)
                ? -1 : index_.get_height(fork_idx);
            spdlog::warn("[header_organizer] Reorg candidate: side branch (head height {}) has greater "
                "cumulative work than the active tip (height {}); fork at height {}. Active tip NOT "
                "switched — block-disconnect / UTXO-Z rewind not yet implemented.",
                index_.get_height(branch_head_idx), index_.get_height(tip_index_),
                result.reorg_fork_height);
        }
        // The active tip did not advance: signal keep-syncing without reporting forward
        // progress (preserves the coordinator contract for stale/duplicate batches).
        result.error = error::stale_chain;
        result.headers_added = 0;
    }

    spdlog::debug("[header_organizer] Validation complete: {} headers added, total size {}",
        result.headers_added, index_.size());

    result.index_size = index_.size();
    result.index_memory_bytes = index_.memory_usage();

    return result;
}

// =============================================================================
// State Queries
// =============================================================================

int32_t header_organizer::header_height() const {
    if (tip_index_ == header_index::null_index) {
        return -1;  // No headers yet (only genesis)
    }
    return index_.get_height(tip_index_);
}

uint32_t header_organizer::tip_timestamp() const {
    if (tip_index_ == header_index::null_index) {
        return 0;
    }
    return index_.get_timestamp(tip_index_);
}

// =============================================================================
// Validation
// =============================================================================

code header_organizer::validate_full(domain::chain::header const& header,
                                     hash_digest const& hash,
                                     int32_t height,
                                     header_index::index_t parent_idx) const {
    // First do context-free check (PoW, timestamp not too far in future)
    // Skip PoW check if under checkpoint (trusted headers)
    if (!validator_.is_under_checkpoint(size_t(height))) {
        auto const ec = validator_.check(header);
        if (ec) {
            return ec;
        }
    }

    // Then do full accept validation with chain_state built from header_index
    // This validates: difficulty, checkpoint, version, MTP
    return validator_.accept_full(header, hash, size_t(height), parent_idx, index_);
}

} // namespace kth::blockchain
