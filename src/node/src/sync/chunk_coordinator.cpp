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
{
    // Initialize all slots to FREE
    for (auto& slot : slots_) {
        slot.store(pack(FREE, 0, 0), std::memory_order_relaxed);
    }

    auto const mem_kb = (slots_.size() * bytes_per_slot) / 1024;
    spdlog::info("[chunk_coordinator] Created: blocks {}-{}, {} chunks ({} KB)",
        start_height, end_height, total_chunks_, mem_kb);
}

// =============================================================================
// Peer Interface (Lock-Free)
// =============================================================================

std::optional<chunk_claim> chunk_coordinator::claim_chunk() {
    if (stopped_.load(std::memory_order_acquire)) {
        return std::nullopt;
    }

    // Start at the first chunk that may not be completed and move that mark
    // over the completed chunks found on the way.
    auto const floor = completed_floor_.load(std::memory_order_acquire);
    auto new_floor = floor;
    bool prefix_completed = true;

    std::optional<chunk_claim> claim;
    uint64_t now = 0;
    for (size_t i = floor; i < slots_.size() && ! claim; ++i) {
        auto word = slots_[i].load(std::memory_order_acquire);
        auto const state = state_of(word);
        if (prefix_completed && state == COMPLETED) {
            new_floor = uint32_t(i) + 1;
            continue;
        }
        prefix_completed = false;
        if (state != FREE) {
            continue;
        }

        if (now == 0) {
            now = now_ms();
        }
        auto const number = (number_of(word) + 1) & uint32_t(number_mask);
        if (slots_[i].compare_exchange_strong(word, pack(IN_PROGRESS, number, now),
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            claim = chunk_claim{.chunk_id = uint32_t(i), .number = number};
        }
        // Otherwise another peer took it first: go on to the next one.
    }

    // Forward only: a concurrent claim may have moved it further already.
    auto seen = floor;
    while (seen < new_floor && ! completed_floor_.compare_exchange_weak(seen, new_floor,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
    }

    if (claim) {
        auto const [block_start, block_end] = chunk_range(claim->chunk_id);
        spdlog::debug("[chunk_coordinator] Claimed chunk {} (blocks {}-{})",
            claim->chunk_id, block_start, block_end);
    }

    // No claim: every chunk is held or completed. The caller waits and retries,
    // or leaves once is_complete() says so.
    return claim;
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

void chunk_coordinator::chunk_downloaded(chunk_claim claim) {
    if (claim.chunk_id >= slots_.size()) {
        return;
    }

    // One attempt is enough: the only thing that can change a held slot under
    // its holder is the stall check freeing it, and then there is no hold left
    // to mark.
    auto word = slots_[claim.chunk_id].load(std::memory_order_acquire);
    if (state_of(word) == IN_PROGRESS && number_of(word) == claim.number) {
        slots_[claim.chunk_id].compare_exchange_strong(word,
            pack(DELIVERING, claim.number, time_of(word)),
            std::memory_order_acq_rel, std::memory_order_relaxed);
    }
}

void chunk_coordinator::chunk_completed(chunk_claim claim) {
    if (claim.chunk_id >= slots_.size()) {
        return;
    }

    // A chunk that stalled is freed and claimed again, so two peers can report
    // the same one, and the first can report it while the slot is FREE or
    // already taken by the second. Whichever state it leaves, the blocks were
    // delivered; the exchange tells the first report from the later ones.
    auto const previous = slots_[claim.chunk_id].exchange(
        pack(COMPLETED, 0, 0), std::memory_order_acq_rel);
    if (state_of(previous) == COMPLETED) {
        spdlog::debug("[chunk_coordinator] Chunk {} completed again, already counted", claim.chunk_id);
        return;
    }
    chunks_completed_.fetch_add(1, std::memory_order_relaxed);

    spdlog::debug("[chunk_coordinator] Chunk {} completed", claim.chunk_id);
}

void chunk_coordinator::chunk_failed(chunk_claim claim) {
    if (claim.chunk_id >= slots_.size()) {
        return;
    }

    // Reset to FREE so another peer can take it, but only by the peer that
    // holds it: a claim that stalled and was handed on names a hold that is
    // gone, and a completed chunk stays completed.
    auto word = slots_[claim.chunk_id].load(std::memory_order_acquire);
    while (holds(word, claim)) {
        if (slots_[claim.chunk_id].compare_exchange_weak(word, pack(FREE, claim.number, 0),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            spdlog::debug("[chunk_coordinator] Chunk {} failed, reset to FREE for retry", claim.chunk_id);
            return;
        }
    }
}

// =============================================================================
// Status
// =============================================================================

void chunk_coordinator::check_timeouts() {
    check_timeouts(std::chrono::steady_clock::now());
}

void chunk_coordinator::check_timeouts(std::chrono::steady_clock::time_point time) {
    if (stopped_.load(std::memory_order_acquire)) {
        return;
    }

    uint64_t const now = to_ms(time);
    uint64_t const timeout_ms = uint64_t(config_.stall_timeout_secs) * 1000;

    for (size_t i = completed_floor_.load(std::memory_order_acquire); i < slots_.size(); ++i) {
        auto word = slots_[i].load(std::memory_order_acquire);
        // Only a request can stall. The claim time travels in the same word as
        // the state, so the swap frees this claim or nothing: a chunk claimed
        // again in between has another word.
        if (state_of(word) != IN_PROGRESS) {
            continue;
        }
        // A chunk claimed after the clock was read is newer than `now`, and the
        // unsigned difference would wrap into a huge age.
        auto const claimed_at = time_of(word);
        if (now <= claimed_at || now - claimed_at <= timeout_ms) {
            continue;
        }
        if (slots_[i].compare_exchange_strong(word, pack(FREE, number_of(word), 0),
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            spdlog::warn("[chunk_coordinator] Chunk {} timed out after {}s, reset to FREE",
                i, config_.stall_timeout_secs);
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
    for (size_t i = completed_floor_.load(std::memory_order_acquire); i < slots_.size(); ++i) {
        auto const state = state_of(slots_[i].load(std::memory_order_acquire));
        if (state == IN_PROGRESS || state == DELIVERING) ++in_progress;
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
    return to_ms(std::chrono::steady_clock::now());
}

uint64_t chunk_coordinator::to_ms(std::chrono::steady_clock::time_point time) {
    return uint64_t(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time.time_since_epoch()
        ).count()
    );
}

} // namespace kth::node::sync
