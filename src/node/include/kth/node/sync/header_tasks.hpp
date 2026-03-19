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
// Downloads headers from peers using CSP pattern.
// - Input: single channel with variant (stop, peers_updated, header_request)
// - Output: single channel with variant (downloaded_headers, peer_failure_report)
// - Messages processed in FIFO order (no arbitrary priority)
// - Uses sticky peer selection (same peer until failure)
//
// =============================================================================

// max_header_height: The maximum height to sync headers to.
// When from_height >= max_header_height, signals sync complete.
// Use 0 for unlimited (sync to network tip).
::asio::awaitable<void> header_download_task(
    header_download_input_channel& input,
    header_download_output_channel& output,
    uint32_t max_header_height = 0
);

// =============================================================================
// Header Validation Task
// =============================================================================
//
// Validates headers and writes to organizer.
// - Input: single channel with variant (stop, downloaded_headers, peer_failure_report)
// - Output: validation results to header_validated_channel
// - Writes to header_organizer (SINGLE WRITER - no lock needed)
//
// =============================================================================

::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    header_validation_input_channel& input,
    header_validated_channel& output
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_HEADER_TASKS_HPP
