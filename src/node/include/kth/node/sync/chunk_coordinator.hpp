// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP
#define KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP

// =============================================================================
// Chunk Coordinator - Lock-Free Slot-Based Chunk Assignment
// =============================================================================
//
// This coordinator ONLY handles chunk assignment for parallel block downloads.
// Validation is handled by a separate task via channels (CSP pattern).
//
// DESIGN:
// -------
// - One slot per chunk of N blocks: the slot's position is the chunk id
// - A slot is FREE, IN_PROGRESS (being requested from a peer), DELIVERING
//   (downloaded, waiting for the consumer to take it) or COMPLETED
// - A slot is one atomic word: state, claim number and claim time together, so
//   every transition is a single compare-and-swap
// - A claim names its chunk and its claim number. Only the current claim of a
//   chunk can give it back: a peer whose claim was handed to another peer
//   cannot free the chunk under the peer that holds it now
// - Failed and stalled chunks reset to FREE for retry by another peer. Only a
//   chunk still being requested can stall; a downloaded one waiting for room
//   downstream is not late
// - A chunk counts as completed once, whoever reports it and however often
//
// FLOW:
// -----
// 1. Peer calls claim_chunk() to get a claim (or nullopt if nothing is free)
// 2. Peer uses chunk_range() to get block heights and requests the blocks
// 3. With the blocks in hand: chunk_downloaded(claim), then the hand-over
// 4. On success: chunk_completed(claim)
// 5. On failure: chunk_failed(claim) - slot reset to FREE
//
// NO MUTEXES - Only atomic operations for lock-free coordination.
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <utility>
#include <vector>

#include <kth/blockchain/header_index.hpp>

namespace kth::node::sync {

/// Configuration for chunk coordinator
struct chunk_coordinator_config {
    /// Blocks per chunk (matches Bitcoin protocol getdata limit)
    size_t chunk_size{16};

    /// Timeout before a slot is considered stalled (seconds)
    uint32_t stall_timeout_secs{15};
};

/// One peer's hold on one chunk. The claim number tells this hold from an
/// earlier one of the same chunk that stalled and was handed on.
struct chunk_claim {
    uint32_t chunk_id{0};
    uint32_t number{0};

    friend bool operator==(chunk_claim const&, chunk_claim const&) = default;
};

/// Lock-free chunk coordinator for parallel block downloads
struct chunk_coordinator {
    /// Slot states (lock-free via atomic)
    enum slot_state : uint8_t {
        FREE = 0,        // Available for assignment
        IN_PROGRESS = 1, // Assigned, being requested from the peer
        DELIVERING = 2,  // Downloaded, waiting for the consumer to take it
        COMPLETED = 3    // Handed over. Final: a completed chunk never changes
    };

    /// Construct coordinator for a block range
    chunk_coordinator(
        blockchain::header_index const& index,
        uint32_t start_height,
        uint32_t end_height,
        chunk_coordinator_config const& config = {});

    ~chunk_coordinator() = default;

    // Non-copyable, non-movable (contains atomics)
    chunk_coordinator(chunk_coordinator const&) = delete;
    chunk_coordinator& operator=(chunk_coordinator const&) = delete;
    chunk_coordinator(chunk_coordinator&&) = delete;
    chunk_coordinator& operator=(chunk_coordinator&&) = delete;

    // -------------------------------------------------------------------------
    // Peer Interface (Lock-Free)
    // -------------------------------------------------------------------------

    /// Claim the lowest free chunk
    /// @return the claim, or nullopt if every chunk is assigned or completed
    [[nodiscard]]
    std::optional<chunk_claim> claim_chunk();

    /// Get block range for a chunk
    /// @return {start_height, end_height} for the chunk
    [[nodiscard]]
    std::pair<uint32_t, uint32_t> chunk_range(uint32_t chunk_id) const;

    /// Get block hash for a height (from header index)
    [[nodiscard]]
    hash_digest get_block_hash(uint32_t height) const;

    /// Active-chain generation at this moment. A chunk is stamped with it when
    /// downloaded so work for a branch abandoned mid-flight can be dropped
    /// instead of stored against the new chain.
    [[nodiscard]]
    uint64_t generation() const { return index_.generation(); }

