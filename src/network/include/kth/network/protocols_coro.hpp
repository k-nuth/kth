// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NETWORK_PROTOCOLS_CORO_HPP
#define KTH_NETWORK_PROTOCOLS_CORO_HPP

// =============================================================================
// Network Layer Protocol Handlers (Coroutine-based)
// =============================================================================
//
// This file contains coroutine-based implementations of Bitcoin P2P network
// layer protocols. These are generic networking protocols that don't require
// blockchain state.
//
// WHAT THIS FILE PROVIDES:
// ------------------------
// - perform_handshake()  : Version/verack exchange and protocol negotiation
// - run_ping_pong()      : Keepalive ping/pong loop
// - request_addresses()  : Request peer addresses (getaddr)
// - send_addresses()     : Send peer addresses (addr)
//
// LEGACY FILES THIS REPLACES:
// ---------------------------
// Once fully integrated, these coroutine functions replace the following
// callback-based protocol classes:
//
// | Coroutine Function    | Replaces Legacy Files                              |
// |-----------------------|----------------------------------------------------|
// | perform_handshake()   | protocols/protocol_version_31402.hpp/.cpp          |
// |                       | protocols/protocol_version_70002.hpp/.cpp          |
// | run_ping_pong()       | protocols/protocol_ping_31402.hpp/.cpp             |
// |                       | protocols/protocol_ping_60001.hpp/.cpp             |
// | request_addresses()   | protocols/protocol_address_31402.hpp/.cpp          |
// | send_addresses()      | protocols/protocol_seed_31402.hpp/.cpp             |
//
// BASE CLASSES (also to be removed):
// | protocols/protocol.hpp/.cpp                                              |
// | protocols/protocol_events.hpp/.cpp                                       |
// | protocols/protocol_timer.hpp/.cpp                                        |
//
// NOT REPLACED HERE (handled elsewhere or deprecated):
// | protocols/protocol_reject_70002.hpp/.cpp  - Reject messages (BIP 61)     |
//
// USAGE:
// ------
// These functions are designed to be used with peer_session and are typically
// called from p2p_node or higher-level code:
//
//   auto result = co_await perform_handshake(peer, config);
//   if (!result) { /* handle error */ }
//
//   // Run ping/pong in background
//   asio::co_spawn(executor, run_ping_pong(peer, 120s), asio::detached);
//
// =============================================================================

#include <chrono>
#include <expected>
#include <string>

#include <kth/domain.hpp>
#include <kth/infrastructure.hpp>
#include <kth/network/define.hpp>
#include <kth/network/peer_session.hpp>
#include <kth/network/settings.hpp>

#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/awaitable_operators.hpp>

