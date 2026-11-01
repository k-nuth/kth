// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_MESSAGES_HPP
#define KTH_NODE_SYNC_MESSAGES_HPP

#include <chrono>
#include <cstdint>
#include <variant>
#include <optional>
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

// A batch of headers from the node's header ingest. Not an answer to anything:
// see detail/header_walk.hpp.
struct downloaded_headers {
    domain::message::header::list headers;  // Same type as add_headers() expects
    network::peer_session::ptr source_peer;
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

// Header validation task input - single channel (CSP pattern)
using header_validation_input = std::variant<stop_request, downloaded_headers>;

struct headers_validated {
    uint32_t height;
    size_t count;
    code result;
    network::peer_session::ptr source_peer;  // For banning on checkpoint failure

    // Set when the batch stored a side branch that out-works the active chain and
    // descends from the finalized block. The coordinator performs the switch;
    // the organizer only reports it.
    bool reorg_candidate{false};
    int32_t reorg_fork_height{-1};
    uint32_t reorg_branch_head{0};
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

    // Chain generation this chunk was downloaded under. A reorg bumps it, so a
    // chunk still in flight (or buffered behind the reorg barrier) for the
    // abandoned branch is recognisable and dropped: its heights now belong to
    // different blocks, and storing it would mark the wrong hashes have_data.
    uint64_t generation{0};
};

struct chunk_validated {
    uint32_t start_height;
    uint32_t block_count;
    code result;
    network::peer_session::ptr source_peer;
};

// Report that a download task has ended (for cleanup of spawned_peers)
/// A download worker reporting that it ended.
///
/// The nonce alone is not an identity (#652). Replacing a coordinator does not
/// stop its workers — `stop()` is a flag they notice between chunks, and one
/// mid-download took 59 seconds to come back in a mainnet run — so this can
/// arrive after a NEW worker for the same peer already exists. Matched on the
/// nonce alone it would retire that new worker, leaving the peer with no
/// consumer and no event coming.
///
/// `task_id` names the instance and `coordinator_epoch` the range it belonged
/// to; both are the values the worker was started with, repeated verbatim.
struct download_task_ended {
    uint64_t peer_nonce;
    uint64_t task_id;
    uint64_t coordinator_epoch;
};

// Block download supervisor input - single channel (CSP pattern)
using block_download_input = std::variant<stop_request, peers_updated, block_range_request>;

// Block download task output - single channel with blocks, lifecycle, and performance
using block_download_task_output = std::variant<downloaded_light_block, download_task_ended, peer_performance>;

// The supervisor waits on its input, its workers' output and its timeout tick
// through ONE task-owned selector (see block_download_supervisor). There is no
// unified event variant any more: the selection arbitrates between the two
// channels and the timer without a bridge task per source, without an extra
// queue, and without the || operator (a losing receive consumes nothing).

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

// Peer provider (unified input). A select_channel: every producer uses
// try_send (a dropped statistic is logged; a dropped peer event is logged too,
// as before), and the one consumer waits on it with the network's stop token
// instead of racing a receive against a timer.
using peer_provider_input_channel = select_channel<peer_provider_input>;

// Peer distribution (output)
using peer_channel = concurrent_channel<peers_updated>;

// Header sync pipeline (CSP pattern - single input/output per task)
using header_validation_input_channel = concurrent_channel<header_validation_input>;
using header_validated_channel = concurrent_channel<headers_validated>;

// Block sync pipeline (CSP pattern - single input/output per task)
//
// The download supervisor's input and its workers' output are select_channels:
// the supervisor selects over both (and its timer) from one loop, and a worker
// hands its end-of-task report over with a suspended send that nobody can race
// (see block_download_task). A select_channel has no executor and no
// async_send/async_receive: producers use try_send(msg) or a selection, and
// close() is the shutdown signal (there is no cancel()).
using block_download_input_channel = select_channel<block_download_input>;
using block_download_task_output_channel = select_channel<block_download_task_output>;
// Supervisor output - carries blocks and performance stats
using block_supervisor_output = std::variant<downloaded_light_block, peer_performance>;
using block_download_channel = concurrent_channel<block_supervisor_output>;  // supervisor -> bridge
using block_validation_input_channel = concurrent_channel<block_validation_input>;
using block_validated_channel = concurrent_channel<block_validated>;

// Fast validation pipeline (chunk-based, bypasses supervisor→bridge→validation)
//
// The hot path of the IBD: every download worker sends its chunk here, and the
// one validation task receives. A select_channel, so a worker whose chunk finds
// the channel full SUSPENDS on the send with the chunk still its own (no copy
// per attempt, no 100ms poll), and is resumed by the receive that frees the
// slot, by close() at shutdown (broken_pipe, payload returned) or by its
// coordinator's stop token (operation_aborted, payload returned). The in-flight
// bound is explicit: the channel capacity plus one chunk per download worker.
using fast_validation_input = std::variant<stop_request, downloaded_chunk>;
using fast_validation_input_channel = select_channel<fast_validation_input>;
using chunk_validated_channel = concurrent_channel<chunk_validated>;

// Block storage pipeline (writes validated chunks to flat files)
// A wake-up only: the existing barrier's pause flag is authoritative. When
// the bounded storage input is full, a queued chunk already wakes its owner.
struct storage_reorg_wake {};
using block_storage_input = std::variant<stop_request, downloaded_chunk, storage_reorg_wake>;
using block_storage_input_channel = concurrent_channel<block_storage_input>;

// Control
using stop_channel = concurrent_event_channel;

// -----------------------------------------------------------------------------
// Sync coordinator messages (unified input channel)
// -----------------------------------------------------------------------------

// Sync coordinator unified event (combines all input sources to avoid || operator)
// This prevents message loss that occurs when async_receive is cancelled by ||
/// The UTXO builder has published a new height (#663).
///
/// The coordinator cannot start the post-checkpoint range until the UTXO set
/// describes the state below it, and the builder runs on its own schedule with
/// nothing to announce when it advances. Without this the coordinator would sit
/// on its receive point waiting for an event that has no reason to arrive, and
/// the range would start on the next unrelated one — a peer connecting, a header
/// batch landing — which is how the range came to depend on an accident.
///
/// Emitted by the builder only after a coherent connected view is published.
/// The height is diagnostic; delayed delivery cannot authorize finalization.
/// The coordinator reads the current immutable view and active branch itself.
struct utxo_build_advanced {
    uint32_t built_height{0};
};

// Capacity one in the orchestrator. Every queued value is the same instruction:
// re-read the latest connected view. A full channel therefore retains the debt
// for all publications, irrespective of the diagnostic height it carries.
using utxo_build_progress_channel = concurrent_channel<utxo_build_advanced>;

// Retry one failed admission read. The coordinator owns the generation and
// ignores a deadline that was replaced or cancelled before it was delivered.
struct body_range_retry {
    uint64_t generation{0};
};

// One or more blocks were announced. Carries nothing: the hashes are held on
// the node, registered before the wake that produced this, so a wake dropped
// for a full channel loses none of them — whoever answers the queued one drains
// everything registered by then (#706).
struct blocks_announced_event {};

// The header ingest refused headers under local pressure. Nothing in flight
// can be relied on to bring them, so a request from the tip is owed.
struct headers_owed_event {};

// Header stimulus `seq` was taken by its peer's outbound queue: a local fact,
// not a write to the wire. `seq` is the walk's own number.
struct header_stimulus_accepted {
    uint64_t seq;
};

// Header stimulus `seq` could not be sent: its session is stopping.
struct header_stimulus_failed {
    uint64_t seq;
};

// The retry of the header walk's park `generation` is due. The generation is
// the walk's own number.
struct header_walk_retry {
    uint64_t generation;
};

// The deadline of header stimulus `seq` expired. `seq` is the walk's own
// number, never anything read off the wire.
struct header_walk_timeout {
    uint64_t seq;
};

using sync_coordinator_event = std::variant<
    stop_request,
    peers_updated,
    blocks_announced_event,
    headers_owed_event,
    header_stimulus_accepted,
    header_stimulus_failed,
    header_walk_timeout,
    header_walk_retry,
    headers_validated,
    block_validated,
    chunk_validated,
    utxo_build_advanced,
    body_range_retry
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