    /// Report that the chunk's blocks have arrived and are about to be handed
    /// over. From here the chunk cannot stall: how long the hand-over waits is
    /// decided by the consumer, not by the peer. Ignored if the claim is no
    /// longer the chunk's current one.
    void chunk_downloaded(chunk_claim claim);

    /// Report that a chunk was handed over. A chunk that stalled is given to a
    /// second peer and both may deliver it: every delivery is valid, and only
    /// the first one counts.
    void chunk_completed(chunk_claim claim);

    /// Give a chunk back (reset to FREE for retry). Ignored unless the claim is
    /// the chunk's current one.
    void chunk_failed(chunk_claim claim);

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /// Reset to FREE the chunks whose request has been pending for longer than
    /// the stall timeout
    void check_timeouts();

    /// The same check against a clock reading the caller took. Peers keep
    /// claiming while the check runs, so a claim can be newer than `now`; such
    /// a claim has not been pending at all and is left alone.
    void check_timeouts(std::chrono::steady_clock::time_point now);

    /// Check if all chunks have been completed
    [[nodiscard]]
    bool is_complete() const;

    /// Check if coordinator was stopped
    [[nodiscard]]
    bool is_stopped() const;

    /// Stop the coordinator (all claim_chunk() will return nullopt)
    void stop();

    /// A token that is stopped by stop(). Workers wait on it instead of polling
    /// is_stopped() while suspended on a channel: a selection armed with this
    /// token completes with operation_aborted the moment the coordinator is
    /// stopped, whether by a replacing range or by shutdown.
    [[nodiscard]]
    std::stop_token stop_token() const { return stop_source_.get_token(); }

    /// Progress information
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
    /// Get current time in milliseconds
    static uint64_t now_ms();

    static uint64_t to_ms(std::chrono::steady_clock::time_point time);

    // Configuration
    chunk_coordinator_config const config_;

    // Header index for hash lookups
    blockchain::header_index const& index_;

    // Range to download
    uint32_t const start_height_;
    uint32_t const end_height_;
    uint32_t const total_chunks_;

    // =========================================================================
    // Lock-free slot management (NO MUTEXES)
    //
    // slots_[i] is chunk i, packed in one word:
    //   bits  0-1   state
    //   bits  2-17  claim number, one more on each claim of this chunk
    //   bits 18-63  time of the claim, in milliseconds
    // The claim number wraps after 65536 claims of the same chunk; a hold that
    // old is long gone.
    // =========================================================================
    using slot_word = uint64_t;
    static constexpr unsigned number_shift = 2;
    static constexpr unsigned time_shift = 18;
    static constexpr slot_word state_mask = (slot_word{1} << number_shift) - 1;
    static constexpr slot_word number_mask = (slot_word{1} << (time_shift - number_shift)) - 1;
    static constexpr size_t bytes_per_slot = sizeof(std::atomic<slot_word>);

    static constexpr slot_word pack(slot_state state, uint32_t number, uint64_t time_ms) {
        return (slot_word{time_ms} << time_shift)
             | ((slot_word{number} & number_mask) << number_shift)
             | slot_word{state};
    }
    static constexpr slot_state state_of(slot_word word) { return slot_state(word & state_mask); }
    static constexpr uint32_t number_of(slot_word word) { return uint32_t((word >> number_shift) & number_mask); }
    static constexpr uint64_t time_of(slot_word word) { return word >> time_shift; }

    /// True if `word` is a hold (requesting or delivering) that `claim` names.
    static constexpr bool holds(slot_word word, chunk_claim claim) {
        auto const state = state_of(word);
        return (state == IN_PROGRESS || state == DELIVERING) && number_of(word) == claim.number;
    }

    std::vector<std::atomic<slot_word>> slots_;

    // Every chunk below this one is COMPLETED. COMPLETED is final, so the mark
    // only moves forward and claims start their search from it instead of from
    // the first chunk of the range.
    std::atomic<uint32_t> completed_floor_{0};

    // Status
    std::atomic<bool> stopped_{false};
    std::stop_source stop_source_;   // Requested by stop(); see stop_token()
    std::atomic<uint32_t> chunks_completed_{0};
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP
