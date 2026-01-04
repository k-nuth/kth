// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_HPP
#define KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_HPP

// =============================================================================
// Block Download Coordinator - Parallel Block Download Management
// =============================================================================
//
// This class coordinates parallel block downloads across multiple peers:
//
// DESIGN:
// -------
// - Central coordinator assigns block heights to peers
// - Each peer can have up to MAX_BLOCKS_PER_PEER blocks in-flight
// - Blocks may arrive out-of-order; buffered until ready for validation
// - Validation happens in-order via a separate pipeline
//
// FLOW:
// -----
// 1. Peer calls claim_blocks() to get next blocks to download
// 2. Peer sends getdata for claimed blocks
// 3. Peer receives blocks and calls block_received()
// 4. Coordinator buffers out-of-order blocks
// 5. Validation pipeline consumes blocks in order
//
// RECOVERY:
// ---------
// - On peer disconnect: reassign its in-flight blocks
// - On timeout: reassign stalled blocks to other peers
// - On validation failure: stop sync and report error
//
// =============================================================================

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/domain.hpp>
#include <kth/network.hpp>
#include <kth/blockchain.hpp>
#include <kth/node/define.hpp>

#include <asio/awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>
#include <asio/strand.hpp>

namespace kth::node {

/// Configuration for parallel block download
struct parallel_download_config {
    /// Maximum blocks in-flight per peer (BCHN: 16)
    size_t max_blocks_per_peer{16};

    /// Global download window - max blocks ahead of validation
    size_t global_window{1024};

    /// Timeout for individual block download
    std::chrono::seconds block_timeout{60};

    /// Timeout before reassigning stalled blocks
    std::chrono::seconds stall_timeout{10};

    /// How often to check for stalled downloads
    std::chrono::seconds timeout_check_interval{1};
};

/// Block download coordinator - manages parallel downloads across peers
class KND_API block_download_coordinator {
public:
    using block_const_ptr = domain::message::block::const_ptr;
    using peer_ptr = network::peer_session::ptr;

    /// Construct coordinator for a block range
    /// @param chain Blockchain for validation
    /// @param organizer Header organizer (has the header index)
    /// @param start_height First block to download (usually 1 for IBD)
    /// @param target_height Last block to download
    /// @param executor Executor for internal channel
    /// @param config Download configuration
    block_download_coordinator(
        blockchain::block_chain& chain,
        blockchain::header_organizer& organizer,
        uint32_t start_height,
        uint32_t target_height,
        ::asio::any_io_executor executor,
        parallel_download_config const& config = {});

    ~block_download_coordinator();

    // Non-copyable
    block_download_coordinator(block_download_coordinator const&) = delete;
    block_download_coordinator& operator=(block_download_coordinator const&) = delete;

    // -------------------------------------------------------------------------
    // Peer Interface
    // -------------------------------------------------------------------------

    /// Claim blocks for a peer to download
    /// @param peer The peer claiming blocks
    /// @param max_count Maximum blocks to claim (typically 16)
    /// @return Vector of {height, hash} pairs to download, empty if none available
    [[nodiscard]]
    ::asio::awaitable<std::vector<std::pair<uint32_t, hash_digest>>> claim_blocks(
        peer_ptr const& peer,
        size_t max_count);

    /// Report that a block was received
    /// @param height Block height
    /// @param hash Block hash
    /// @param block The received block
    ::asio::awaitable<void> block_received(
        uint32_t height,
        hash_digest const& hash,
        block_const_ptr block);

    /// Report that a peer disconnected - reassign its blocks
    ::asio::awaitable<void> peer_disconnected(peer_ptr const& peer);

    // -------------------------------------------------------------------------
    // Validation Pipeline
    // -------------------------------------------------------------------------

    /// Get next block ready for validation (in-order)
    /// Blocks until a block is available or sync is complete/failed
    /// @return Block to validate, or nullopt if sync complete/failed
    [[nodiscard]]
    ::asio::awaitable<std::optional<std::pair<uint32_t, block_const_ptr>>> next_block_to_validate();

    /// Report validation result
    void validation_complete(uint32_t height, code result);

    // -------------------------------------------------------------------------
    // Status & Control
    // -------------------------------------------------------------------------

    /// Check for stalled downloads and reassign
    void check_timeouts();

    /// Check if all blocks have been downloaded and validated
    [[nodiscard]]
    bool is_complete() const;

    /// Check if sync has failed
    [[nodiscard]]
    bool has_failed() const;

    /// Check if coordinator was stopped externally
    [[nodiscard]]
    bool is_stopped() const;

    /// Get failure error code
    [[nodiscard]]
    code failure_reason() const;

    /// Stop the coordinator (cancel pending operations)
    void stop();

    /// Get current progress
    struct progress {
        uint32_t blocks_downloaded;
        uint32_t blocks_validated;
        uint32_t blocks_in_flight;
        uint32_t blocks_pending;  // Downloaded but not yet validated
        uint32_t active_peers;    // Peers with blocks in-flight
        uint32_t start_height;
        uint32_t target_height;
        std::chrono::steady_clock::time_point start_time;
    };

    [[nodiscard]]
    progress get_progress() const;

private:
    // Get hash for a block height from header organizer
    hash_digest get_block_hash(uint32_t height) const;

    // Try to push ready blocks to validation channel
    void flush_pending_to_validation();

    // Mark sync as failed
    void set_failed(code reason);

    // Configuration
    parallel_download_config config_;

    // Blockchain access
    blockchain::block_chain& chain_;
    blockchain::header_organizer& organizer_;

    // Range to download
    uint32_t const start_height_;
    uint32_t const target_height_;

    // Strand for serializing access to shared state (replaces mutex)
    // All peer operations (claim_blocks, block_received, etc.) run on this strand
    ::asio::strand<::asio::any_io_executor> strand_;
    std::atomic<bool> stopped_{false};
    std::atomic<bool> failed_{false};
    code failure_reason_{error::success};

    // Assignment state
    uint32_t next_height_to_assign_;  // Next height to assign to a peer

    // In-flight tracking: height -> {peer, request_time}
    struct assignment {
        peer_ptr peer;
        std::chrono::steady_clock::time_point requested_at;
    };
    boost::unordered_flat_map<uint32_t, assignment> in_flight_;

    // Out-of-order buffer: height -> block
    boost::unordered_flat_map<uint32_t, block_const_ptr> pending_blocks_;

    // Validation state
    uint32_t next_height_to_validate_;
    std::atomic<uint32_t> blocks_validated_{0};

    // Timing
    std::chrono::steady_clock::time_point start_time_;

    // Channel for validation pipeline
    using validation_channel = ::asio::experimental::concurrent_channel<
        void(std::error_code, std::pair<uint32_t, block_const_ptr>)>;
    std::unique_ptr<validation_channel> validation_queue_;
};

} // namespace kth::node

#endif // KTH_NODE_BLOCK_DOWNLOAD_COORDINATOR_HPP
