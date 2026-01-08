// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_HEADER_CHUNK_COORDINATOR_HPP
#define KTH_NODE_SYNC_HEADER_CHUNK_COORDINATOR_HPP

// =============================================================================
// Header Chunk Coordinator - Lock-Free Speculative Header Downloads
// =============================================================================
//
// Similar to chunk_coordinator for blocks, but handles the fact that we don't
// know the chain height upfront. Uses speculative chunks that extend as we
// discover more headers.
//
// DESIGN:
// -------
// - Each chunk is 2000 headers (standard getheaders response)
// - Speculative: we don't know end_height, so we extend as peers respond
// - Limited speculation: max N chunks ahead of validated height
// - Lock-free: all operations use atomic CAS
//
// FLOW:
// -----
// 1. Peer calls claim_chunk() to get a starting height (or nullopt if at limit)
// 2. Peer downloads headers from that height
// 3. On success: report_chunk_completed(height, headers_received)
//    - If headers_received < chunk_size, we've hit the tip
// 4. On failure: report_chunk_failed(height) - slot reset for retry
// 5. Validation task calls report_validated(height) as it processes
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

namespace kth::node::sync {

/// Configuration for header chunk coordinator
struct header_chunk_config {
    /// Headers per chunk (matches getheaders protocol response)
    uint32_t chunk_size{2000};

    /// How many chunks ahead of validated we allow (speculation limit)
    /// Lower than blocks because header downloads are fast
    uint32_t speculative_chunks{3};

    /// Maximum concurrent peers downloading headers
    uint32_t max_peers{8};

    /// Timeout before a chunk is considered stalled (seconds)
    uint32_t stall_timeout_secs{30};
};

/// Lock-free coordinator for parallel header downloads with speculation
class header_chunk_coordinator {
public:
    /// Chunk states (lock-free via atomic)
    enum chunk_state : uint8_t {
        FREE = 0,        // Available for assignment
        IN_PROGRESS = 1, // Assigned, download in progress
        COMPLETED = 2    // Download completed successfully
    };

    /// Construct coordinator starting from a given height
    explicit header_chunk_coordinator(
        uint32_t start_height,
        header_chunk_config const& config = {});

    ~header_chunk_coordinator() = default;

    // Non-copyable, non-movable (contains atomics)
    header_chunk_coordinator(header_chunk_coordinator const&) = delete;
    header_chunk_coordinator& operator=(header_chunk_coordinator const&) = delete;
    header_chunk_coordinator(header_chunk_coordinator&&) = delete;
    header_chunk_coordinator& operator=(header_chunk_coordinator&&) = delete;

    // -------------------------------------------------------------------------
    // Peer Interface (Lock-Free)
    // -------------------------------------------------------------------------

    /// Claim a chunk to download
    /// @return starting height for the chunk, or nullopt if at speculation limit
    [[nodiscard]]
    std::optional<uint32_t> claim_chunk();

    /// Report that a chunk completed successfully
    /// @param start_height The starting height of the chunk
    /// @param headers_received How many headers were received (< chunk_size means tip)
    void report_chunk_completed(uint32_t start_height, uint32_t headers_received);

    /// Report that a chunk failed (will be reset to FREE for retry)
    void report_chunk_failed(uint32_t start_height);

    // -------------------------------------------------------------------------
    // Validation Interface
    // -------------------------------------------------------------------------

    /// Report that headers up to this height have been validated
    /// This allows more speculative chunks to be claimed
    void report_validated(uint32_t height);

    // -------------------------------------------------------------------------
    // Status
    // -------------------------------------------------------------------------

    /// Check for stalled downloads and reset them to FREE
    void check_timeouts();

    /// Check if we've reached the chain tip (received < chunk_size headers)
    [[nodiscard]]
    bool tip_reached() const;

    /// Check if sync is complete (tip reached AND all validated)
    [[nodiscard]]
    bool is_complete() const;

    /// Check if coordinator was stopped
    [[nodiscard]]
    bool is_stopped() const;

    /// Stop the coordinator (all claim_chunk() will return nullopt)
    void stop();

    /// Progress information
    struct progress {
        uint32_t start_height;
        uint32_t validated_height;
        uint32_t discovered_height;   // Highest height we know exists
        uint32_t next_chunk_height;   // Next chunk that will be assigned
        uint32_t chunks_in_progress;
        bool tip_reached;
    };

    [[nodiscard]]
    progress get_progress() const;

private:
    /// Convert height to slot index
    [[nodiscard]]
    size_t height_to_slot(uint32_t height) const;

    /// Get current time in milliseconds
    [[nodiscard]]
    static uint64_t now_ms();

    // Configuration
    header_chunk_config const config_;
    uint32_t const start_height_;

    // Number of slots = speculative_chunks * max_peers (circular buffer)
    size_t const num_slots_;

    // =========================================================================
    // Lock-free state (NO MUTEXES)
    // =========================================================================

    // Slot states (circular buffer indexed by (height - start_height) / chunk_size % num_slots)
    std::vector<std::atomic<uint8_t>> slots_;
    std::vector<std::atomic<uint64_t>> slot_times_;  // timestamp when assigned

    // Progress tracking
    std::atomic<uint32_t> next_chunk_height_;    // Next height to assign
    std::atomic<uint32_t> validated_height_;     // Highest validated height
    std::atomic<uint32_t> discovered_height_;    // Highest height discovered from responses

    // Completion tracking
    std::atomic<bool> tip_reached_{false};       // Received < chunk_size headers
    std::atomic<bool> stopped_{false};
};

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_HEADER_CHUNK_COORDINATOR_HPP
