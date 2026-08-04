// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_SYNC_REORG_HPP
#define KTH_NODE_SYNC_REORG_HPP

#include <cstdint>
#include <functional>

#include <asio/awaitable.hpp>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/header_organizer.hpp>

namespace kth::node::sync {

// Retires a reorg-barrier participant on every exit path. A leaked registration
// leaves the registered count above the parked count forever, so no later switch
// can ever reach the barrier.
//
// Every task that writes chain state holds one for its lifetime: block storage,
// the UTXO build, and header persistence. While one is registered and not parked,
// a switch waits — which is what keeps a write that is already under way from
// landing after the chain moved out from under it.
struct reorg_participation {
    explicit reorg_participation(blockchain::block_chain& chain) : chain_(chain) {
        chain_.register_reorg_participant();
    }
    ~reorg_participation() { chain_.unregister_reorg_participant(); }
    reorg_participation(reorg_participation const&) = delete;
    reorg_participation& operator=(reorg_participation const&) = delete;
private:
    blockchain::block_chain& chain_;
};

// Rewrite the by-height header table over [from_height, to_height] from the
// active chain.
//
// That table is what `get_header(height)` answers from, and it is written by
// append during sync, so nothing updates it when a switch replaces the block at
// a height. Until it is rewritten, median time past, the staleness check and the
// RPC surface all keep reading the branch the node abandoned.
//
// Returns false if it stopped early: a height that is not on the active chain, a
// failed write, or the chain moving underneath it (checked per batch, so a run
// racing a switch stops instead of interleaving two branches in one table).
[[nodiscard]]
bool persist_active_headers(
    blockchain::block_chain& chain,
    blockchain::header_index const& index,
    uint32_t from_height,
    uint32_t to_height);

// What a switch left behind, and whether the node can carry on from it.
struct reorg_outcome {
    blockchain::block_chain::switch_result result;

    // The chain moved but its persisted description did not, and nothing repairs
    // that while the node runs: the by-height header table is what the header
    // index is rebuilt from at startup, so carrying on means running with the two
    // disagreeing and coming back up on the abandoned branch after a restart —
    // with the UTXO set already rewound below it.
    //
    // The caller owns the node's lifecycle, so it is the caller that must shut it
    // down: stop the network, and send no further work.
    bool fatal{false};
};

// Move the node onto another branch.
//
// The switch itself is one call, but it cannot stand alone: the tasks that write
// chain state have to be parked first (the switch rewrites the UTXO set), and
// the organizer's notion of the tip has to move with the chain afterwards — it
// is what decides whether the next header batch extends the chain or forks it,
// so an organizer left on the abandoned branch would read every later batch as
// another fork and ask for a switch whose fork height no longer matches.
//
// Keeping that sequence in one place is what lets it be driven the same way in a
// test as in the coordinator.
//
// `abort` is polled while waiting at the barrier (node shutdown); when it fires
// the switch is not attempted and a failed result is returned, leaving the chain
// untouched. So does waiting past the barrier deadline: a writer that never
// parks is a bug, and holding the pause open forever on top of it would stall
// sync silently.
::asio::awaitable<reorg_outcome> execute_reorg(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    blockchain::header_index::index_t branch_head,
    uint32_t fork_height,
    std::function<bool()> abort);

} // namespace kth::node::sync

#endif // KTH_NODE_SYNC_REORG_HPP
