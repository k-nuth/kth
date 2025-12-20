// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_SESSION_HPP
#define KTH_NODE_SYNC_SESSION_HPP

// =============================================================================
// Sync Session - Headers-First Block Synchronization
// =============================================================================
//
// This class coordinates the headers-first synchronization protocol:
//
// SYNC FLOW:
// ----------
// 1. Build block locator from current chain state
// 2. Send getheaders to peer, receive headers
// 3. Validate and store headers (via header_organizer)
// 4. Request blocks for validated headers (getdata)
// 5. Receive and organize blocks
// 6. Repeat until synced with peer
//
// HEADERS-FIRST BENEFITS:
// -----------------------
// - Lower bandwidth: Headers are ~80 bytes vs full blocks (MB)
// - Faster validation: Can validate PoW chain before downloading blocks
// - Parallel downloads: Can request blocks from multiple peers
//
// ARCHITECTURE:
// -------------
// Uses coroutines for async I/O with peer_session.
// Delegates header caching and validation to header_organizer.
// Interacts with blockchain via block_chain::organize().
//
// =============================================================================

#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <kth/blockchain.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>
#include <kth/network.hpp>
#include <kth/node/define.hpp>

#include <asio/awaitable.hpp>

namespace kth::node {

/// Configuration for sync session
struct sync_config {
    /// Maximum headers to request per getheaders
    size_t max_headers_per_request{2000};

    /// Maximum blocks to request per getdata
    size_t max_blocks_per_request{16};

    /// Timeout for individual header request (waiting for response)
    std::chrono::seconds headers_timeout{30};

    /// Timeout for block requests
    std::chrono::seconds block_timeout{60};

    /// Minimum peer protocol version for headers-first sync
    uint32_t minimum_version{70012};

    // -------------------------------------------------------------------------
    // BCHN-style timeout parameters
    // -------------------------------------------------------------------------

    /// Headers download timeout base (BCHN: 15 minutes)
    /// Used to calculate total allowed time for headers sync
    std::chrono::seconds headers_download_timeout_base{15 * 60};

    /// Headers download timeout per expected header (BCHN: 1ms/header)
    /// Total timeout = base + per_header * expected_headers
    std::chrono::microseconds headers_download_timeout_per_header{1000};

    /// Chain sync timeout for outbound peers (BCHN: 20 minutes)
    /// If peer's chain has less work than ours after this time, send getheaders
    std::chrono::seconds chain_sync_timeout{20 * 60};

    /// Headers response time after getheaders sent (BCHN: 2 minutes)
    /// If peer doesn't respond with better chain after this, disconnect
    std::chrono::seconds headers_response_timeout{2 * 60};

    /// Block stalling timeout (BCHN: 2 seconds)
    /// If block download window can't move for this long, disconnect
    std::chrono::seconds block_stalling_timeout{2};

    /// Block download timeout base, in units of block interval (BCHN: 10 min equivalent)
    /// Actual timeout = block_interval * (base + per_peer * num_other_peers)
    double block_download_timeout_base{1.0};

    /// Block download timeout per additional peer (BCHN: 5 min equivalent)
    double block_download_timeout_per_peer{0.5};
};

/// Result of a sync operation
struct sync_result {
    code error;
    size_t headers_received{0};
    size_t blocks_received{0};
    size_t final_height{0};
    bool timed_out{false};  // True if sync failed due to timeout (peer too slow)
};

/// Sync session - coordinates headers-first sync with a single peer
class KND_API sync_session {
public:
    using ptr = std::shared_ptr<sync_session>;

    /// Construct sync session
    /// @param target_height Override target height (0 = use peer's start_height)
    sync_session(
        blockchain::block_chain& chain,
        network::peer_session::ptr peer,
        domain::config::network network,
        sync_config const& config = {},
        size_t target_height = 0);

    /// Run synchronization with peer
    /// Returns when synced or on error
    [[nodiscard]]
    ::asio::awaitable<sync_result> run();

    /// Check if sync is complete (peer has no more headers)
    [[nodiscard]]
    bool is_synced() const;

    /// Get current sync heights (header_height, block_height)
    [[nodiscard]]
    std::pair<size_t, size_t> current_heights() const;

private:
    // Build block locator for getheaders
    hash_list build_locator() const;

    // Sync phases
    // Fetch headers and add to organizer, returns hashes for block download
    ::asio::awaitable<code> sync_headers_batch();
    ::asio::awaitable<code> sync_blocks(std::vector<hash_digest> const& hashes);

    blockchain::block_chain& chain_;
    blockchain::header_organizer organizer_;
    network::peer_session::ptr peer_;
    sync_config config_;

    size_t header_height_{0};  // Current header-sync height (from DB at start)
    size_t block_height_{0};   // Current block-sync height
    size_t target_height_{0};  // Peer's start_height from version message
    hash_digest last_header_hash_{null_hash};
    bool synced_{false};

    // Statistics
    size_t total_headers_{0};
    size_t total_blocks_{0};

    // BCHN-style timeout tracking
    using clock = std::chrono::steady_clock;
    clock::time_point headers_sync_start_;      // When headers sync started
    clock::time_point headers_sync_deadline_;   // Deadline for headers sync completion
    bool headers_sync_timed_out_{false};
};

/// Create a sync session for a peer
/// @param target_height Override target height (0 = use peer's start_height)
[[nodiscard]]
KND_API sync_session::ptr make_sync_session(
    blockchain::block_chain& chain,
    network::peer_session::ptr peer,
    domain::config::network network,
    sync_config const& config = {},
    size_t target_height = 0);

/// Run sync with the best available peer
/// Selects peer based on start_height from version message
[[nodiscard]]
KND_API ::asio::awaitable<sync_result> sync_from_best_peer(
    blockchain::block_chain& chain,
    network::p2p_node& p2p,
    domain::config::network network,
    sync_config const& config = {});

} // namespace kth::node

#endif // KTH_NODE_SYNC_SESSION_HPP
