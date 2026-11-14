// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/node/sync/chunk_coordinator.hpp>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace kth::node::sync {

// =============================================================================
// download_range
// =============================================================================

hash_digest download_range::get_block_hash(uint32_t height) const {
    // Address by height through the active chain: the index also holds side
    // branches, numbered in arrival order, so an entry's index is not its height.
    auto const idx = index_.active_at(static_cast<int32_t>(height));
    if (idx == blockchain::header_index::null_index) {
        return null_hash;
    }
    return index_.get_hash(idx);
}

// =============================================================================
// chunk_coordinator
// =============================================================================

chunk_coordinator::chunk_coordinator(
    uint32_t start_height,
    uint32_t end_height,
    chunk_coordinator_config const& config)
    : config_(config)
    , start_height_(start_height)
    , end_height_(end_height)
    , total_chunks_(uint32_t((end_height - start_height + config.chunk_size) / config.chunk_size))
    , slots_(total_chunks_)
{
    spdlog::info("[chunk_coordinator] Created: blocks {}-{}, {} chunks ({} KB)",
        start_height, end_height, total_chunks_, (slots_.size() * sizeof(slot)) / 1024);
}

// -----------------------------------------------------------------------------
// Requests from workers
// -----------------------------------------------------------------------------

void chunk_coordinator::request_chunk(chunk_reply_channel_ptr reply, time_point now) {
    if ( ! reply) {
        return;
    }
    if (is_complete()) {
        reply->try_send(chunk_reply{});
        return;
    }
    // Behind the requests already waiting, which are served first.
    waiting_.push_back(std::move(reply));
    serve_waiting(now);
}

void chunk_coordinator::dismiss_waiting() {
    for (auto const& reply : waiting_) {
        reply->try_send(chunk_reply{});
    }
    waiting_.clear();
}

void chunk_coordinator::serve_waiting(time_point now) {
    while ( ! waiting_.empty()) {
        auto const claim = claim_chunk(now);
        if ( ! claim) {
            break;
        }
        auto const [block_start, block_end] = chunk_range(claim->chunk_id);
        auto const reply = std::move(waiting_.front());
        waiting_.pop_front();
        if ( ! reply->try_send(chunk_reply{chunk_assignment{*claim, block_start, block_end}})) {
            // The worker's channel is closed or still holds an answer it never
            // took: nobody will download this chunk. Free it for the next one.
            spdlog::debug("[chunk_coordinator] Chunk {} could not be handed to its worker", claim->chunk_id);
            release(claim->chunk_id);
        }
    }
}

// -----------------------------------------------------------------------------
// Chunk state
// -----------------------------------------------------------------------------

std::optional<chunk_claim> chunk_coordinator::claim_chunk(time_point now) {
    while (completed_floor_ < slots_.size() && slots_[completed_floor_].state == COMPLETED) {
        ++completed_floor_;
    }
    free_floor_ = std::max(free_floor_, completed_floor_);
    while (free_floor_ < slots_.size() && slots_[free_floor_].state != FREE) {
        ++free_floor_;
    }
    if (free_floor_ >= slots_.size()) {
        // Every chunk is held or completed.
        return std::nullopt;
    }

    auto const chunk_id = free_floor_;
    auto& taken = slots_[chunk_id];
    taken.state = IN_PROGRESS;
    ++taken.number;
    ++chunks_held_;
    ++free_floor_;

    chunk_claim const claim{.chunk_id = chunk_id, .number = taken.number};
    pending_.push_back({now + std::chrono::seconds(config_.stall_timeout_secs), claim});

    auto const [block_start, block_end] = chunk_range(chunk_id);
    spdlog::debug("[chunk_coordinator] Claimed chunk {} (blocks {}-{})", chunk_id, block_start, block_end);
    return claim;
}

std::pair<uint32_t, uint32_t> chunk_coordinator::chunk_range(uint32_t chunk_id) const {
    uint32_t block_start = start_height_ + chunk_id * uint32_t(config_.chunk_size);
    uint32_t block_end = std::min(block_start + uint32_t(config_.chunk_size) - 1, end_height_);
    return {block_start, block_end};
}

