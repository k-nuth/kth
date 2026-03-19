// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_MESSAGES_HPP
#define KTH_NODE_SYNC_MESSAGES_HPP

#include <chrono>
#include <cstdint>
#include <variant>
#include <vector>

#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

#include <kth/domain.hpp>
#include <kth/domain/chain/light_block.hpp>
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

// Report header download performance to peer_provider (for slow peer eviction)
struct header_performance {
    uint64_t peer_nonce;
    uint32_t headers_downloaded;
    uint32_t download_time_ms;
};

// Header download task output (can be headers, failure report, or performance)
using header_download_output = std::variant<downloaded_headers, peer_failure_report, header_performance>;

// Header download task input - single channel with all message types (CSP pattern)
// Messages are processed in FIFO order, no arbitrary priority
using header_download_input = std::variant<stop_request, peers_updated, header_request>;

// Header validation task input - single channel (CSP pattern)
using header_validation_input = std::variant<stop_request, downloaded_headers, peer_failure_report>;

struct headers_validated {
    uint32_t height;
    size_t count;
    code result;
    network::peer_session::ptr source_peer;  // For banning on checkpoint failure
};

// -----------------------------------------------------------------------------
// Block sync messages
// -----------------------------------------------------------------------------

// Periodic timeout for supervisors
struct supervisor_timeout {};

struct block_range_request {
    uint32_t start_height;
    uint32_t end_height;
};

template<typename BlockType>
struct downloaded_block {
    uint32_t height;
    std::shared_ptr<BlockType const> block;
    network::peer_session::ptr source_peer;
    uint32_t active_peers;  // snapshot of active peer count at time of send
    uint32_t deserialize_us{0};  // time spent deserializing this block (microseconds)
    uint32_t network_wait_us{0}; // time spent waiting for network (microseconds)
    // Pipeline latency tracking (microseconds since epoch, for measuring channel overhead)
    uint64_t received_from_net_us{0};  // when block arrived from network
    uint64_t sent_to_supervisor_us{0}; // when download task sent to supervisor
};

// Aliases for convenience
using downloaded_full_block = downloaded_block<domain::message::block>;
using downloaded_light_block = downloaded_block<domain::chain::light_block>;

struct block_validated {
    uint32_t height;
    code result;
    network::peer_session::ptr source_peer;  // For banning on validation failure
};

// Chunk-based messages for fast validation pipeline (bypasses supervisor→bridge→validation)
struct downloaded_chunk {
    uint32_t start_height;
    uint32_t chunk_id;
    std::vector<std::shared_ptr<domain::chain::light_block const>> blocks;
    network::peer_session::ptr source_peer;
};

struct chunk_validated {
    uint32_t start_height;
    uint32_t block_count;
    code result;
    network::peer_session::ptr source_peer;
};

// Report that a download task has ended (for cleanup of spawned_peers)
struct download_task_ended {
    uint64_t peer_nonce;  // peer->nonce() for lookup in spawned_peers
};

// Block download supervisor input - single channel (CSP pattern)
using block_download_input = std::variant<stop_request, peers_updated, block_range_request>;

// Block download task output - single channel with blocks, lifecycle, and performance
using block_download_task_output = std::variant<downloaded_light_block, download_task_ended, peer_performance>;

// Block download supervisor unified event (combines all input sources to avoid || operator)
// This prevents message loss that occurs when async_receive is cancelled by ||
using block_supervisor_event = std::variant<
    stop_request,
    peers_updated,
    block_range_request,
    downloaded_light_block,
    download_task_ended,
    supervisor_timeout,
    peer_performance
>;

// Block validation task input - single channel (CSP pattern)
using block_validation_input = std::variant<stop_request, downloaded_light_block>;

// -----------------------------------------------------------------------------
// Peer provider messages (unified input channel)
// -----------------------------------------------------------------------------

// New peer connected from network
struct new_peer {
    network::peer_session::ptr peer;
};

// Peer disconnected from network
struct peer_disconnected {
    network::peer_session::ptr peer;
};

// Peer provider input - single channel for CSP pattern
using peer_provider_input = std::variant<new_peer, peer_disconnected, peer_error, peer_performance, header_performance>;

// =============================================================================
// Channel Type Aliases
// =============================================================================

// Peer provider (unified input)
using peer_provider_input_channel = concurrent_channel<peer_provider_input>;

// Peer distribution (output)
using peer_channel = concurrent_channel<peers_updated>;

// Header sync pipeline (CSP pattern - single input/output per task)
using header_download_input_channel = concurrent_channel<header_download_input>;
using header_download_output_channel = concurrent_channel<header_download_output>;
using header_validation_input_channel = concurrent_channel<header_validation_input>;
using header_validated_channel = concurrent_channel<headers_validated>;

// Block sync pipeline (CSP pattern - single input/output per task)
using block_download_input_channel = concurrent_channel<block_download_input>;
using block_download_task_output_channel = concurrent_channel<block_download_task_output>;
using block_supervisor_event_channel = concurrent_channel<block_supervisor_event>;  // unified input
// Supervisor output - carries blocks and performance stats
using block_supervisor_output = std::variant<downloaded_light_block, peer_performance>;
using block_download_channel = concurrent_channel<block_supervisor_output>;  // supervisor -> bridge
using block_validation_input_channel = concurrent_channel<block_validation_input>;
using block_validated_channel = concurrent_channel<block_validated>;

// Fast validation pipeline (chunk-based, bypasses supervisor→bridge→validation)
using fast_validation_input = std::variant<stop_request, downloaded_chunk>;
using fast_validation_input_channel = concurrent_channel<fast_validation_input>;
using chunk_validated_channel = concurrent_channel<chunk_validated>;

// Block storage pipeline (writes validated chunks to flat files)
using block_storage_input = std::variant<stop_request, downloaded_chunk>;
using block_storage_input_channel = concurrent_channel<block_storage_input>;

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
    block_validated,
    chunk_validated
>;

using sync_coordinator_event_channel = concurrent_channel<sync_coordinator_event>;

// =============================================================================
// Channel Utilities
// =============================================================================

/// Try to send a message to a channel with retries.
/// Useful for avoiding message loss when the channel is temporarily full.
/// Best for small/simple messages where copying is acceptable.
/// @param channel The channel to send to.
/// @param msg The message to send (copied on each attempt until success).
/// @param max_attempts Maximum number of attempts before giving up.
/// @param retry_delay Delay between retry attempts.
/// @return true if message was sent successfully, false if all attempts failed.
template <typename Channel, typename Msg>
::asio::awaitable<bool> try_send_with_retry(
    Channel& channel,
    Msg msg,  // Take by value - caller decides whether to copy or move
    int max_attempts = 5,
    std::chrono::milliseconds retry_delay = std::chrono::milliseconds(10)
) {
    auto executor = co_await ::asio::this_coro::executor;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        // Copy msg for try_send (preserves original for retry)
        if (channel.try_send(std::error_code{}, Msg{msg})) {
            co_return true;
        }
        // Wait before retrying (except on last attempt)
        if (attempt + 1 < max_attempts) {
            ::asio::steady_timer timer(executor);
            timer.expires_after(retry_delay);
            co_await timer.async_wait(::asio::use_awaitable);
        }
    }
    co_return false;
}

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_MESSAGES_HPP
