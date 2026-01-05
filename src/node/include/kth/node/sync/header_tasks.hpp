// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_HEADER_TASKS_HPP
#define KTH_NODE_SYNC_HEADER_TASKS_HPP

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::sync {

// =============================================================================
// Header Download Task
// =============================================================================
//
// Downloads headers from peers.
// - Receives peers from peer_channel (already filtered by peer_provider)
// - Receives requests from header_request_channel
// - Sends downloaded headers to header_download_channel
// - Reports peer issues to peer_issue_channel (peer_provider filters them)
// - Uses sticky peer selection (same peer until failure)
//
// =============================================================================

::asio::awaitable<void> header_download_task(
    peer_channel& peers,
    peer_issue_channel& peer_issues,
    header_request_channel& requests,
    header_download_channel& output,
    stop_channel& stop
);

// =============================================================================
// Header Validation Task
// =============================================================================
//
// Validates headers and writes to organizer.
// - Receives headers from header_download_channel
// - Writes to header_organizer (SINGLE WRITER - no lock needed)
// - Sends validation results to header_validated_channel
//
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    header_download_channel& input,
    header_validated_channel& output,
    stop_channel& stop
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_HEADER_TASKS_HPP