bool chunk_coordinator::is_pending(chunk_claim claim) const {
    if (claim.chunk_id >= slots_.size()) {
        return false;
    }
    auto const& current = slots_[claim.chunk_id];
    return current.state == IN_PROGRESS && current.number == claim.number;
}

bool chunk_coordinator::holds(chunk_claim claim) const {
    if (claim.chunk_id >= slots_.size()) {
        return false;
    }
    auto const& current = slots_[claim.chunk_id];
    return (current.state == IN_PROGRESS || current.state == DELIVERING) && current.number == claim.number;
}

void chunk_coordinator::release(uint32_t chunk_id) {
    slots_[chunk_id].state = FREE;
    --chunks_held_;
    free_floor_ = std::min(free_floor_, chunk_id);
}

void chunk_coordinator::chunk_downloaded(chunk_claim claim) {
    if (is_pending(claim)) {
        slots_[claim.chunk_id].state = DELIVERING;
    }
}

void chunk_coordinator::chunk_completed(chunk_claim claim) {
    if (claim.chunk_id >= slots_.size()) {
        return;
    }

    // A chunk that stalled is freed and claimed again, so two peers can report
    // the same one, and the first can report it while the slot is FREE or
    // already taken by the second. Whichever state it leaves, the blocks were
    // delivered; only the first report counts.
    auto& current = slots_[claim.chunk_id];
    if (current.state == COMPLETED) {
        spdlog::debug("[chunk_coordinator] Chunk {} completed again, already counted", claim.chunk_id);
        return;
    }
    if (current.state != FREE) {
        --chunks_held_;
    }
    current.state = COMPLETED;
    ++chunks_completed_;
    spdlog::debug("[chunk_coordinator] Chunk {} completed", claim.chunk_id);

    if (is_complete()) {
        // Nothing is left to hand out: the workers still waiting can leave.
        dismiss_waiting();
    }
}

void chunk_coordinator::chunk_failed(chunk_claim claim, time_point now) {
    // Only by the peer that holds it: a claim that stalled and was handed on
    // names a hold that is gone, and a completed chunk stays completed.
    if ( ! holds(claim)) {
        return;
    }
    release(claim.chunk_id);
    spdlog::debug("[chunk_coordinator] Chunk {} failed, reset to FREE for retry", claim.chunk_id);
    serve_waiting(now);
}

// -----------------------------------------------------------------------------
// Stalled requests
// -----------------------------------------------------------------------------

std::optional<chunk_coordinator::time_point> chunk_coordinator::next_deadline() {
    // Requests that were answered, given back or handed on are no longer
    // pending: their entries are dropped when they reach the front.
    while ( ! pending_.empty() && ! is_pending(pending_.front().claim)) {
        pending_.pop_front();
    }
    if (pending_.empty()) {
        return std::nullopt;
    }
    return pending_.front().deadline;
}

void chunk_coordinator::check_timeouts(time_point now) {
    bool freed = false;
    while ( ! pending_.empty()) {
        auto const front = pending_.front();
        if (is_pending(front.claim)) {
            if (front.deadline > now) {
                break;
            }
            release(front.claim.chunk_id);
            freed = true;
            spdlog::warn("[chunk_coordinator] Chunk {} timed out after {}s, reset to FREE",
                front.claim.chunk_id, config_.stall_timeout_secs);
        }
        pending_.pop_front();
    }
    if (freed) {
        serve_waiting(now);
    }
}

// -----------------------------------------------------------------------------
// Status
// -----------------------------------------------------------------------------

chunk_coordinator::progress chunk_coordinator::get_progress() const {
    return {
        .total_chunks = total_chunks_,
        .chunks_completed = chunks_completed_,
        .chunks_in_progress = chunks_held_,
        .start_height = start_height_,
        .end_height = end_height_
    };
}

} // namespace kth::node::sync
