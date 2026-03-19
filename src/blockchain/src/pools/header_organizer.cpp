// Copyright (c) 2016-2025 Knuth Project developers.
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
    // Sync tip from the header index (assumes index is already initialized)
    auto const size = index_.size();
    if (size > 0) {
        // For now, assume tip is the last added entry (index size - 1)
        // TODO(fernando): properly track the actual chain tip when we support forks
        tip_index_ = index_t(size - 1);
        tip_hash_ = index_.get_hash(tip_index_);
        spdlog::info("[header_organizer] Synced tip: index {}, hash {}",
            tip_index_, encode_hash(tip_hash_));
    }
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
    hash_digest prev_hash = tip_hash_;
    int32_t height = index_.get_height(tip_index_) + 1;

    spdlog::debug("[header_organizer] Starting validation at height {}, tip_hash: {}, tip_index: {}",
        height, encode_hash(tip_hash_), tip_index_);

    // 2026-01-28: Fix for duplicate/stale header batches causing "missing parent" errors.
    // Symptom: After ABC peer fails checkpoint and sync retries with BCHN peer, BCHN headers
    // are added successfully. But then a DUPLICATE retry request (from the ABC peer's
    // channel_stopped event) causes the BCHN peer to send headers starting from an older
    // point. The organizer tried to validate these at the current tip height, causing
    // "missing parent" errors and banning innocent BCHN peers.
    //
    // Fix: Check if the first header's prev_hash matches our current tip. If not, check if
    // it matches an older block in our chain. If so, these are stale/duplicate headers
    // that can be silently skipped.
    if (!headers.empty()) {
        auto const& first_prev_hash = headers.front().previous_block_hash();
        spdlog::debug("[header_organizer] First header prev_hash: {}",
            encode_hash(first_prev_hash));

        if (first_prev_hash != tip_hash_) {
            // First header doesn't connect to our tip - check if it's a stale request
            auto const existing_idx = index_.find(first_prev_hash);
            if (existing_idx != header_index::null_index) {
                // The prev_hash exists in our chain but is not the tip - stale request
                auto const existing_height = index_.get_height(existing_idx);
                auto const tip_height = index_.get_height(tip_index_);

                if (existing_height < tip_height) {
                    // 2026-02-02: Return stale_chain error instead of success.
                    // This prevents the coordinator from marking header sync as "complete"
                    // when it receives a stale batch (which has headers_added = 0).
                    // The coordinator will see this error and continue syncing instead of stopping.
                    spdlog::info("[header_organizer] Skipping stale header batch: first header connects to "
                        "height {} but tip is at height {} - likely duplicate retry request",
                        existing_height, tip_height);
                    result.error = error::stale_chain;  // Signal stale batch, not end of chain
                    result.index_size = index_.size();
                    result.index_memory_bytes = index_.memory_usage();
                    return result;
                }
            }
            // If prev_hash doesn't exist at all, let validation handle it (orphan block)
        }
    }

    for (auto const& header : headers) {
        // Compute hash first (needed for validation)
        KTH_STATS_TIME_START(hash);
        auto const hash = header.hash();
        KTH_STATS_TIME_END(global_sync_stats(), hash, hash_time_ns, hash_calls);

        // Validate header with full chain-state validation
        // This includes: PoW check, difficulty, checkpoint, version, MTP
        KTH_STATS_TIME_START(validate);
        auto const ec = validate_full(header, hash, height, tip_index_);
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

        if (add_result.inserted) {
            tip_index_ = add_result.index;
            tip_hash_ = hash;
            prev_hash = hash;
            ++height;
            ++result.headers_added;

            // Mark as valid header
            index_.add_status(add_result.index, header_status::valid_header);

            if (add_result.capacity_warning) {
                spdlog::warn("[header_organizer] Header index at 95% capacity!");
            }
        } else {
            // Header already existed - this shouldn't happen in normal sync
            spdlog::debug("[header_organizer] Header already exists at index {}",
                add_result.index);
        }

        // Log progress every 1000 headers
        // if (result.headers_added > 0 && result.headers_added % 1000 == 0) {
        //     spdlog::debug("[header_organizer] Validated {} headers, height {}...",
        //         result.headers_added, height - 1);
        // }
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
