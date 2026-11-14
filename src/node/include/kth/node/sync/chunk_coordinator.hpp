// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP
#define KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP

// =============================================================================
// Chunk assignment for parallel block downloads
// =============================================================================
//
// A range of blocks is downloaded in chunks, each by one peer at a time. Two
// types take part, and they are deliberately separate:
//
// - chunk_coordinator is the state of a range: which chunk is free, held or
//   delivered, and which workers are waiting for one. It has ONE OWNER, the
//   download supervisor, and is plain data: no atomics, no locks. Nothing but
//   the supervisor's coroutine calls it.
//
// - download_range is what the workers of a range share: the fixed description
//   of the range, the header index to read hashes from, and the stop of the
//   range. It holds no chunk state.
//
// A worker never touches the coordinator. It talks to the supervisor through
// the workers' output channel (messages.hpp): a chunk_request, answered on the
// worker's own reply channel, and a chunk_report for each thing that happens
// to the chunk it holds.
//
// FLOW:
// -----
// 1. The worker sends chunk_request and suspends on its reply channel
// 2. The supervisor answers with a chunk_assignment as soon as a chunk is free:
//    at once, or when a chunk is given back or stalls. No worker polls
// 3. With the blocks in hand the worker reports `downloaded`, hands them over
//    and reports `completed`; anything else is reported as `failed`
// 4. When every chunk is completed the supervisor answers every waiting and
//    later request with "no more work", and the workers leave
//
// A slot is FREE, IN_PROGRESS (being requested from a peer), DELIVERING
// (downloaded, waiting for the consumer to take it) or COMPLETED.
//
// - A claim names its chunk and its claim number. Only the current claim of a
//   chunk can give it back: a peer whose claim was handed to another peer
//   cannot free the chunk under the peer that holds it now
// - Failed and stalled chunks reset to FREE for retry by another peer. Only a
//   chunk still being requested can stall; a downloaded one waiting for room
//   downstream is not late
// - A chunk counts as completed once, whoever reports it and however often
//
// =============================================================================

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include <kth/blockchain/header_index.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>

namespace kth::node::sync {

/// Configuration for chunk coordinator
struct chunk_coordinator_config {
    /// Blocks per chunk (matches Bitcoin protocol getdata limit)
    size_t chunk_size{16};

    /// How long a request may be pending before its chunk is given to another
    /// peer (seconds)
    uint32_t stall_timeout_secs{15};
};

/// One peer's hold on one chunk. The claim number tells this hold from an
/// earlier one of the same chunk that stalled and was handed on.
struct chunk_claim {
    uint32_t chunk_id{0};
    uint32_t number{0};

    friend bool operator==(chunk_claim const&, chunk_claim const&) = default;
};

/// A chunk given to a worker: the claim to report with, and its blocks.
struct chunk_assignment {
    chunk_claim claim;
    uint32_t start_height{0};
    uint32_t end_height{0};

    friend bool operator==(chunk_assignment const&, chunk_assignment const&) = default;
};

/// The answer to a chunk_request: a chunk, or nullopt when the range has no
/// more work for the worker.
using chunk_reply = std::optional<chunk_assignment>;

/// A worker's reply channel. Capacity one: a worker has one request pending at
/// most, so an answer always finds room. Shared, so an answer to a worker that
/// has just left lands in a channel that still exists.
using chunk_reply_channel = select_channel<chunk_reply>;
using chunk_reply_channel_ptr = std::shared_ptr<chunk_reply_channel>;

// =============================================================================
// download_range: what the workers of a range share
// =============================================================================

struct download_range {
    download_range(blockchain::header_index const& index, uint32_t start_height, uint32_t end_height)
        : index_(index), start_height_(start_height), end_height_(end_height)
    {}

    download_range(download_range const&) = delete;
    download_range& operator=(download_range const&) = delete;

    [[nodiscard]] uint32_t start_height() const { return start_height_; }
    [[nodiscard]] uint32_t end_height() const { return end_height_; }

    /// Get block hash for a height (from header index)
    [[nodiscard]]
    hash_digest get_block_hash(uint32_t height) const;

    /// Active-chain generation at this moment. A chunk is stamped with it when
    /// downloaded so work for a branch abandoned mid-flight can be dropped
    /// instead of stored against the new chain.
    [[nodiscard]]
    uint64_t generation() const { return index_.generation(); }

    /// Stop the range: replaced by another one, or shutdown. Requested by the
    /// supervisor only.
    void stop() { stop_source_.request_stop(); }

    [[nodiscard]]
    bool is_stopped() const { return stop_source_.stop_requested(); }

