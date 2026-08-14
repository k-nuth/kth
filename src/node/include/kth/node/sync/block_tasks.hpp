// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_BLOCK_TASKS_HPP
#define KTH_NODE_SYNC_BLOCK_TASKS_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <memory>
#include <optional>

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/sync/chunk_coordinator.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::sync {

// =============================================================================
// UTXO-build sync decisions (pure; unit-tested in test/sync_decisions.cpp)
// =============================================================================

/// Resume floor for the UTXO builder: the height already built (the builder
/// resumes at the next one). The persisted utxo-built height (`saved`) is
/// authoritative — it is the last height whose UTXO delta was actually applied —
/// and must win over `start_height`, which is derived from the block-sync marker
/// (blocks downloaded) and can run ahead of what has been built. Only when there
/// is no saved progress does it fall back to `start_height - 1`.
[[nodiscard]]
uint32_t resume_utxo_built_height(std::optional<uint32_t> saved, uint32_t start_height);

/// Number of contiguous blocks the UTXO builder should process this iteration,
/// or 0 to wait and poll. During IBD (`stale`) it requires a full `batch_size`
/// window for throughput; once caught up to the tip (`!stale`) it drains whatever
/// is available, down to a single block, so the trailing remainder and each
/// newly-mined block are validated promptly instead of stalling for a full batch.
[[nodiscard]]
uint32_t utxo_batch_len(uint32_t available, uint32_t batch_size, bool stale);

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

/// Starts the actual download for one peer session.
///
/// A seam, and a deliberately narrow one (#652): it replaces ONLY "run the
/// download for this session". Everything the supervisor decides — the snapshot
/// of known peers, the range and its epoch, the reservation before the start,
/// the exact match on a report, the handoff and the rollback — stays inside the
/// supervisor and is exercised through it.
///
/// It exists because the worker body talks p2p, so a test that wants to control
/// WHEN a worker ends would otherwise have to drive a real peer conversation.
/// The identity is passed through unchanged: a launcher must report with the
/// same task id and epoch it was handed, exactly as the real worker does.
using download_worker_launcher = std::function<void(
    network::peer_session::ptr peer,
    std::shared_ptr<chunk_coordinator> coordinator,
    uint64_t task_id,
    uint64_t coordinator_epoch,
    block_download_task_output_channel& output)>;

::asio::awaitable<void> block_download_supervisor(
    block_download_input_channel& input,
    block_download_channel& output,  // carries blocks + performance stats
    blockchain::header_organizer& organizer,  // read-only for hashes
    fast_validation_input_channel* fast_val = nullptr,  // chunk-based fast validation (nullptr = old path)
    download_worker_launcher launcher = nullptr   // test seam; null means the real worker
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
    uint64_t task_id,            // this instance, so a late report cannot retire a newer one (#652)
    uint64_t coordinator_epoch,  // the range it belongs to; NOT index_.generation()
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
    chunk_validated_channel& output,
    block_storage_input_channel* storage = nullptr  // if non-null, forward valid chunks here
);

// =============================================================================
// Block Storage Task (writes validated blocks to flat files)
// =============================================================================
//
// Receives validated chunks (with block data) from fast_validation_task,
// buffers out-of-order chunks, and flushes them sequentially via
// chain.store_block(). Sends chunk_validated to coordinator once stored.
//
// =============================================================================

::asio::awaitable<void> block_storage_task(
    blockchain::block_chain& chain,
    block_storage_input_channel& input,
    chunk_validated_channel& output,
    uint32_t start_height,
    blockchain::header_organizer& organizer,  // advance finalization as blocks validate
    std::atomic<uint32_t>* contiguous_out = nullptr  // publish contiguous height for utxo_build_task
);

// =============================================================================
// Incremental UTXO Build Task
// =============================================================================
//
// Independent task that monitors contiguous_height and builds the UTXO set
// incrementally by reading blocks from flat files (disk).
// Runs in parallel with block_storage_task — zero coupling, zero buffering.
//
// =============================================================================

// `on_fatal` reports a condition this task cannot go on from and that ending the
// coroutine would not communicate: the task group only propagates exceptions, so
// a co_return here reduces a counter and nothing else. Winding the node down is
// the owner's (see full_node::notify_fatal).
::asio::awaitable<void> utxo_build_task(
    blockchain::block_chain& chain,
    std::atomic<uint32_t> const& contiguous_height,
    uint32_t start_height,
    domain::config::network network,
    std::function<bool()> should_stop,  // node-owned stop signal (e.g. network.stopped())
    std::function<void(std::string const&)> on_fatal
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_BLOCK_TASKS_HPP
