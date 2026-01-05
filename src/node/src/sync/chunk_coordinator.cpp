// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/chunk_coordinator.hpp>

#include <algorithm>
#include <thread>

#include <spdlog/spdlog.h>

namespace kth::node::sync {

chunk_coordinator::chunk_coordinator(
    blockchain::header_index const& index,
    uint32_t start_height,
    uint32_t end_height,
    chunk_coordinator_config const& config)
    : config_(config)
    , slots_per_round_(config.max_peers * config.slots_multiplier)
    , index_(index)
    , start_height_(start_height)
    , end_height_(end_height)
    , total_chunks_((end_height - start_height + config.chunk_size) / config.chunk_size)
    , slots_(slots_per_round_)
    , slot_times_(slots_per_round_)
{
    // Initialize all slots to FREE
    for (size_t i = 0; i < slots_per_round_; ++i) {
        slots_[i].store(FREE, std::memory_order_relaxed);
        slot_times_[i].store(0, std::memory_order_relaxed);
    }

    spdlog::debug("[chunk_coordinator] Created for blocks {}-{} ({} blocks, {} chunks, {} slots/round)",
        start_height, end_height, end_height - start_height + 1,
        total_chunks_, slots_per_round_);
}

// =============================================================================
// Peer Interface (Lock-Free)
// =============================================================================

std::optional<uint32_t> chunk_coordinator::claim_chunk() {
    if (stopped_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // Wait if round is being reset (spin-wait, very brief)
    while (resetting_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    uint32_t r = round_.load(std::memory_order_acquire);

    // Search for a FREE slot using CAS
    for (size_t i = 0; i < slots_per_round_; ++i) {
        uint8_t expected = FREE;
        if (slots_[i].compare_exchange_strong(expected, IN_PROGRESS,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            // Got slot i - record assignment time
            slot_times_[i].store(now_ms(), std::memory_order_release);

            uint32_t chunk_id = r * uint32_t(slots_per_round_) + uint32_t(i);

            // Check if this chunk is beyond our target
            auto [block_start, block_end] = chunk_range(chunk_id);
            if (block_start > end_height_) {
                // No more chunks needed - mark as completed and return nullopt
                slots_[i].store(COMPLETED, std::memory_order_release);
                return std::nullopt;
            }

            spdlog::debug("[chunk_coordinator] Claimed chunk {} (slot {}, round {}, blocks {}-{})",
                chunk_id, i, r, block_start, block_end);

            return chunk_id;
        }
    }

    // All slots occupied - check if all are COMPLETED to advance round
    bool all_completed = true;
    for (size_t i = 0; i < slots_per_round_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) != COMPLETED) {
            all_completed = false;
            break;
        }
    }

    if (all_completed) {
        try_advance_round();
        // Retry claim after round advance
        return claim_chunk();
    }

    // Some slots are IN_PROGRESS - peer should wait and retry
    spdlog::debug("[chunk_coordinator] All slots occupied, waiting...");
    return std::nullopt;
}

std::pair<uint32_t, uint32_t> chunk_coordinator::chunk_range(uint32_t chunk_id) const {
    uint32_t block_start = start_height_ + chunk_id * uint32_t(config_.chunk_size);
    uint32_t block_end = std::min(block_start + uint32_t(config_.chunk_size) - 1, end_height_);
    return {block_start, block_end};
}

hash_digest chunk_coordinator::get_block_hash(uint32_t height) const {
    return index_.get_hash(static_cast<blockchain::header_index::index_t>(height));
}

void chunk_coordinator::chunk_completed(uint32_t chunk_id) {
    uint32_t r = chunk_id / uint32_t(slots_per_round_);
    size_t slot = chunk_id % slots_per_round_;

    uint32_t current_round = round_.load(std::memory_order_acquire);
    if (r != current_round) {
        // Old round - ignore (already processed)
        spdlog::debug("[chunk_coordinator] Chunk {} completed but from old round {} (current: {})",
            chunk_id, r, current_round);
        return;
    }

    // Mark as completed
    slots_[slot].store(COMPLETED, std::memory_order_release);
    chunks_completed_.fetch_add(1, std::memory_order_relaxed);

    spdlog::debug("[chunk_coordinator] Chunk {} completed (slot {})", chunk_id, slot);

    // Try to advance round if all completed
    try_advance_round();
}

void chunk_coordinator::chunk_failed(uint32_t chunk_id) {
    uint32_t r = chunk_id / uint32_t(slots_per_round_);
    size_t slot = chunk_id % slots_per_round_;

    uint32_t current_round = round_.load(std::memory_order_acquire);
    if (r != current_round) {
        // Old round - ignore
        return;
    }

    // Reset to FREE so another peer can take it
    uint8_t expected = IN_PROGRESS;
    if (slots_[slot].compare_exchange_strong(expected, FREE,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        spdlog::debug("[chunk_coordinator] Chunk {} failed, reset to FREE for retry", chunk_id);
    }
}

// =============================================================================
// Status
// =============================================================================

void chunk_coordinator::check_timeouts() {
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t now = now_ms();
    uint64_t timeout_ms = uint64_t(config_.stall_timeout_secs) * 1000;

    for (size_t i = 0; i < slots_per_round_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == IN_PROGRESS) {
            uint64_t assigned_at = slot_times_[i].load(std::memory_order_acquire);
            if (assigned_at > 0 && (now - assigned_at) > timeout_ms) {
                // Slot stalled - reset to FREE
                uint8_t expected = IN_PROGRESS;
                if (slots_[i].compare_exchange_strong(expected, FREE,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    uint32_t chunk_id = round_.load(std::memory_order_acquire) *
                        uint32_t(slots_per_round_) + uint32_t(i);
                    spdlog::warn("[chunk_coordinator] Chunk {} (slot {}) timed out after {}s, reset to FREE",
                        chunk_id, i, config_.stall_timeout_secs);
                }
            }
        }
    }
}

bool chunk_coordinator::is_complete() const {
    return chunks_completed_.load(std::memory_order_acquire) >= total_chunks_;
}

bool chunk_coordinator::is_stopped() const {
    return stopped_.load(std::memory_order_acquire);
}

void chunk_coordinator::stop() {
    stopped_.store(true, std::memory_order_release);
}

chunk_coordinator::progress chunk_coordinator::get_progress() const {
    // Count slots in current round
    uint32_t in_progress = 0;
    uint32_t completed_this_round = 0;
    for (size_t i = 0; i < slots_per_round_; ++i) {
        uint8_t state = slots_[i].load(std::memory_order_acquire);
        if (state == IN_PROGRESS) ++in_progress;
        else if (state == COMPLETED) ++completed_this_round;
    }

    return {
        .total_chunks = total_chunks_,
        .chunks_completed = chunks_completed_.load(std::memory_order_acquire),
        .chunks_in_progress = in_progress,
        .current_round = round_.load(std::memory_order_acquire),
        .start_height = start_height_,
        .end_height = end_height_
    };
}

// =============================================================================
// Private Helpers
// =============================================================================

void chunk_coordinator::try_advance_round() {
    // Check if all slots are COMPLETED
    for (size_t i = 0; i < slots_per_round_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) != COMPLETED) {
            return;  // Not all completed
        }
    }

    // All completed - try to acquire reset lock
    bool expected = false;
    if (!resetting_.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        return;  // Another thread is resetting
    }

    // We have the reset lock
    uint32_t old_round = round_.load(std::memory_order_acquire);
    uint32_t new_round = old_round + 1;

    // Check if we've processed all chunks
    uint32_t chunks_in_new_round = new_round * uint32_t(slots_per_round_);
    if (chunks_in_new_round >= total_chunks_) {
        // All chunks assigned - no need to advance
        resetting_.store(false, std::memory_order_release);
        return;
    }

    spdlog::debug("[chunk_coordinator] Advancing to round {} (chunks {}-{})",
        new_round, chunks_in_new_round,
        std::min(chunks_in_new_round + uint32_t(slots_per_round_) - 1, total_chunks_ - 1));

    // Reset all slots to FREE
    for (size_t i = 0; i < slots_per_round_; ++i) {
        slots_[i].store(FREE, std::memory_order_relaxed);
        slot_times_[i].store(0, std::memory_order_relaxed);
    }

    // Advance round counter
    round_.store(new_round, std::memory_order_release);

    // Release reset lock
    resetting_.store(false, std::memory_order_release);
}

uint64_t chunk_coordinator::now_ms() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace kth::node::sync
