// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_MESSAGES_HPP
#define KTH_NODE_SYNC_MESSAGES_HPP

#include <cstdint>
#include <variant>
#include <vector>

#include <kth/domain.hpp>
#include <kth/infrastructure/utility/async_channel.hpp>
#include <kth/network/peer_session.hpp>

namespace kth::node::sync {

// =============================================================================
// Message Types (CSP messages passed through channels)
// =============================================================================

// Peer list update - sent by peer manager whenever the peer list changes
struct peers_updated {
    std::vector<network::peer_session::ptr> peers;
};

// -----------------------------------------------------------------------------
// Header sync messages
// -----------------------------------------------------------------------------

// Signal to stop processing
struct stop_request {};

struct header_request {
    uint32_t from_height;
    hash_digest from_hash;
};

struct downloaded_headers {
    domain::message::header::list headers;  // Same type as add_headers() expects
    uint32_t start_height;
    network::peer_session::ptr source_peer;
};

// Report peer failure to coordinator (for tracking peer health)
struct peer_failure_report {
    network::peer_session::ptr peer;
    code error;
};

// Report peer error to peer_provider - it decides what action to take (ban, exclude, etc.)
struct peer_error {
    network::peer_session::ptr peer;
    code error;
};

// Report peer download performance to peer_provider (for slow peer eviction)
struct peer_performance {
    uint64_t peer_nonce;  // Use nonce since peer might disconnect before message arrives
    uint32_t blocks_downloaded;
    uint32_t download_time_ms;
};

// Report header download performance to peer_provider
struct header_performance {
    uint64_t peer_nonce;
    uint32_t headers_downloaded;
    uint32_t download_time_ms;
};

// Report that a header download task has ended
struct header_download_task_ended {
    uint64_t peer_nonce;
};

// Start parallel header sync from a given height
struct start_header_sync {
    uint32_t from_height;
    hash_digest from_hash;
};

// Header download task output - single channel with headers, lifecycle, and performance
using header_download_task_output = std::variant<downloaded_headers, header_download_task_ended, header_performance>;

// Periodic timeout for supervisors (shared by header and block supervisors)
struct supervisor_timeout {};

// Header download supervisor unified event (combines all input sources)
using header_supervisor_event = std::variant<
    stop_request,
    peers_updated,
    start_header_sync,
    downloaded_headers,
    header_download_task_ended,
    supervisor_timeout,
    header_performance
>;

// Header supervisor output - carries headers and performance stats
using header_supervisor_output = std::variant<downloaded_headers, header_performance>;

// Header download supervisor input - single channel (CSP pattern)
using header_download_input = std::variant<stop_request, peers_updated, start_header_sync>;

// Header validation task input - single channel (CSP pattern)
using header_validation_input = std::variant<stop_request, downloaded_headers>;

struct headers_validated {
    uint32_t height;
    size_t count;
    code result;
    network::peer_session::ptr source_peer;  // For banning on checkpoint failure
};

// -----------------------------------------------------------------------------
// Block sync messages
// -----------------------------------------------------------------------------

struct block_range_request {
    uint32_t start_height;
    uint32_t end_height;
};

struct downloaded_block {
    uint32_t height;
    domain::message::block::const_ptr block;
    network::peer_session::ptr source_peer;
    uint32_t active_peers;  // snapshot of active peer count at time of send
};

struct block_validated {
    uint32_t height;
    code result;
    network::peer_session::ptr source_peer;  // For banning on validation failure
};

// Report that a download task has ended (for cleanup of spawned_peers)
struct download_task_ended {
    uint64_t peer_nonce;  // peer->nonce() for lookup in spawned_peers
};

// Block download supervisor input - single channel (CSP pattern)
using block_download_input = std::variant<stop_request, peers_updated, block_range_request>;

// Block download task output - single channel with blocks, lifecycle, and performance
using block_download_task_output = std::variant<downloaded_block, download_task_ended, peer_performance>;

// Block download supervisor unified event (combines all input sources to avoid || operator)
// This prevents message loss that occurs when async_receive is cancelled by ||
using block_supervisor_event = std::variant<
    stop_request,
    peers_updated,
    block_range_request,
    downloaded_block,
    download_task_ended,
    supervisor_timeout,
    peer_performance
>;

// Block validation task input - single channel (CSP pattern)
using block_validation_input = std::variant<stop_request, downloaded_block>;

// -----------------------------------------------------------------------------
// Peer provider messages (unified input channel)
// -----------------------------------------------------------------------------

// New peer connected from network
struct new_peer {
    network::peer_session::ptr peer;
};

// Peer provider input - single channel for CSP pattern
using peer_provider_input = std::variant<new_peer, peer_error, peer_performance, header_performance>;

// =============================================================================
// Channel Type Aliases
// =============================================================================

// Peer provider (unified input)
using peer_provider_input_channel = concurrent_channel<peer_provider_input>;

// Peer distribution (output)
using peer_channel = concurrent_channel<peers_updated>;

// Header sync pipeline (CSP pattern - single input/output per task)
using header_download_input_channel = concurrent_channel<header_download_input>;
using header_download_task_output_channel = concurrent_channel<header_download_task_output>;
using header_supervisor_event_channel = concurrent_channel<header_supervisor_event>;  // unified input
using header_download_channel = concurrent_channel<header_supervisor_output>;  // supervisor -> bridge
using header_validation_input_channel = concurrent_channel<header_validation_input>;
using header_validated_channel = concurrent_channel<headers_validated>;

// Block sync pipeline (CSP pattern - single input/output per task)
using block_download_input_channel = concurrent_channel<block_download_input>;
using block_download_task_output_channel = concurrent_channel<block_download_task_output>;
using block_supervisor_event_channel = concurrent_channel<block_supervisor_event>;  // unified input
// Supervisor output - carries blocks and performance stats
using block_supervisor_output = std::variant<downloaded_block, peer_performance>;
using block_download_channel = concurrent_channel<block_supervisor_output>;  // supervisor -> bridge
using block_validation_input_channel = concurrent_channel<block_validation_input>;
using block_validated_channel = concurrent_channel<block_validated>;

// Control
using stop_channel = concurrent_event_channel;

// -----------------------------------------------------------------------------
// Sync coordinator messages (unified input channel)
// -----------------------------------------------------------------------------

// Sync coordinator unified event (combines all input sources to avoid || operator)
// This prevents message loss that occurs when async_receive is cancelled by ||
using sync_coordinator_event = std::variant<
    stop_request,
    headers_validated,
    block_validated
>;

using sync_coordinator_event_channel = concurrent_channel<sync_coordinator_event>;

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_MESSAGES_HPP
