// Copyright (c) 2016-2025 Knuth Project developers.
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
// - Uses a slot-based system where each slot represents a chunk of N blocks
// - Slots have 3 states: FREE (0), IN_PROGRESS (1), COMPLETED (2)
// - Claim operation is lock-free using CAS
// - When all slots in a round are COMPLETED, advance to next round
// - Failed chunks reset to FREE for retry by another peer
//
// FLOW:
// -----
// 1. Peer calls claim_chunk() to get a chunk_id (or nullopt if done)
// 2. Peer uses chunk_range() to get block heights
// 3. Peer downloads blocks and sends them to validation channel
// 4. On success: chunk_completed(chunk_id)
// 5. On failure: chunk_failed(chunk_id) - slot reset to FREE
//
// NO MUTEXES - Only atomic operations for lock-free coordination.
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include <kth/blockchain/header_index.hpp>

namespace kth::node::sync {

/// Configuration for chunk coordinator
struct chunk_coordinator_config {
    /// Blocks per chunk (matches Bitcoin protocol getdata limit)
    size_t chunk_size{16};

    /// Slots per round = max_peers * multiplier

    /// Higher = less frequent round resets, more parallelism
    size_t slots_multiplier{100};
    // size_t slots_multiplier{50};
    
    /// Maximum peers expected
    size_t max_peers{8};

    /// Timeout before a slot is considered stalled (seconds)
    /// 2026-02-03: Increased from 10s to 15s for better tolerance of variable peer speeds
    uint32_t stall_timeout_secs{15};
};

/// Lock-free chunk coordinator for parallel block downloads
class chunk_coordinator {
public:
    /// Slot states (lock-free via atomic)
    enum slot_state : uint8_t {
        FREE = 0,        // Available for assignment
        IN_PROGRESS = 1, // Assigned, download in progress
        COMPLETED = 2    // Download completed successfully
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

    /// Claim a chunk to download
    /// @return chunk_id, or nullopt if all chunks assigned/completed
    [[nodiscard]]
    std::optional<uint32_t> claim_chunk();

    /// Get block range for a chunk
    /// @return {start_height, end_height} for the chunk
    [[nodiscard]]
    std::pair<uint32_t, uint32_t> chunk_range(uint32_t chunk_id) const;

    /// Get block hash for a height (from header index)
    [[nodiscard]]
    hash_digest get_block_hash(uint32_t height) const;

    /// Report that a chunk was completed successfully
    void chunk_completed(uint32_t chunk_id);

    /// Report that a chunk failed (will be reset to FREE for retry)
    void chunk_failed(uint32_t chunk_id);

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /// Check for stalled downloads and reset them to FREE
    void check_timeouts();

    /// Check if all chunks have been completed
    [[nodiscard]]
    bool is_complete() const;

    /// Check if coordinator was stopped
    [[nodiscard]]
    bool is_stopped() const;

    /// Stop the coordinator (all claim_chunk() will return nullopt)
    void stop();

    /// Progress information
    struct progress {
        uint32_t total_chunks;
        uint32_t chunks_completed;
        uint32_t chunks_in_progress;
        uint32_t current_round;
        uint32_t start_height;
        uint32_t end_height;
    };

    [[nodiscard]]
    progress get_progress() const;

private:
    /// Try to advance to next round if all slots completed
    void try_advance_round();

    /// Get current time in milliseconds
    static uint64_t now_ms();

    // Configuration
    chunk_coordinator_config const config_;
    size_t const slots_per_round_;

    // Header index for hash lookups
    blockchain::header_index const& index_;

    // Range to download
    uint32_t const start_height_;
    uint32_t const end_height_;
    uint32_t const total_chunks_;

    // =========================================================================
    // Lock-free slot management (NO MUTEXES)
    // =========================================================================
    std::atomic<uint32_t> round_{0};
    std::vector<std::atomic<uint8_t>> slots_;
    std::vector<std::atomic<uint64_t>> slot_times_;  // timestamp when assigned
    std::atomic<bool> resetting_{false};  // guard for round advancement

    // Status
    std::atomic<bool> stopped_{false};
    std::atomic<uint32_t> chunks_completed_{0};
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_CHUNK_COORDINATOR_HPP
