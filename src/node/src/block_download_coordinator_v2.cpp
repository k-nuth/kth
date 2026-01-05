// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/block_download_coordinator_v2.hpp>

#include <algorithm>
#include <thread>

#include <spdlog/spdlog.h>

namespace kth::node {

block_download_coordinator_v2::block_download_coordinator_v2(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    uint32_t start_height,
    uint32_t target_height,
    ::asio::any_io_executor executor,
    parallel_download_config_v2 const& config)
    : config_(config)
    , slots_per_round_(config.max_peers * config.slots_multiplier)
    , chain_(chain)
    , organizer_(organizer)
    , start_height_(start_height)
    , target_height_(target_height)
    , total_chunks_((target_height - start_height + config.chunk_size) / config.chunk_size)
    , slots_(slots_per_round_)
    , slot_times_(slots_per_round_)
    , next_height_to_validate_(start_height)
    , start_time_(std::chrono::steady_clock::now())
    , validation_queue_(std::make_unique<validation_channel>(executor, 1024))
{
    // Initialize all slots to FREE
    for (size_t i = 0; i < slots_per_round_; ++i) {
        slots_[i].store(FREE, std::memory_order_relaxed);
        slot_times_[i].store(0, std::memory_order_relaxed);
    }

    spdlog::debug("[coordinator_v2] Created for blocks {}-{} ({} blocks, {} chunks, {} slots/round)",
        start_height, target_height, target_height - start_height + 1,
        total_chunks_, slots_per_round_);
}

block_download_coordinator_v2::~block_download_coordinator_v2() {
    stop();
}

// =============================================================================
// Peer Interface (Lock-Free)
// =============================================================================

std::optional<uint32_t> block_download_coordinator_v2::claim_chunk() {
    if (stopped_.load(std::memory_order_acquire) || failed_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // Wait if round is being reset
    while (resetting_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    uint32_t r = round_.load(std::memory_order_acquire);

    // Search for a FREE slot
    for (size_t i = 0; i < slots_per_round_; ++i) {
        uint8_t expected = FREE;
        if (slots_[i].compare_exchange_strong(expected, IN_PROGRESS,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            // Got slot i
            slot_times_[i].store(now_ms(), std::memory_order_release);

            uint32_t chunk_id = r * slots_per_round_ + i;

            // Check if this chunk is beyond our target
            auto [block_start, block_end] = chunk_range(chunk_id);
            if (block_start > target_height_) {
                // No more chunks needed - mark as completed and return nullopt
                slots_[i].store(COMPLETED, std::memory_order_release);
                return std::nullopt;
            }

            spdlog::debug("[coordinator_v2] Claimed chunk {} (slot {}, round {}, blocks {}-{})",
                chunk_id, i, r, block_start, block_end);

            return chunk_id;
        }
    }

    // All slots occupied - try to advance round
    // First check if all are COMPLETED
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

    // Some slots are IN_PROGRESS - wait for them or timeout will reset them
    spdlog::debug("[coordinator_v2] All slots occupied, waiting...");
    return std::nullopt;
}

std::pair<uint32_t, uint32_t> block_download_coordinator_v2::chunk_range(uint32_t chunk_id) const {
    uint32_t block_start = start_height_ + chunk_id * config_.chunk_size;
    uint32_t block_end = std::min(block_start + uint32_t(config_.chunk_size) - 1, target_height_);
    return {block_start, block_end};
}

hash_digest block_download_coordinator_v2::get_block_hash(uint32_t height) const {
    return organizer_.index().get_hash(static_cast<blockchain::header_index::index_t>(height));
}

void block_download_coordinator_v2::chunk_completed(uint32_t chunk_id) {
    uint32_t r = chunk_id / slots_per_round_;
    size_t slot = chunk_id % slots_per_round_;

    uint32_t current_round = round_.load(std::memory_order_acquire);
    if (r != current_round) {
        // Old round - ignore
        spdlog::debug("[coordinator_v2] Chunk {} completed but from old round {} (current: {})",
            chunk_id, r, current_round);
        return;
    }

    // Mark as completed
    slots_[slot].store(COMPLETED, std::memory_order_release);
    chunks_completed_.fetch_add(1, std::memory_order_relaxed);

    spdlog::debug("[coordinator_v2] Chunk {} completed (slot {})", chunk_id, slot);

    // Try to advance round if all completed
    try_advance_round();
}

void block_download_coordinator_v2::chunk_failed(uint32_t chunk_id) {
    uint32_t r = chunk_id / slots_per_round_;
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
        spdlog::info("[coordinator_v2] Chunk {} failed, reset to FREE", chunk_id);
    }
}

// =============================================================================
// Block Reception & Validation
// =============================================================================

void block_download_coordinator_v2::block_received(uint32_t height, block_const_ptr block) {
    std::lock_guard lock(validation_mutex_);

    if (stopped_ || failed_) {
        return;
    }

    // Store in pending buffer
    pending_blocks_[height] = std::move(block);

    spdlog::trace("[coordinator_v2] Block {} received, {} pending, next to validate: {}",
        height, pending_blocks_.size(), next_height_to_validate_);

    // Try to push ready blocks to validation
    flush_pending_to_validation();
}

::asio::awaitable<std::optional<std::pair<uint32_t, block_download_coordinator_v2::block_const_ptr>>>
block_download_coordinator_v2::next_block_to_validate()
{
    if (stopped_ || failed_) {
        co_return std::nullopt;
    }

    auto [ec, block_pair] = co_await validation_queue_->async_receive(
        ::asio::as_tuple(::asio::use_awaitable));

    if (ec) {
        co_return std::nullopt;
    }

    co_return block_pair;
}

void block_download_coordinator_v2::validation_complete(uint32_t height, code result) {
    if (result != error::success) {
        spdlog::error("[coordinator_v2] Validation failed at height {}: {}",
            height, result.message());
        set_failed(result);
        return;
    }

    uint32_t validated = blocks_validated_.fetch_add(1, std::memory_order_relaxed) + 1;

    // Check if we're done
    if (validated >= (target_height_ - start_height_ + 1)) {
        spdlog::info("[coordinator_v2] All {} blocks validated",
            target_height_ - start_height_ + 1);
        validation_queue_->close();
    }
}

// =============================================================================
// Status & Control
// =============================================================================

void block_download_coordinator_v2::check_timeouts() {
    if (stopped_ || failed_) {
        return;
    }

    uint64_t now = now_ms();
    uint64_t timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.stall_timeout).count();

    for (size_t i = 0; i < slots_per_round_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == IN_PROGRESS) {
            uint64_t assigned_at = slot_times_[i].load(std::memory_order_acquire);
            if (assigned_at > 0 && (now - assigned_at) > timeout_ms) {
                // Slot stalled - reset to FREE
                uint8_t expected = IN_PROGRESS;
                if (slots_[i].compare_exchange_strong(expected, FREE,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    uint32_t chunk_id = round_.load(std::memory_order_acquire) * slots_per_round_ + i;
                    spdlog::warn("[coordinator_v2] Chunk {} (slot {}) timed out, reset to FREE",
                        chunk_id, i);
                }
            }
        }
    }
}

bool block_download_coordinator_v2::is_complete() const {
    auto validated = blocks_validated_.load(std::memory_order_acquire);
    auto total = target_height_ - start_height_ + 1;
    bool complete = validated >= total;
    spdlog::info("[coordinator_v2] is_complete() at {}: validated={}, total={}, start={}, target={}, result={}",
        static_cast<void const*>(this), validated, total, start_height_, target_height_, complete);
    return complete;
}

bool block_download_coordinator_v2::has_failed() const {
    return failed_.load(std::memory_order_acquire);
}

bool block_download_coordinator_v2::is_stopped() const {
    return stopped_.load(std::memory_order_acquire);
}

code block_download_coordinator_v2::failure_reason() const {
    return failure_reason_;
}

void block_download_coordinator_v2::stop() {
    if (stopped_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    if (validation_queue_) {
        validation_queue_->close();
    }
}

void block_download_coordinator_v2::peer_started() {
    spdlog::info("[coordinator_v2] peer_started() called on coordinator at {}", static_cast<void const*>(this));
    auto prev = active_peers_.fetch_add(1, std::memory_order_relaxed);
    spdlog::info("[coordinator_v2] peer_started() completed, was {}, now {}", prev, prev + 1);
}

void block_download_coordinator_v2::peer_stopped() {
    active_peers_.fetch_sub(1, std::memory_order_relaxed);
}

block_download_coordinator_v2::progress block_download_coordinator_v2::get_progress() const {
    // Count assigned and completed slots in current round
    uint32_t in_progress = 0;
    uint32_t completed = 0;
    for (size_t i = 0; i < slots_per_round_; ++i) {
        uint8_t state = slots_[i].load(std::memory_order_acquire);
        if (state == IN_PROGRESS) ++in_progress;
        else if (state == COMPLETED) ++completed;
    }

    // Get pending blocks count
    uint32_t pending = 0;
    {
        std::lock_guard lock(const_cast<std::mutex&>(validation_mutex_));
        pending = uint32_t(pending_blocks_.size());
    }

    return {
        .chunks_assigned = in_progress + completed,
        .chunks_completed = chunks_completed_.load(std::memory_order_acquire),
        .chunks_in_progress = in_progress,
        .blocks_validated = blocks_validated_.load(std::memory_order_acquire),
        .blocks_pending = pending,
        .current_round = round_.load(std::memory_order_acquire),
        .start_height = start_height_,
        .target_height = target_height_,
        .active_peers = active_peers_.load(std::memory_order_acquire),
        .start_time = start_time_
    };
}

// =============================================================================
// Private Helpers
// =============================================================================

void block_download_coordinator_v2::try_advance_round() {
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
    uint32_t chunks_in_new_round = new_round * slots_per_round_;
    if (chunks_in_new_round >= total_chunks_) {
        // All chunks assigned - no need to advance, sync is almost done
        resetting_.store(false, std::memory_order_release);
        return;
    }

    spdlog::info("[coordinator_v2] Advancing to round {} (chunks {}-{})",
        new_round, chunks_in_new_round, chunks_in_new_round + slots_per_round_ - 1);

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

void block_download_coordinator_v2::flush_pending_to_validation() {
    // Must be called with validation_mutex_ held

    while (!pending_blocks_.empty()) {
        auto it = pending_blocks_.find(next_height_to_validate_);
        if (it == pending_blocks_.end()) {
            break;  // Next block not yet received
        }

        auto block_pair = std::make_pair(next_height_to_validate_, it->second);

        bool sent = validation_queue_->try_send(std::error_code{}, std::move(block_pair));
        if (!sent) {
            // Channel full - try again later
            break;
        }

        pending_blocks_.erase(it);
        ++next_height_to_validate_;
    }
}

void block_download_coordinator_v2::set_failed(code reason) {
    if (failed_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    failure_reason_ = reason;
    spdlog::error("[coordinator_v2] Sync failed: {}", reason.message());

    if (validation_queue_) {
        validation_queue_->close();
    }
}

uint64_t block_download_coordinator_v2::now_ms() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace kth::node
