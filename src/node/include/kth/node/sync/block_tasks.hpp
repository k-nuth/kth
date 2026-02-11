// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_BLOCK_TASKS_HPP
#define KTH_NODE_SYNC_BLOCK_TASKS_HPP

#include <atomic>
#include <memory>

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/sync/chunk_coordinator.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::sync {

// =============================================================================
// Pipeline Counters for debugging block loss
// =============================================================================

extern std::atomic<uint64_t> g_blocks_sent_by_tasks;
extern std::atomic<uint64_t> g_blocks_received_by_supervisor;
extern std::atomic<uint64_t> g_blocks_forwarded_by_supervisor;
extern std::atomic<uint64_t> g_blocks_received_by_bridge;
extern std::atomic<uint64_t> g_blocks_forwarded_by_bridge;
extern std::atomic<uint64_t> g_blocks_received_by_validation;

// =============================================================================
// Block Download Supervisor
// =============================================================================
//
// Manages block download tasks. Spawns one download task per peer.
// - Input: single channel with variant (stop, peers_updated, block_range_request)
// - Output: downloaded blocks to block_download_channel
// - Creates chunk_coordinator for lock-free chunk assignment
// - Maintains internal task_group for download workers
//
// =============================================================================

::asio::awaitable<void> block_download_supervisor(
    block_download_input_channel& input,
    block_download_channel& output,  // carries blocks + performance stats
    blockchain::header_organizer& organizer,  // read-only for hashes
    fast_validation_input_channel* fast_val = nullptr  // chunk-based fast validation (nullptr = old path)
);

// =============================================================================
// Block Download Task (internal, spawned by supervisor)
// =============================================================================
//
// Downloads blocks from a single peer.
// - Claims chunks via chunk_coordinator (lock-free CAS)
// - Downloads blocks and sends to block_download_channel
// - Reports success/failure to coordinator for proper retry handling
// - Exits when peer disconnects or no more chunks
//
// =============================================================================

::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    std::shared_ptr<chunk_coordinator> coordinator,  // Lock-free chunk assignment (shared to keep alive)
    std::atomic<uint32_t>& active_peers,     // Atomic peer counter for metrics
    block_download_task_output_channel& output,  // Single output: blocks + task_ended
    fast_validation_input_channel* fast_val = nullptr  // chunk-based fast validation (nullptr = old path)
);

// =============================================================================
// Block Validation Task
// =============================================================================
//
// Validates blocks in order and writes to chain.
// - Input: single channel with variant (stop, downloaded_block)
// - Output: validation results to block_validated_channel
// - Buffers out-of-order blocks (OWNED state, not shared)
// - Writes to block_chain (SINGLE WRITER - no lock needed)
// - Uses organize_fast() under checkpoint for fast IBD (no UTXO updates)
// - Uses organize() above checkpoint for full validation
//
// =============================================================================

::asio::awaitable<void> block_validation_task(
    blockchain::block_chain& chain,
    block_validation_input_channel& input,
    block_validated_channel& output,
    uint32_t start_height,
    uint32_t checkpoint_height  // Use fast mode up to this height
);

// =============================================================================
// Fast Validation Task (chunk-based, parallel merkle)
// =============================================================================
//
// Validates entire chunks of light_blocks in parallel.
// - Input: single channel with variant (stop, downloaded_chunk)
// - Output: chunk validation results to chunk_validated_channel
// - Posts N merkle checks to priority_pool_ in parallel via chain.validate_chunk()
// - Single channel message per chunk (instead of N individual messages)
// - Designed for fast IBD under checkpoint
//
// =============================================================================

::asio::awaitable<void> fast_validation_task(
    blockchain::block_chain& chain,
    fast_validation_input_channel& input,
    chunk_validated_channel& output
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_BLOCK_TASKS_HPP
