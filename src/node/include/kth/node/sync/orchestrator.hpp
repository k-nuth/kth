// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_ORCHESTRATOR_HPP
#define KTH_NODE_SYNC_ORCHESTRATOR_HPP

#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/network/p2p_node.hpp>
#include <kth/node/sync/messages.hpp>

namespace kth::node::sync {

// =============================================================================
// Sync Orchestrator
// =============================================================================
//
// Main entry point for the CSP-based sync system.
// Creates all channels and spawns all independent tasks:
//
// Tasks spawned:
// 1. peer_provider_task     - Watches network for new peers
// 2. header_download_task   - Downloads headers from peers
// 3. header_validation_task - Validates headers (single writer to organizer)
// 4. block_download_supervisor - Spawns per-peer download tasks
// 5. block_validation_task  - Validates blocks (single writer to chain)
// 6. sync_coordinator_task  - Orchestrates the sync flow
//
// Communication:
// - All tasks communicate ONLY via channels
// - No shared mutable state (except one atomic for chunk assignment)
// - No mutexes, no locks
//
// =============================================================================

::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_ORCHESTRATOR_HPP