    /// A token that is stopped by stop(). Workers arm their waits with it: a
    /// selection armed with this token completes with operation_aborted the
    /// moment the range is stopped.
    [[nodiscard]]
    std::stop_token stop_token() const { return stop_source_.get_token(); }

private:
    blockchain::header_index const& index_;
    uint32_t const start_height_;
    uint32_t const end_height_;
    std::stop_source stop_source_;
};

// =============================================================================
// chunk_coordinator: the state of a range. One owner, no synchronization.
// =============================================================================

struct chunk_coordinator {
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    enum slot_state : uint8_t {
        FREE = 0,        // Available for assignment
        IN_PROGRESS = 1, // Assigned, being requested from the peer
        DELIVERING = 2,  // Downloaded, waiting for the consumer to take it
        COMPLETED = 3    // Handed over. Final: a completed chunk never changes
    };

    chunk_coordinator(
        uint32_t start_height,
        uint32_t end_height,
        chunk_coordinator_config const& config = {});

    chunk_coordinator(chunk_coordinator const&) = delete;
    chunk_coordinator& operator=(chunk_coordinator const&) = delete;

    // -------------------------------------------------------------------------
    // Requests from workers
    // -------------------------------------------------------------------------

    /// A worker asks for a chunk. Answered on `reply` at once when a chunk is
    /// free or the range has no more work; otherwise the request waits, and is
    /// answered when a chunk is given back or stalls, or when the range ends.
    /// Requests are served in the order they arrive.
    void request_chunk(chunk_reply_channel_ptr reply, time_point now = clock::now());

    /// Answer every waiting request with "no more work". For a range that is
    /// being stopped or replaced.
    void dismiss_waiting();

    /// Requests waiting for a chunk
    [[nodiscard]]
    size_t waiting() const { return waiting_.size(); }

    // -------------------------------------------------------------------------
    // Chunk state
    // -------------------------------------------------------------------------

    /// Claim the lowest free chunk
    /// @return the claim, or nullopt if every chunk is assigned or completed
    [[nodiscard]]
    std::optional<chunk_claim> claim_chunk(time_point now = clock::now());

    /// Get block range for a chunk
    /// @return {start_height, end_height} for the chunk
    [[nodiscard]]
    std::pair<uint32_t, uint32_t> chunk_range(uint32_t chunk_id) const;

    /// The chunk's blocks have arrived and are about to be handed over. From
    /// here the chunk cannot stall: how long the hand-over waits is decided by
    /// the consumer, not by the peer. Ignored if the claim is no longer the
    /// chunk's current one.
    void chunk_downloaded(chunk_claim claim);

    /// A chunk was handed over. A chunk that stalled is given to a second peer
    /// and both may deliver it: every delivery is valid, and only the first
    /// one counts.
    void chunk_completed(chunk_claim claim);

    /// Give a chunk back (reset to FREE for retry) and hand it to a waiting
    /// request, if any. Ignored unless the claim is the chunk's current one.
    void chunk_failed(chunk_claim claim, time_point now = clock::now());

    // -------------------------------------------------------------------------
    // Stalled requests
    // -------------------------------------------------------------------------

    /// When the oldest pending request stalls, or nullopt if none is pending.
    /// The owner waits until then and calls check_timeouts().
    [[nodiscard]]
    std::optional<time_point> next_deadline();

    /// Reset to FREE the chunks whose request has been pending for the stall
    /// timeout at `now`, and hand them to the waiting requests.
    void check_timeouts(time_point now = clock::now());

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /// Check if all chunks have been completed
    [[nodiscard]]
    bool is_complete() const { return chunks_completed_ >= total_chunks_; }

    struct progress {
        uint32_t total_chunks;
        uint32_t chunks_completed;
        uint32_t chunks_in_progress;
        uint32_t start_height;
        uint32_t end_height;
    };

    [[nodiscard]]
    progress get_progress() const;

private:
    struct slot {
        slot_state state{FREE};
        uint32_t number{0};   // one more on each claim of this chunk
    };

    /// A pending request and when it stalls. Claims are made in time order, so
    /// the queue is ordered by deadline.
    struct pending_request {
        time_point deadline;
        chunk_claim claim;
    };

    [[nodiscard]]
    bool is_pending(chunk_claim claim) const;

    /// True if `claim` names the chunk's current hold (requesting or delivering).
    [[nodiscard]]
    bool holds(chunk_claim claim) const;

    /// Back to FREE, from a hold.
    void release(uint32_t chunk_id);

    /// Hand the free chunks to the waiting requests, oldest request first.
    void serve_waiting(time_point now);

    chunk_coordinator_config const config_;
    uint32_t const start_height_;
    uint32_t const end_height_;
    uint32_t const total_chunks_;

    std::vector<slot> slots_;

    // Every chunk below this one is COMPLETED. COMPLETED is final, so the mark
    // only moves forward.
    uint32_t completed_floor_{0};

    // No chunk below this one is FREE. Lowered when a chunk is given back.
    uint32_t free_floor_{0};

    uint32_t chunks_completed_{0};
    uint32_t chunks_held_{0};

    std::deque<pending_request> pending_;
    std::deque<chunk_reply_channel_ptr> waiting_;
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP
