// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_HPP

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool_hashers.hpp>
#include <kth/blockchain/pools/mempool_map.hpp>
#include <kth/blockchain/pools/mempool_stats.hpp>

namespace kth::blockchain {

// One unconfirmed transaction plus the policy metadata computed at admission.
// Holds the tx by shared_ptr (== BCHN CTransactionRef): the pool owns it, and
// prevout resolution / queries copy the shared_ptr out (a refcount bump), never
// the transaction, and it stays alive across concurrent eviction.
struct mempool_entry {
    transaction_const_ptr tx;
    uint64_t fee;
    uint32_t size;
    uint32_t sigops;
    uint64_t time_seen;
};

// The BCH mempool: unconfirmed transactions in two concurrent maps —
//   pool_     : txid    -> entry                (the pool)
//   spent_by_ : outpoint-> spending txid        (== BCHN mapNextTx)
// No CPFP, no ancestor/descendant fee graph, no unconfirmed chain limit. The
// spent_by_ index gives O(1) first-seen double-spend detection and conflict /
// descendant eviction; the descendant set is derived from spent_by_ (an evicted
// tx's outputs are looked up there), so no separate child graph is kept.
//
// Concurrency model: writes (add / remove_for_block / update_for_reorg) are
// serialised by the organizers' shared prioritized_mutex, so they need no
// internal locking; reads (resolve / queries) run concurrently with them, which
// is what the concurrent map backend is for. The operations are plain
// synchronous methods — they are CPU/memory work on the maps with no I/O, so
// they are called directly inside the organizer coroutines without co_await.
struct mempool {
    // Production (cfm) uses a per-node random salt for the hashers; the
    // explicit-salt ctor is for tests / reproducibility.
    mempool();
    mempool(uint64_t k0, uint64_t k1);

    // Admission of an already-validated transaction (scripts/fees checked by
    // the organizer). Returns false if the txid is already present or if any
    // input double-spends an outpoint already spent in the pool (first-seen).
    bool add(mempool_entry entry);

    // A block was connected: remove its transactions (now confirmed) and any
    // pool transaction that conflicts with them (recursively, with descendants).
    void remove_for_block(domain::chain::block const& block);

    // A reorg happened: re-admit transactions from the disconnected blocks that
    // are not in the new chain, then apply remove_for_block for the new blocks.
    void update_for_reorg(block_const_ptr_list_const_ptr outgoing,
                          block_const_ptr_list_const_ptr incoming);

    // CCoinsViewMemPool-style prevout resolution for chained transactions: if
    // `outpoint` is an output of a pool transaction, returns it; otherwise
    // nullopt and the caller falls back to the confirmed UTXO set (UTXO-Z).
    // Takes point (the base of output_point) — the outpoint's validation cache
    // is deliberately not part of the key/lookup.
    std::optional<domain::chain::output>
    resolve(domain::chain::point const& outpoint) const;

    bool contains(hash_digest const& txid) const;
    std::size_t size() const;

    // Fetch a pooled transaction by id (null if absent).
    transaction_const_ptr get(hash_digest const& txid) const;

    // Visit every pooled entry. Snapshot-ish under concurrency (an entry added
    // or removed during the walk may or may not be seen) — fine for the
    // read-only listing/relay queries that use it.
    template <typename Fn>
        requires std::invocable<Fn, mempool_entry const&>
    void for_each(Fn&& fn) const {
        pool_.for_each([&](hash_digest const&, mempool_entry const& entry) {
            fn(entry);
        });
    }

    mempool_stats const& stats() const { return stats_; }

private:
    // Erase one tx from pool_ and release its input-outpoint claims in
    // spent_by_. Non-recursive: descendants are untouched.
    void remove_entry(hash_digest const& txid);

    // Remove a tx and, transitively, every pool tx that spends its outputs
    // (the rare invalid/conflict path).
    void remove_recursive(hash_digest const& txid);

    mempool_map<hash_digest, mempool_entry, salted_txid_hasher> pool_;
    mempool_map<outpoint_key, hash_digest, salted_outpoint_hasher> spent_by_;
    mempool_stats stats_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_HPP
