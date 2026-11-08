// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/mempool_limit.hpp>

#include <cmath>

namespace kth::blockchain {

namespace {

// BCHN ROLLING_FEE_HALFLIFE: twelve hours.
constexpr double rolling_fee_half_life = 12.0 * 60 * 60;

// A red-black tree node beside its value: three links and the colour.
constexpr uint64_t tree_node_usage = 4 * sizeof(void*);

} // namespace

uint64_t mempool_limit::entry_usage(mempool_entry const& entry) {
    auto const& tx = *entry.tx;
    // The transaction: the object and its shared_ptr control block, its input
    // and output arrays, and the scripts they own, which the serialized size
    // bounds from above.
    uint64_t usage = sizeof(domain::message::transaction) + 2 * sizeof(void*)
        + tx.inputs().capacity() * sizeof(domain::chain::input)
        + tx.outputs().capacity() * sizeof(domain::chain::output)
        + entry.size;
    // Its slot in the pool, one spent_by slot per input, and its place here.
    usage += sizeof(hash_digest) + sizeof(mempool_entry);
    usage += tx.inputs().size() * (sizeof(outpoint_key) + sizeof(hash_digest));
    usage += sizeof(key) + tree_node_usage;
    usage += sizeof(hash_digest) + sizeof(tracked) + tree_node_usage;
    return usage;
}

void mempool_limit::added(mempool_entry const& entry) {
    auto const txid = entry.tx->hash();
    key const order{fee_per_kb(entry.fee, virtual_size(entry.size, entry.sigchecks)),
        next_sequence_++, txid};
    auto const usage = entry_usage(entry);
    if ( ! by_txid_.emplace(txid, tracked{order, usage}).second) {
        return;
    }
    order_.insert(order);
    usage_ += usage;
}

void mempool_limit::removed(std::vector<hash_digest> const& txids) {
    for (auto const& txid : txids) {
        auto const it = by_txid_.find(txid);
        if (it == by_txid_.end()) {
            continue;
        }
        order_.erase(it->second.order);
        usage_ -= std::min(usage_, it->second.usage);
        by_txid_.erase(it);
    }
}

std::size_t mempool_limit::trim(mempool& pool, uint64_t limit, uint64_t increment_per_kb) {
    std::size_t evicted = 0;
    while (usage_ > limit && ! order_.empty()) {
        auto const lowest = *order_.begin();

        // The minimum fee becomes the evicted rate plus the increment, so what
        // was evicted cannot come straight back at the same price (BCHN
        // trackPackageRemoved). It does not decay before a block.
        auto const raised = double(lowest.rate_per_kb + increment_per_kb);
        if (raised > rolling_minimum_per_kb_) {
            rolling_minimum_per_kb_ = raised;
            block_since_rolling_bump_ = false;
        }

        // Its descendants spend its outputs, so they cannot stay without it.
        auto const gone = pool.remove_with_descendants(lowest.txid);
        evicted += gone.size();
        removed(gone);
        // Whatever the pool reported, the lowest leaves the order, so the loop
        // always advances.
        removed({lowest.txid});
        order_.erase(lowest);
    }
    return evicted;
}

uint64_t mempool_limit::minimum_fee_per_kb(uint64_t limit, uint64_t now) {
    if ( ! block_since_rolling_bump_ || rolling_minimum_per_kb_ == 0) {
        return uint64_t(std::ceil(rolling_minimum_per_kb_));
    }
    if (now > last_rolling_update_ + 10) {
        auto half_life = rolling_fee_half_life;
        if (usage_ < limit / 4) {
            half_life /= 4;
        } else if (usage_ < limit / 2) {
            half_life /= 2;
        }
        rolling_minimum_per_kb_ /= std::pow(2.0, double(now - last_rolling_update_) / half_life);
        last_rolling_update_ = now;
    }
    return uint64_t(std::ceil(rolling_minimum_per_kb_));
}

void mempool_limit::note_block_connected(uint64_t now) {
    last_rolling_update_ = now;
    block_since_rolling_bump_ = true;
}

} // namespace kth::blockchain
