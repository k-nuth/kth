// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/mempool_admissions.hpp>

namespace kth::blockchain {

namespace {

// Pooled under this id, not removed or pooled again since.
std::optional<mempool_entry> pooled_as(mempool const& pool, uint64_t id, hash_digest const& txid) {
    auto entry = pool.entry(txid);
    if ( ! entry || entry->entry_id != id) {
        return std::nullopt;
    }
    return entry;
}

} // namespace

void mempool_admissions::record(uint64_t id, hash_digest const& txid, mempool const& pool) {
    order_.emplace(id, txid);
    // Amortized: a sweep walks all of it, and only after it doubled past the pool.
    if (order_.size() > 2 * pool.size() + 1024) {
        sweep(pool);
    }
}

std::vector<mempool_entry> mempool_admissions::after(uint64_t after, size_t limit, mempool const& pool) {
    std::vector<mempool_entry> found;
    auto it = order_.upper_bound(after);
    while (it != order_.end() && found.size() < limit) {
        if (auto entry = pooled_as(pool, it->first, it->second)) {
            found.push_back(std::move(*entry));
            ++it;
        } else {
            it = order_.erase(it);
        }
    }
    return found;
}

void mempool_admissions::sweep(mempool const& pool) {
    std::erase_if(order_, [&pool](auto const& item) {
        return ! pooled_as(pool, item.first, item.second);
    });
}

} // namespace kth::blockchain
