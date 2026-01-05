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

// Header download task output (can be headers or failure report)
using header_download_event = std::variant<downloaded_headers, peer_failure_report>;

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
};

// =============================================================================
// Channel Type Aliases
// =============================================================================

// Peer distribution
using peer_channel = concurrent_channel<peers_updated>;

// Header sync pipeline
using header_request_channel = concurrent_channel<header_request>;
using header_download_channel = concurrent_channel<header_download_event>;
using header_validated_channel = concurrent_channel<headers_validated>;

// Block sync pipeline
using block_request_channel = concurrent_channel<block_range_request>;
using block_download_channel = concurrent_channel<downloaded_block>;
using block_validated_channel = concurrent_channel<block_validated>;

// Control
using stop_channel = concurrent_event_channel;

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_MESSAGES_HPP
