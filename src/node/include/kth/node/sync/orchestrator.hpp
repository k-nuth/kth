// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_ORCHESTRATOR_HPP
#define KTH_NODE_SYNC_ORCHESTRATOR_HPP

#include <functional>
#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/p2p_node.hpp>
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

// `on_fatal` reports a condition sync cannot go on from — the persisted chain
// and the chain in memory describing different branches after a reorganization.
// Sync stops sending work the moment it fires; winding the process down is the
// node owner's, so that logic is not half-repeated here.
::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    kth::node::p2p_node& network,
    domain::config::network network_type,
    std::function<void(std::string const&)> on_fatal
);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_ORCHESTRATOR_HPP
