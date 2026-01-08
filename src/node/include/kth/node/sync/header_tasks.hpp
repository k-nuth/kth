// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_HEADER_TASKS_HPP
#define KTH_NODE_SYNC_HEADER_TASKS_HPP

#include <atomic>

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/sync/header_chunk_coordinator.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::sync {

// =============================================================================
// Header Download Supervisor
// =============================================================================
//
// Manages parallel header downloads. Spawns one download task per peer.
// - Input: single channel with variant (stop, peers_updated, start_header_sync)
// - Output: downloaded headers + performance stats to header_download_channel
// - Creates header_chunk_coordinator for lock-free chunk assignment
// - Maintains internal task_group for download workers
// - Speculative: downloads chunks ahead of validated height
//
// =============================================================================

::asio::awaitable<void> header_download_supervisor(
    header_download_input_channel& input,
    header_download_channel& output,
    blockchain::header_organizer& organizer  // read-only for hashes
);

// =============================================================================
// Header Download Task (internal, spawned by supervisor)
// =============================================================================
//
// Downloads headers from a single peer.
// - Claims chunks via header_chunk_coordinator (lock-free CAS)
// - Downloads headers and sends to output channel
// - Reports success/failure to coordinator for proper retry handling
// - Exits when peer disconnects or no more chunks
//
// =============================================================================

::asio::awaitable<void> header_download_task(
    network::peer_session::ptr peer,
    header_chunk_coordinator& coordinator,
    blockchain::header_organizer& organizer,  // read-only for hashes
    std::atomic<uint32_t>& active_peers,
    header_download_task_output_channel& output
);

// =============================================================================
// Header Validation Task
// =============================================================================
//
// Validates headers in order and writes to organizer.
// - Input: single channel with variant (stop, downloaded_headers)
// - Output: validation results to header_validated_channel
// - Buffers out-of-order headers (speculative chunks may arrive early)
// - Writes to header_organizer (SINGLE WRITER - no lock needed)
//
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    uint32_t start_height,  // Starting height for expected_height calculation
    header_validation_input_channel& input,
    header_validated_channel& output
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_HEADER_TASKS_HPP