namespace kth::network {

using kth::awaitable_expected;

// =============================================================================
// Message Helpers
// =============================================================================

/// Wait for a specific message type from the peer
/// Returns the deserialized message or an error code
/// @param peer The peer session to receive from
/// @param timeout Maximum time to wait for the message
template <typename Message>
awaitable_expected<Message> wait_for_message(
    peer_session& peer,
    std::chrono::seconds timeout)
{
    using namespace ::asio::experimental::awaitable_operators;

    auto executor = co_await ::asio::this_coro::executor;
    ::asio::steady_timer timer(executor, timeout);

    // Race between message receive and timeout
    auto result = co_await (
        peer.messages().async_receive(::asio::as_tuple(::asio::use_awaitable)) ||
        timer.async_wait(::asio::as_tuple(::asio::use_awaitable))
    );

    // Check which completed first
    if (result.index() == 1) {
        // Timeout won
        co_return std::unexpected(error::channel_timeout);
    }

    // Message received
    auto& [ec, raw] = std::get<0>(result);
    if (ec) {
        co_return std::unexpected(error::channel_stopped);
    }

    // Check command matches expected type
    if (raw.heading.command() != Message::command) {
        // Wrong message type - could handle differently
        co_return std::unexpected(error::bad_stream);
    }

    // Deserialize the message
    byte_reader reader(raw.payload);
    auto msg = Message::from_data(reader, peer.negotiated_version());
    if (!msg) {
        co_return std::unexpected(error::bad_stream);
    }

    co_return std::move(*msg);
}

/// Wait for any message from the peer (returns raw_message)
/// @param peer The peer session to receive from
/// @param timeout Maximum time to wait
awaitable_expected<raw_message> wait_for_any_message(
    peer_session& peer,
    std::chrono::seconds timeout);

// =============================================================================
// Version Handshake Protocol
// =============================================================================

/// Configuration for version handshake
struct handshake_config {
    uint32_t protocol_version;      // Our protocol version
    uint64_t services;              // Our services
    uint64_t invalid_services;      // Services we reject
    uint32_t minimum_version;       // Minimum peer version we accept
    uint64_t minimum_services;      // Minimum peer services we accept
    std::string user_agent;         // Our user agent string
    uint32_t start_height;          // Our current block height
    uint64_t nonce;                 // Connection nonce
    std::chrono::seconds timeout;   // Handshake timeout
    std::vector<std::string> user_agent_blacklist;  // Blacklisted user agents
};

/// Result of a successful handshake
struct handshake_result {
    domain::message::version::const_ptr peer_version;
    uint32_t negotiated_version;
};

/// Perform version handshake with a peer
/// Exchanges version messages and negotiates protocol version
/// @param peer The peer session
/// @param config Handshake configuration
/// @return handshake_result on success, error code on failure
[[nodiscard]]
KN_API awaitable_expected<handshake_result> perform_handshake(
    peer_session& peer,
    handshake_config const& config);

/// Perform version handshake using direct socket I/O (no message pump needed)
/// This version reads/writes directly to the socket, so peer->run() does NOT
/// need to be running. Use this to avoid detached coroutines in connect/accept.
///
/// Usage:
///   auto peer = co_await async_connect(...);
///   auto result = co_await perform_handshake_direct(*peer, config);  // No run() needed!
///   // After handshake, send peer to supervisor which starts run()
///
/// @param peer The peer session (run() must NOT be running yet)
/// @param config Handshake configuration
/// @return handshake_result on success, error code on failure
[[nodiscard]]
KN_API awaitable_expected<handshake_result> perform_handshake_direct(
    peer_session& peer,
    handshake_config const& config);

/// Create handshake config from network settings
[[nodiscard]]
KN_API handshake_config make_handshake_config(
    settings const& network_settings,
    uint32_t current_height,
    uint64_t nonce);

// =============================================================================
// Ping/Pong Protocol
// =============================================================================

/// Run ping/pong loop with a peer
/// Sends periodic pings and responds to incoming pings
/// Runs until the peer is stopped or an error occurs
/// @param peer The peer session
/// @param ping_interval Time between pings
/// @return Error code when loop terminates
[[nodiscard]]
KN_API ::asio::awaitable<code> run_ping_pong(
    peer_session& peer,
    std::chrono::seconds ping_interval);

// =============================================================================
// Address Protocol
// =============================================================================

/// Request addresses from a peer
/// @param peer The peer session
/// @param timeout Maximum time to wait for response
/// @return Vector of addresses or error
[[nodiscard]]
KN_API awaitable_expected<domain::message::address> request_addresses(
    peer_session& peer,
    std::chrono::seconds timeout);

/// Send our addresses to a peer
/// @param peer The peer session
/// @param addresses Addresses to send
[[nodiscard]]
KN_API ::asio::awaitable<code> send_addresses(
    peer_session& peer,
    domain::message::address const& addresses);

// =============================================================================
// Blockchain Protocol Handlers (Headers-First Sync)
// =============================================================================
//
// These protocols handle blockchain synchronization. Unlike the network layer
// protocols above, these require interaction with blockchain state.
//
// SYNC FLOW (Headers-First):
// --------------------------
// 1. request_headers() - Send getheaders, receive headers
// 2. Validate and store headers in blockchain
// 3. request_blocks() - Send getdata for blocks we need
// 4. Receive and validate blocks
// 5. Repeat until synced
//
// ANNOUNCEMENT HANDLING:
// ----------------------
// - handle_inv() - Process inventory announcements from peers
// - handle_headers() - Process unsolicited headers (after sendheaders)
//
// =============================================================================

// -----------------------------------------------------------------------------
// Header Sync Protocol
// -----------------------------------------------------------------------------

/// Request headers from a peer using getheaders message
/// @param peer The peer session
/// @param locator_hashes Block locator hashes (most recent first)
/// @param stop_hash Hash to stop at (null_hash for no limit)
/// @param timeout Maximum time to wait for response
/// @return Headers message or error
[[nodiscard]]
KN_API awaitable_expected<domain::message::headers> request_headers(
    peer_session& peer,
    hash_list const& locator_hashes,
    hash_digest const& stop_hash,
    std::chrono::seconds timeout);

/// Request headers starting from a single known hash
/// Convenience wrapper that creates a single-element locator
[[nodiscard]]
KN_API awaitable_expected<domain::message::headers> request_headers_from(
    peer_session& peer,
    hash_digest const& from_hash,
    std::chrono::seconds timeout);

// -----------------------------------------------------------------------------
// Block Sync Protocol
// -----------------------------------------------------------------------------

/// Request blocks using getdata message
/// @param peer The peer session
/// @param block_hashes Hashes of blocks to request
/// @return Error code (blocks arrive via message handler)
[[nodiscard]]
KN_API ::asio::awaitable<code> request_blocks(
    peer_session& peer,
    hash_list const& block_hashes);

/// Request a single block
/// @param peer The peer session
/// @param block_hash Hash of block to request
/// @param timeout Maximum time to wait for block
/// @return Block message or error
[[nodiscard]]
KN_API awaitable_expected<domain::message::block> request_block(
    peer_session& peer,
    hash_digest const& block_hash,
    std::chrono::seconds timeout);

/// Result of batch block request - block with its height
struct block_with_height {
    uint32_t height;
    domain::message::block block;
};

/// Request multiple blocks in a single getdata (batch mode)
/// Sends ONE getdata with all hashes and receives blocks as they arrive.
/// Much more efficient than requesting blocks one at a time.
/// @param peer The peer session
/// @param blocks Vector of {height, hash} pairs to request
/// @param timeout Maximum time to wait for ALL blocks
/// @return Vector of received blocks with heights, or error
/// @note Blocks may be received out of order; vector is sorted by height on return
[[nodiscard]]
KN_API awaitable_expected<std::vector<block_with_height>> request_blocks_batch(
    peer_session& peer,
    std::vector<std::pair<uint32_t, hash_digest>> const& blocks,
    std::chrono::seconds timeout);

// -----------------------------------------------------------------------------
// Inventory Protocol
// -----------------------------------------------------------------------------

/// Send inventory announcement to peer
/// @param peer The peer session
/// @param inv Inventory to announce
[[nodiscard]]
KN_API ::asio::awaitable<code> send_inventory(
    peer_session& peer,
    domain::message::inventory const& inv);

/// Request data for inventory items
/// @param peer The peer session
/// @param inv Inventory items to request
[[nodiscard]]
KN_API ::asio::awaitable<code> send_getdata(
    peer_session& peer,
    domain::message::get_data const& request);

// -----------------------------------------------------------------------------
// Transaction Protocol
// -----------------------------------------------------------------------------

/// Send a transaction to peer
/// @param peer The peer session
/// @param tx Transaction to send
[[nodiscard]]
KN_API ::asio::awaitable<code> send_transaction(
    peer_session& peer,
    domain::message::transaction const& tx);

/// Request mempool contents from peer (BIP 35)
/// @param peer The peer session
[[nodiscard]]
KN_API ::asio::awaitable<code> request_mempool(
    peer_session& peer);

// -----------------------------------------------------------------------------
// Response Helpers (for serving peers)
// -----------------------------------------------------------------------------

/// Send headers in response to getheaders request
/// @param peer The peer session
/// @param headers Headers to send
[[nodiscard]]
KN_API ::asio::awaitable<code> send_headers(
    peer_session& peer,
    domain::message::headers const& headers);

/// Send a block in response to getdata request
/// @param peer The peer session
/// @param block Block to send
[[nodiscard]]
KN_API ::asio::awaitable<code> send_block(
    peer_session& peer,
    domain::message::block const& block);

/// Send not_found for items we don't have
/// @param peer The peer session
/// @param not_found Items not found
[[nodiscard]]
KN_API ::asio::awaitable<code> send_not_found(
    peer_session& peer,
    domain::message::not_found const& not_found);

// -----------------------------------------------------------------------------
// Message Parsing Helpers
// -----------------------------------------------------------------------------

/// Parse a getheaders message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::get_headers, code> parse_getheaders(
    raw_message const& raw,
    uint32_t version);

