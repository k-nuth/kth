// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_ORCHESTRATOR_HPP
#define KTH_NODE_SYNC_ORCHESTRATOR_HPP

#include <expected>
#include <optional>
#include <functional>
#include <asio/awaitable.hpp>

#include <kth/blockchain.hpp>
#include <kth/node/p2p_node.hpp>
#include <kth/node/define.hpp>
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

// =============================================================================
// May the post-checkpoint range start? (#663)
// =============================================================================
//
// To validate the block at height H the UTXO set must already describe the chain
// through H - 1: full validation resolves that block's prevouts against the set,
// and a set that is short answers a PROVEN ABSENCE, which is indistinguishable
// from an output that was never there. The block is then rejected with a
// consensus verdict for a condition that is purely transient.
//
// The coordinator used to start the range on "the blocks below it are validated"
// alone, which says nothing about the builder — it runs on its own poll loop,
// following the contiguous height storage publishes, and lags by however much it
// lags.
//
// @par Why the condition is not `utxo_built_height >= checkpoint_height`
// Because that condition was TRUE in the run that exposed this. The checkpoint
// was 951146, the range was about to start at 963888, and the UTXO set was at
// 962946: past the checkpoint by twelve thousand blocks and still 941 short of
// what the first block of the range needed. The checkpoint is where full
// validation BEGINS; it says nothing about where the builder has reached, and
// gating on it would have left the defect exactly as it was.
//
// A pure function over two values, so each case below can be a value rather than
// a scenario to reproduce.
enum class slow_sync_admission {
    /// The set describes the state below `start_height`. Send the range.
    start,
    /// Not yet, and nothing is wrong: the builder is behind and still moving.
    builder_behind,
    /// The height could not be read. Fail-closed — a store that will not say how
    /// far it has built is not a store to validate against, and "no answer" must
    /// never be read as "far enough".
    height_unavailable
};

/// Always called with a FRESHLY read height. The coordinator's wake-up message
/// carries a height too, and that one is logged and never passed here: it can sit
/// in the channel while a reorg, a startup reconciliation or a rebuild moves the
/// real height under it, and admitting on the stale number would send the range
/// against a set that no longer describes what the message claimed.
///
/// @param start_height The first block the range would validate.
/// @param built The builder's published height, or why it could not be read.
[[nodiscard]] KND_API
slow_sync_admission may_start_slow_sync(
    uint32_t start_height,
    std::expected<uint32_t, database::result_code> const& built);

/// One tick of the builder-progress bridge.
struct utxo_progress_step {
    /// The height to announce, or nullopt to stay quiet.
    std::optional<uint32_t> announce;
    /// What to remember for the next tick.
    std::optional<uint32_t> next_last_seen;

    friend bool operator==(utxo_progress_step const&, utxo_progress_step const&) = default;
};

/// What the builder-progress bridge should do this tick.
///
/// The bridge's whole job is to make the coordinator ask again when the answer
/// above could have changed, and its only inputs are a timer and the store — no
/// peer event, no header batch, nothing another task has to do first. That is the
/// property #663 is about, so the decision is a value here rather than something
/// only a running orchestrator could show.
///
/// Announces on CHANGE, so a builder sitting between batches is silent.
///
/// A read that failed announces nothing — the coordinator asks the store itself
/// when it evaluates, and fail-closed there is the decision. But it also FORGETS
/// the last height, and that is the part worth stating: without it, a store that
/// hiccups while the coordinator is holding and then answers the SAME height again
/// would produce no announcement, and the coordinator would go on waiting for an
/// unrelated event — the exact stall this bridge exists to remove, reached through
/// a different door.
///
/// @param last_seen The height last announced, or nullopt if none has been.
/// @param built     What the store answered this tick.
[[nodiscard]] KND_API
utxo_progress_step utxo_progress_for_tick(
    std::optional<uint32_t> last_seen,
    std::expected<uint32_t, database::result_code> const& built);

/// `slow_sync_admission` as text, for the one log line that reports it.
[[nodiscard]] KND_API
char const* to_string(slow_sync_admission admission);

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
