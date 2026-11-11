// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_ADMISSIONS_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_ADMISSIONS_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool.hpp>

namespace kth::blockchain {

/// The pooled transactions in the order they were admitted (BCHN's entryId
/// order, a topological one), for announcing them to peers: each peer's
/// service walks it from where it last stopped.
///
/// Plain state. The chain keeps it under its validation mutex, as it keeps
/// every write to the mempool: it is written as a transaction is admitted and
/// read under the same exclusion.
///
/// Removals from the pool are not reported to it. What it lists is checked
/// against the pool as it is read, and what left the pool (or came back under
/// a later id) is dropped then; it is also swept as it grows past twice the
/// pool, so a node no peer reads from keeps it bounded.
class KB_API mempool_admissions {
public:
    /// The id for the next admission, larger than any before it.
    [[nodiscard]]
    uint64_t next_id() { return ++last_; }

    /// The last id handed out; none yet is zero.
    [[nodiscard]]
    uint64_t last() const { return last_; }

    /// `txid` entered `pool` as `id`.
    void record(uint64_t id, hash_digest const& txid, mempool const& pool);

    /// Up to `limit` entries still pooled, admitted after `after`, in order.
    [[nodiscard]]
    std::vector<mempool_entry> after(uint64_t after, size_t limit, mempool const& pool);

    /// Entries recorded and not yet found gone.
    [[nodiscard]]
    size_t size() const { return order_.size(); }

private:
    void sweep(mempool const& pool);

    uint64_t last_{0};
    std::map<uint64_t, hash_digest> order_;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_ADMISSIONS_HPP