/// Parse a getdata message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::get_data, code> parse_getdata(
    raw_message const& raw,
    uint32_t version);

/// Parse an inventory message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::inventory, code> parse_inventory(
    raw_message const& raw,
    uint32_t version);

/// Parse a headers message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::headers, code> parse_headers(
    raw_message const& raw,
    uint32_t version);

/// Parse a block message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::block, code> parse_block(
    raw_message const& raw,
    uint32_t version);

/// Parse a transaction message from raw bytes
[[nodiscard]]
KN_API std::expected<domain::message::transaction, code> parse_transaction(
    raw_message const& raw,
    uint32_t version);

// -----------------------------------------------------------------------------
// Message Loop Handler
// -----------------------------------------------------------------------------

/// Message handler callback type
/// Return true to continue processing, false to stop
using message_handler = std::function<::asio::awaitable<bool>(raw_message const&)>;

/// Run a message processing loop
/// Receives messages and dispatches to handler until stopped
/// @param peer The peer session
/// @param handler Callback for each message
/// @return Error code when loop terminates
[[nodiscard]]
KN_API ::asio::awaitable<code> run_message_loop(
    peer_session& peer,
    message_handler handler);

} // namespace kth::network

#endif // KTH_NETWORK_PROTOCOLS_CORO_HPP
