// Copyright (c) 2016-present Knuth Project developers.
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
    , index_(index)
    , start_height_(start_height)
    , end_height_(end_height)
    , total_chunks_((end_height - start_height + config.chunk_size) / config.chunk_size)
    , slots_(total_chunks_)
    , slot_times_(total_chunks_)
{
    // Initialize all slots to FREE
    for (size_t i = 0; i < slots_.size(); ++i) {
        slots_[i].store(FREE, std::memory_order_relaxed);
        slot_times_[i].store(0, std::memory_order_relaxed);
    }

    auto const mem_kb = (slots_.size() * bytes_per_slot) / 1024;
    spdlog::info("[chunk_coordinator] Created: blocks {}-{}, {} chunks ({} KB)",
        start_height, end_height, total_chunks_, mem_kb);
}

// =============================================================================
// Peer Interface (Lock-Free)
// =============================================================================

std::optional<uint32_t> chunk_coordinator::claim_chunk() {
    if (stopped_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // Search for a FREE slot using CAS
    for (size_t i = 0; i < slots_.size(); ++i) {
        uint8_t expected = FREE;
        if (slots_[i].compare_exchange_strong(expected, IN_PROGRESS,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            // Got slot i - record assignment time
            slot_times_[i].store(now_ms(), std::memory_order_release);

            auto const chunk_id = uint32_t(i);
            auto const [block_start, block_end] = chunk_range(chunk_id);
            spdlog::debug("[chunk_coordinator] Claimed chunk {} (blocks {}-{})",
                chunk_id, block_start, block_end);

            return chunk_id;
        }
    }

    // Every chunk is in progress or completed: the caller waits and retries,
    // or leaves once is_complete() says so.
    return std::nullopt;
}

std::pair<uint32_t, uint32_t> chunk_coordinator::chunk_range(uint32_t chunk_id) const {
    uint32_t block_start = start_height_ + chunk_id * uint32_t(config_.chunk_size);
    uint32_t block_end = std::min(block_start + uint32_t(config_.chunk_size) - 1, end_height_);
    return {block_start, block_end};
}

hash_digest chunk_coordinator::get_block_hash(uint32_t height) const {
    // Address by height through the active chain: the index also holds side
    // branches, numbered in arrival order, so an entry's index is not its height.
    auto const idx = index_.active_at(static_cast<int32_t>(height));
    if (idx == blockchain::header_index::null_index) {
        return null_hash;
    }
    return index_.get_hash(idx);
}

void chunk_coordinator::chunk_completed(uint32_t chunk_id) {
    if (chunk_id >= slots_.size()) {
        return;
    }

    // A chunk that stalled is freed and claimed again, so two peers can report
    // the same one, and the first can report it while the slot is FREE or
    // already taken by the second. Whichever state it leaves, the blocks were
    // delivered; the exchange tells the first report from the later ones.
    auto const previous = slots_[chunk_id].exchange(COMPLETED, std::memory_order_acq_rel);
    if (previous == COMPLETED) {
        spdlog::debug("[chunk_coordinator] Chunk {} completed again, already counted", chunk_id);
        return;
    }
    chunks_completed_.fetch_add(1, std::memory_order_relaxed);

    spdlog::debug("[chunk_coordinator] Chunk {} completed", chunk_id);
}

void chunk_coordinator::chunk_failed(uint32_t chunk_id) {
    if (chunk_id >= slots_.size()) {
        return;
    }

    // Reset to FREE so another peer can take it. A chunk another peer already
    // completed stays completed.
    uint8_t expected = IN_PROGRESS;
    if (slots_[chunk_id].compare_exchange_strong(expected, FREE,
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

    for (size_t i = 0; i < slots_.size(); ++i) {
        if (slots_[i].load(std::memory_order_acquire) == IN_PROGRESS) {
            uint64_t assigned_at = slot_times_[i].load(std::memory_order_acquire);
            if (assigned_at > 0 && (now - assigned_at) > timeout_ms) {
                // Slot stalled - reset to FREE
                uint8_t expected = IN_PROGRESS;
                if (slots_[i].compare_exchange_strong(expected, FREE,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    spdlog::warn("[chunk_coordinator] Chunk {} timed out after {}s, reset to FREE",
                        i, config_.stall_timeout_secs);
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
    // After the flag, so a worker woken by the token observes is_stopped().
    // std::stop_source is thread safe and idempotent.
    stop_source_.request_stop();
}

chunk_coordinator::progress chunk_coordinator::get_progress() const {
    uint32_t in_progress = 0;
    for (size_t i = 0; i < slots_.size(); ++i) {
        uint8_t state = slots_[i].load(std::memory_order_acquire);
        if (state == IN_PROGRESS) ++in_progress;
    }

    return {
        .total_chunks = total_chunks_,
        .chunks_completed = chunks_completed_.load(std::memory_order_acquire),
        .chunks_in_progress = in_progress,
        .start_height = start_height_,
        .end_height = end_height_
    };
}

// =============================================================================
// Private Helpers
// =============================================================================

uint64_t chunk_coordinator::now_ms() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace kth::node::sync
