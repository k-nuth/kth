// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/header_chunk_coordinator.hpp>

#include <algorithm>
#include <thread>

#include <spdlog/spdlog.h>

namespace kth::node::sync {

header_chunk_coordinator::header_chunk_coordinator(
    uint32_t start_height,
    header_chunk_config const& config)
    : config_(config)
    , start_height_(start_height)
    , num_slots_(config.speculative_chunks * config.max_peers)
    , slots_(num_slots_)
    , slot_times_(num_slots_)
    , next_chunk_height_(start_height)
    , validated_height_(start_height)
    , discovered_height_(start_height)
{
    // Initialize all slots to FREE
    for (size_t i = 0; i < num_slots_; ++i) {
        slots_[i].store(FREE, std::memory_order_relaxed);
        slot_times_[i].store(0, std::memory_order_relaxed);
    }

    spdlog::debug("[header_chunk_coordinator] Created: start_height={}, chunk_size={}, "
        "speculative_chunks={}, num_slots={}",
        start_height, config.chunk_size, config.speculative_chunks, num_slots_);
}

// =============================================================================
// Peer Interface (Lock-Free)
// =============================================================================

std::optional<uint32_t> header_chunk_coordinator::claim_chunk() {
    if (stopped_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    if (tip_reached_.load(std::memory_order_acquire)) {
        // We've reached the tip, no more chunks to claim
        return std::nullopt;
    }

    // Use discovered_height for speculation limit (simpler - validation doesn't need coordinator)
    // This is fine because header validation is fast (just index updates)
    uint32_t discovered = discovered_height_.load(std::memory_order_acquire);
    uint32_t max_speculative = discovered + config_.speculative_chunks * config_.chunk_size;

    // Try to claim the next chunk
    uint32_t current = next_chunk_height_.load(std::memory_order_acquire);

    // Check speculation limit
    if (current >= max_speculative) {
        spdlog::debug("[header_chunk_coordinator] At speculation limit: current={}, discovered={}, max={}",
            current, discovered, max_speculative);
        return std::nullopt;
    }

    // Try to atomically claim this chunk
    if (!next_chunk_height_.compare_exchange_strong(current, current + config_.chunk_size,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        // Another peer claimed it, caller should retry
        return std::nullopt;
    }

    // We claimed chunk starting at 'current', now mark the slot as IN_PROGRESS
    size_t slot = height_to_slot(current);

    // The slot might still be COMPLETED from a previous round - that's ok, we overwrite it
    slots_[slot].store(IN_PROGRESS, std::memory_order_release);
    slot_times_[slot].store(now_ms(), std::memory_order_release);

    spdlog::debug("[header_chunk_coordinator] Claimed chunk at height {} (slot {})",
        current, slot);

    return current;
}

void header_chunk_coordinator::report_chunk_completed(uint32_t start_height, uint32_t headers_received) {
    size_t slot = height_to_slot(start_height);

    // Mark slot as completed
    slots_[slot].store(COMPLETED, std::memory_order_release);

    // Update discovered height
    uint32_t new_discovered = start_height + headers_received;
    uint32_t old_discovered = discovered_height_.load(std::memory_order_acquire);
    while (new_discovered > old_discovered) {
        if (discovered_height_.compare_exchange_weak(old_discovered, new_discovered,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }

    // Check if we hit the tip (received fewer headers than requested)
    if (headers_received < config_.chunk_size) {
        tip_reached_.store(true, std::memory_order_release);
        spdlog::info("[header_chunk_coordinator] Tip reached at height {} (received {} headers)",
            new_discovered, headers_received);
    }

    spdlog::debug("[header_chunk_coordinator] Chunk completed: start={}, received={}, discovered={}",
        start_height, headers_received, new_discovered);
}

void header_chunk_coordinator::report_chunk_failed(uint32_t start_height) {
    size_t slot = height_to_slot(start_height);

    // Only reset if still IN_PROGRESS (might have timed out already)
    uint8_t expected = IN_PROGRESS;
    if (slots_[slot].compare_exchange_strong(expected, FREE,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {
        spdlog::debug("[header_chunk_coordinator] Chunk failed at height {}, reset to FREE for retry",
            start_height);
    }
}

// =============================================================================
// Validation Interface
// =============================================================================

void header_chunk_coordinator::report_validated(uint32_t height) {
    uint32_t old_validated = validated_height_.load(std::memory_order_acquire);
    while (height > old_validated) {
        if (validated_height_.compare_exchange_weak(old_validated, height,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            spdlog::debug("[header_chunk_coordinator] Validated up to height {}", height);
            break;
        }
    }
}

// =============================================================================
// Status
// =============================================================================

void header_chunk_coordinator::check_timeouts() {
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t now = now_ms();
    uint64_t timeout_ms = uint64_t(config_.stall_timeout_secs) * 1000;

    for (size_t i = 0; i < num_slots_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == IN_PROGRESS) {
            uint64_t assigned_at = slot_times_[i].load(std::memory_order_acquire);
            if (assigned_at > 0 && (now - assigned_at) > timeout_ms) {
                // Slot stalled - reset to FREE
                uint8_t expected = IN_PROGRESS;
                if (slots_[i].compare_exchange_strong(expected, FREE,
                        std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    spdlog::warn("[header_chunk_coordinator] Slot {} timed out after {}s, reset to FREE",
                        i, config_.stall_timeout_secs);
                }
            }
        }
    }
}

bool header_chunk_coordinator::tip_reached() const {
    return tip_reached_.load(std::memory_order_acquire);
}

bool header_chunk_coordinator::is_complete() const {
    if (!tip_reached_.load(std::memory_order_acquire)) {
        return false;
    }

    // Tip reached - check if all validated
    uint32_t validated = validated_height_.load(std::memory_order_acquire);
    uint32_t discovered = discovered_height_.load(std::memory_order_acquire);
    return validated >= discovered;
}

bool header_chunk_coordinator::is_stopped() const {
    return stopped_.load(std::memory_order_acquire);
}

void header_chunk_coordinator::stop() {
    stopped_.store(true, std::memory_order_release);
}

header_chunk_coordinator::progress header_chunk_coordinator::get_progress() const {
    // Count in-progress slots
    uint32_t in_progress = 0;
    for (size_t i = 0; i < num_slots_; ++i) {
        if (slots_[i].load(std::memory_order_acquire) == IN_PROGRESS) {
            ++in_progress;
        }
    }

    return {
        .start_height = start_height_,
        .validated_height = validated_height_.load(std::memory_order_acquire),
        .discovered_height = discovered_height_.load(std::memory_order_acquire),
        .next_chunk_height = next_chunk_height_.load(std::memory_order_acquire),
        .chunks_in_progress = in_progress,
        .tip_reached = tip_reached_.load(std::memory_order_acquire)
    };
}

// =============================================================================
// Private Helpers
// =============================================================================

size_t header_chunk_coordinator::height_to_slot(uint32_t height) const {
    // Circular buffer: slot = ((height - start_height) / chunk_size) % num_slots
    uint32_t chunk_index = (height - start_height_) / config_.chunk_size;
    return chunk_index % num_slots_;
}

uint64_t header_chunk_coordinator::now_ms() {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

} // namespace kth::node::sync
