// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/header_organizer.hpp>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/stats.hpp>
#include <kth/infrastructure/utility/timer.hpp>

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
    , finalizer_(index, settings.max_reorg_depth, settings.finalization_delay_seconds,
                 static_cast<int64_t>(zulu_time()))
{}

void header_organizer::note_block_validated(int32_t block_valid_height) {
    auto const before = finalizer_.finalized_height();
    finalizer_.maybe_advance(tip_index_, block_valid_height, static_cast<int64_t>(zulu_time()));
    auto const after = finalizer_.finalized_height();
    if (after != before) {
        spdlog::info("[header_organizer] Finalized block advanced to height {}", after);
    }
}

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

    // Materialize the active chain (height -> index) for this tip. The index also
    // holds side branches, so the rest of the node can only address blocks by
    // height through this mapping.
    index_.active_set_tip(tip_index_);

    spdlog::info("[header_organizer] Synced tip: index {}, hash {}, active height {}",
        tip_index_, encode_hash(tip_hash_), index_.active_tip_height());
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

    if (tip_index_ == database::header_index::null_index) {
        spdlog::error("[header_organizer] add_headers() called with uninitialized tip_index_ — call sync_tip() first");
        return {error::operation_failed, 0, 0, 0};
    }

    // Determine the batch anchor: the index entry the first header builds on.
    //   (1) anchor == tip                        -> forward extension of the active chain.
    //   (2) anchor is a known entry below the tip -> a side branch (fork). Validated
    //       and stored against its OWN ancestor so difficulty / MTP / checkpoint
    //       context is correct, then compared against the tip's work.
    //   (3) anchor unknown                       -> orphan; validation surfaces the
    //       missing-parent error (this is also the old ABC/BCHN duplicate-retry case).
    auto const first_prev_hash = headers.front().previous_block_hash();
    bool const extends_tip = (first_prev_hash == tip_hash_);

    index_t anchor_idx = tip_index_;
    if ( ! extends_tip) {
        anchor_idx = index_.find(first_prev_hash);
        if (anchor_idx == header_index::null_index) {
            // Unknown parent (orphan): validate against the tip so the missing-parent
            // error is raised and the batch rejected, as before.
            anchor_idx = tip_index_;
        }
    }

    // Running parent starts at the anchor (the tip for a forward extension, the fork
    // ancestor for a side branch) and advances as each header is stored.
    index_t parent_idx = anchor_idx;
    index_t branch_head_idx = anchor_idx;
    int32_t height = index_.get_height(anchor_idx) + 1;

    // Whether parent_idx is known to descend from the finalized block. A child of a
    // verified parent inherits the property, so a contiguous run of new headers costs
    // one ancestor walk instead of one per header. Jumping to an already-known header
    // (the `continue` below) moves the parent anywhere in the index, so it clears this.
    bool parent_descends_finalized = false;

    spdlog::debug("[header_organizer] Starting validation at height {}, anchor_index {}, extends_tip {}",
        height, anchor_idx, extends_tip);

    for (auto const& header : headers) {
        // Compute hash first (needed for validation)
        KTH_STATS_TIME_START(hash);
        auto const hash = domain::chain::hash(header);
        KTH_STATS_TIME_END(global_sync_stats(), hash, hash_time_ns, hash_calls);

        // Already known (duplicate/retry batch, or a re-announced branch header): it
        // was validated when first added, so skip re-validation and just advance the
        // running parent. Resync the height from the stored entry. This also covers
        // the below-tip retry batch the old early-return special-cased.
        if (auto const known_idx = index_.find(hash); known_idx != header_index::null_index) {
            parent_idx = known_idx;
            branch_head_idx = known_idx;
            height = index_.get_height(known_idx) + 1;
            // The parent moved to an arbitrary entry: re-verify before the next new header.
            parent_descends_finalized = false;
            spdlog::debug("[header_organizer] Header already exists at index {}", known_idx);
            continue;
        }

        // Header finalization (BCHN ContextualCheckBlockHeader / -finalizeheaders):
        // a NEW header whose parent does not descend from the finalized block extends
        // a branch that forks below finalization. Reject the batch, store nothing more,
        // and signal the caller to penalize the peer. Checked per NEW header (not just
        // the batch anchor) so a getheaders reply that overlaps a known point and then
        // continues onto a sub-finalized branch cannot slip new headers through.
        //
        // Already-known headers took the `continue` above (mirrors BCHN returning early
        // for known headers), so honest stale-retry of buried headers is not penalized.
        // While nothing is finalized yet (startup window / finalization disabled),
        // descends_from_finalized() is always true and nothing is rejected — matching
        // BCHN, where finalization is inactive until the node has been up long enough.
        if ( ! parent_descends_finalized) {
            if ( ! finalizer_.descends_from_finalized(parent_idx)) {
                spdlog::warn("[header_organizer] Rejecting header that forks below the finalized block "
                    "(parent height {}, finalized height {})",
                    index_.get_height(parent_idx), finalizer_.finalized_height());
                result.error = error::finalized_header_violation;
                break;
            }
            parent_descends_finalized = true;
        }

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

        // New header (known ones took the `continue` above), so this always inserts.
        // The parent becomes this header, a child of a parent already verified to
        // descend from the finalized block, so parent_descends_finalized still holds.
        parent_idx = add_result.index;
        branch_head_idx = add_result.index;
        ++height;
        ++result.headers_added;

        // Record wall-clock reception time for the finalization time rule.
        // (Headers loaded from the store at startup keep 0 = immediately eligible.)
        index_.set_received_time(add_result.index, static_cast<uint32_t>(zulu_time()));

        // Mark as valid header
        index_.add_status(add_result.index, header_status::valid_header);

        if (add_result.capacity_warning) {
            spdlog::warn("[header_organizer] Header index at 95% capacity!");
        }
    }

    // Decide the active tip. A forward extension advances it linearly. A side branch
    // only becomes the tip if it has strictly greater cumulative work — and even then
    // the actual switch is deferred to the execution layer, so here we only flag the
    // reorg candidate and keep the coordinator syncing (stale_chain, no forward count).
    if (extends_tip) {
        tip_index_ = branch_head_idx;
        tip_hash_ = index_.get_hash(branch_head_idx);
        // Extend the active chain over the headers just linked in. Walks back only
        // as far as the newly added run, since everything below is already active.
        index_.active_set_tip(tip_index_);
    } else if ( ! result.error) {
        auto const branch_work = index_.get_chain_work(branch_head_idx);
        auto const tip_work = index_.get_chain_work(tip_index_);
        if (branch_work > tip_work) {
            auto const fork_idx = index_.find_fork(branch_head_idx, tip_index_);
            result.reorg_candidate = true;
            result.reorg_branch_head = branch_head_idx;
            result.reorg_fork_height = (fork_idx == header_index::null_index)
                ? -1 : index_.get_height(fork_idx);
            spdlog::warn("[header_organizer] Reorg candidate: side branch (head height {}) has greater "
                "cumulative work than the active tip (height {}); fork at height {}. Active tip NOT "
                "switched — execution layer pending.",
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
