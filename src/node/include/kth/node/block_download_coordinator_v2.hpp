// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_V2_HPP
#define KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_V2_HPP

// =============================================================================
// Block Download Coordinator V2 - Lock-Free Parallel Block Download
// =============================================================================
//
// Simplified coordinator using atomic operations instead of mutex/strand.
//
// DESIGN:
// -------
// - Uses a slot-based system where each slot represents a chunk of 16 blocks
// - Slots have 3 states: FREE (0), IN_PROGRESS (1), COMPLETED (2)
// - Claim operation is lock-free using CAS
// - Timeout checker can reset stalled slots back to FREE
// - When all slots in a round are COMPLETED, advance to next round
//
// FLOW:
// -----
// 1. Peer calls claim_chunk() to get a chunk_id
// 2. Peer calculates block range: [start + chunk_id*16, start + chunk_id*16 + 15]
// 3. Peer fetches hashes from header_index and downloads blocks
// 4. Peer calls chunk_completed(chunk_id) when done
// 5. Timeout checker periodically resets stalled IN_PROGRESS slots
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain.hpp>
#include <kth/blockchain.hpp>
#include <kth/node/define.hpp>

namespace kth::node {

/// Configuration for parallel block download v2
struct parallel_download_config_v2 {
    /// Blocks per chunk (matches Bitcoin protocol limit)
    size_t chunk_size{16};

    /// Slots per round = max_peers * multiplier
    /// Higher = less frequent round resets
    size_t slots_multiplier{100};

    /// Maximum peers expected
    size_t max_peers{8};

    /// Timeout before a slot is considered stalled
    std::chrono::seconds stall_timeout{30};

    /// How often to check for stalled downloads
    std::chrono::seconds timeout_check_interval{5};
};

/// Lock-free block download coordinator
class KND_API block_download_coordinator_v2 {
public:
    using block_const_ptr = domain::message::block::const_ptr;

    /// Slot states
    enum SlotState : uint8_t {
        FREE = 0,        // Available for assignment
        IN_PROGRESS = 1, // Assigned, waiting for completion
        COMPLETED = 2    // Blocks received successfully
    };

    /// Construct coordinator for a block range
    block_download_coordinator_v2(
        blockchain::block_chain& chain,
        blockchain::header_organizer& organizer,
        uint32_t start_height,
        uint32_t target_height,
        ::asio::any_io_executor executor,
        parallel_download_config_v2 const& config = {});

    ~block_download_coordinator_v2();

    // Non-copyable
    block_download_coordinator_v2(block_download_coordinator_v2 const&) = delete;
    block_download_coordinator_v2& operator=(block_download_coordinator_v2 const&) = delete;

    // -------------------------------------------------------------------------
    // Peer Interface (Lock-Free)
    // -------------------------------------------------------------------------

    /// Claim a chunk to download
    /// @return chunk_id, or nullopt if sync complete
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

    /// Report that a chunk failed (will be retried)
    void chunk_failed(uint32_t chunk_id);

    // -------------------------------------------------------------------------
    // Block Reception & Validation
    // -------------------------------------------------------------------------

    /// Report a received block for validation pipeline
    void block_received(uint32_t height, block_const_ptr block);

    /// Get next block ready for validation (in-order)
    [[nodiscard]]
    ::asio::awaitable<std::optional<std::pair<uint32_t, block_const_ptr>>> next_block_to_validate();

    /// Report validation result
    void validation_complete(uint32_t height, code result);

    // -------------------------------------------------------------------------
    // Status & Control
    // -------------------------------------------------------------------------

    /// Check for stalled downloads and reset them
    void check_timeouts();

    /// Check if all blocks have been validated
    [[nodiscard]]
    bool is_complete() const;

    /// Check if sync has failed
    [[nodiscard]]
    bool has_failed() const;

    /// Check if coordinator was stopped
    [[nodiscard]]
    bool is_stopped() const;

    /// Get failure reason
    [[nodiscard]]
    code failure_reason() const;

    /// Stop the coordinator
    void stop();

    /// Progress information
    struct progress {
        uint32_t chunks_assigned;
        uint32_t chunks_completed;
        uint32_t chunks_in_progress;
        uint32_t blocks_validated;
        uint32_t blocks_pending;
        uint32_t current_round;
        uint32_t start_height;
        uint32_t target_height;
        uint32_t active_peers;
        std::chrono::steady_clock::time_point start_time;
    };

    /// Register/unregister active download peers
    void peer_started();
    void peer_stopped();

    [[nodiscard]]
    progress get_progress() const;

private:
    /// Try to advance to next round if all slots completed
    void try_advance_round();

    /// Flush pending blocks to validation channel
    void flush_pending_to_validation();

    /// Set sync as failed
    void set_failed(code reason);

    /// Get current time in milliseconds
    static uint64_t now_ms();

    // Configuration
    parallel_download_config_v2 config_;
    size_t slots_per_round_;

    // Blockchain access
    blockchain::block_chain& chain_;
    blockchain::header_organizer& organizer_;

    // Range to download
    uint32_t const start_height_;
    uint32_t const target_height_;
    uint32_t const total_chunks_;

    // =========================================================================
    // Lock-free slot management
    // =========================================================================
    std::atomic<uint32_t> round_{0};
    std::vector<std::atomic<uint8_t>> slots_;
    std::vector<std::atomic<uint64_t>> slot_times_;  // timestamp when assigned
    std::atomic<bool> resetting_{false};

    // Status
    std::atomic<bool> stopped_{false};
    std::atomic<bool> failed_{false};
    code failure_reason_{error::success};
    std::atomic<uint32_t> chunks_completed_{0};
    std::atomic<uint32_t> blocks_validated_{0};
    std::atomic<uint32_t> active_peers_{0};

    // Validation pipeline - needs synchronization for out-of-order blocks
    std::mutex validation_mutex_;
    uint32_t next_height_to_validate_;
    boost::unordered_flat_map<uint32_t, block_const_ptr> pending_blocks_;

    // Timing
    std::chrono::steady_clock::time_point start_time_;

    // Channel for validation pipeline
    using validation_channel = ::asio::experimental::concurrent_channel<
        void(std::error_code, std::pair<uint32_t, block_const_ptr>)>;
    std::unique_ptr<validation_channel> validation_queue_;
};

} // namespace kth::node

#endif // KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_V2_HPP
