// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/mempool.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <kth/infrastructure/utility/pseudo_random.hpp>

namespace kth::blockchain {

using namespace kth::domain::chain;

namespace {
// Bucket hint for the concurrent maps. The pool grows as needed; this just
// avoids early rehashing on a busy node.
constexpr std::size_t initial_capacity = std::size_t(1) << 16;
} // namespace

// The mempool is lock-free: it relies on the concurrent maps' per-key atomic
// operations, not on the organizers' mutex. Writes are (today) still serialised
// by that mutex, but the logic below is correct without it — see the header.

mempool::mempool()
    : mempool(pseudo_random::generate<uint64_t>(),
              pseudo_random::generate<uint64_t>()) {}

mempool::mempool(uint64_t k0, uint64_t k1)
    : pool_(initial_capacity, salted_txid_hasher{k0, k1})
    , spent_by_(initial_capacity, salted_outpoint_hasher{k0, k1}) {}

bool mempool::add(mempool_entry entry) {
    KTH_STATS_TIME_START(add);
    KTH_STATS_INCREMENT(stats_, add_calls);

    auto const& tx = *entry.tx;
    auto const txid = tx.hash();

    // First-seen: atomically claim every spent outpoint. On any conflict, roll
    // back the claims already made and reject.
    std::vector<outpoint_key> reserved;
    reserved.reserve(tx.inputs().size());
    for (auto const& in : tx.inputs()) {
        auto const& op = in.previous_output();
        outpoint_key const key{op.hash(), op.index()};
        if ( ! spent_by_.try_insert(key, txid)) {
            for (auto const& k : reserved) {
                spent_by_.erase(k);
            }
            KTH_STATS_INCREMENT(stats_, add_rejected_conflict);
            KTH_STATS_TIME_ADD(stats_, add, add_time_ns);
            return false;
        }
        reserved.push_back(key);
    }

    // Commit the transaction; release the claims if it is a duplicate.
    if ( ! pool_.try_insert(txid, std::move(entry))) {
        for (auto const& k : reserved) {
            spent_by_.erase(k);
        }
        KTH_STATS_INCREMENT(stats_, add_rejected_duplicate);
        KTH_STATS_TIME_ADD(stats_, add, add_time_ns);
        return false;
    }

    KTH_STATS_INCREMENT(stats_, add_inserted);
    KTH_STATS_TIME_ADD(stats_, add, add_time_ns);
    return true;
}

void mempool::remove_entry(hash_digest const& txid) {
    transaction_const_ptr tx;
    bool const found = pool_.visit(txid, [&](mempool_entry const& e) {
        tx = e.tx;
    });
    if ( ! found) {
        return;
    }

    for (auto const& in : tx->inputs()) {
        auto const& op = in.previous_output();
        spent_by_.erase(outpoint_key{op.hash(), op.index()});
    }
    pool_.erase(txid);
}

void mempool::remove_recursive(hash_digest const& txid) {
    transaction_const_ptr tx;
    bool const found = pool_.visit(txid, [&](mempool_entry const& e) {
        tx = e.tx;
    });
    if ( ! found) {
        return;
    }

    // Evict every pool tx that spends an output of this one (its descendants),
    // depth-first, before erasing this tx.
    auto const outputs = static_cast<uint32_t>(tx->outputs().size());
    for (uint32_t index = 0; index < outputs; ++index) {
        outpoint_key const out{txid, index};
        hash_digest child;
        bool const has_child = spent_by_.visit(out, [&](hash_digest const& c) {
            child = c;
        });
        if (has_child) {
            remove_recursive(child);
        }
    }

    remove_entry(txid);
}

void mempool::remove_for_block(domain::chain::block const& block) {
    KTH_STATS_INCREMENT(stats_, remove_for_block_calls);
    KTH_STATS_TIME_START(rmv);

    for (auto const& tx : block.transactions()) {
        auto const txid = tx.hash();

        // Rare path: a confirmed tx may double-spend an outpoint claimed by a
        // DIFFERENT pool tx; that pool tx is now invalid — evict it and its
        // descendants.
        for (auto const& in : tx.inputs()) {
            auto const& op = in.previous_output();
            outpoint_key const key{op.hash(), op.index()};
            hash_digest spender;
            bool const found = spent_by_.visit(key, [&](hash_digest const& s) {
                spender = s;
            });
            if (found && spender != txid) {
                remove_recursive(spender);
                KTH_STATS_INCREMENT(stats_, removed_conflict);
            }
        }

        // Normal path: the block confirmed this tx; drop it (non-recursive —
        // its children now spend on-chain outputs and stay valid).
        remove_entry(txid);
        KTH_STATS_INCREMENT(stats_, removed_confirmed);
    }

    KTH_STATS_TIME_ADD(stats_, rmv, remove_time_ns);
}

void mempool::update_for_reorg(block_const_ptr_list_const_ptr /*outgoing*/,
                               block_const_ptr_list_const_ptr incoming) {
    // Evict for the new (connected) chain.
    if (incoming) {
        for (auto const& block : *incoming) {
            remove_for_block(*block);
        }
    }

    // TODO(mempool): re-admit transactions from the disconnected (`outgoing`)
    // blocks that are not in the new chain — they become unconfirmed again.
    // They must be re-validated against the new tip and their fee/size/sigops
    // recomputed before re-entry (parents before children). Deferred; this is
    // the rare path and the common flow does not depend on it. See issue #498.
}

std::optional<output> mempool::resolve(point const& outpoint) const {
    KTH_STATS_INCREMENT(stats_, resolve_calls);

    std::optional<output> result;
    pool_.visit(outpoint.hash(), [&](mempool_entry const& e) {
        auto const& outputs = e.tx->outputs();
        if (outpoint.index() < outputs.size()) {
            result = outputs[outpoint.index()];
        }
    });

    if (result) {
        KTH_STATS_INCREMENT(stats_, resolve_hits);
    }
    return result;
}

bool mempool::contains(hash_digest const& txid) const {
    return pool_.visit(txid, [](mempool_entry const&) {});
}

transaction_const_ptr mempool::get(hash_digest const& txid) const {
    transaction_const_ptr tx;
    pool_.visit(txid, [&](mempool_entry const& e) {
        tx = e.tx;
    });
    return tx;
}

std::size_t mempool::size() const {
    return pool_.size();
}

} // namespace kth::blockchain
