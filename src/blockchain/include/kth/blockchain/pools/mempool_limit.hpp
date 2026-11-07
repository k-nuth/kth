// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_LIMIT_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_LIMIT_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

#include <kth/domain.hpp>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool.hpp>

namespace kth::blockchain {

// BCHN's virtual size: a transaction heavy in signature checks counts as
// larger, so what it pays per byte reflects the work of validating it.
inline constexpr uint64_t bytes_per_sigcheck = 50;

// BCHN MEMPOOL_FULL_FEE_INCREMENT: what an eviction adds to the rate it evicted,
// in satoshis per 1000 bytes, to make the pool's minimum fee.
inline constexpr uint64_t mempool_full_fee_increment_per_kb = 1000;

[[nodiscard]] constexpr
uint64_t virtual_size(uint64_t size, uint64_t sigchecks) noexcept {
    return std::max(size, sigchecks * bytes_per_sigcheck);
}

// Satoshis per 1000 bytes, truncated (BCHN CFeeRate(fee, size)).
[[nodiscard]] constexpr
uint64_t fee_per_kb(uint64_t fee, uint64_t size) noexcept {
    return size == 0 ? 0 : fee * 1000 / size;
}

// The fee a rate asks of `size` bytes: truncated, and at least one satoshi
// when the rate is not zero (BCHN CFeeRate::GetFee).
[[nodiscard]] constexpr
uint64_t fee_for(uint64_t per_kb, uint64_t size) noexcept {
    auto const fee = per_kb * size / 1000;
    return fee == 0 && per_kb > 0 && size > 0 ? 1 : fee;
}

// The mempool's size limit (BCHN -maxmempool, TrimToSize, GetMinFee): the
// eviction order, the memory it counts and the minimum fee a full pool asks.
//
// Ownership: block_chain owns it and touches it only while holding the
// validation mutex, which every mempool writer in the node already holds, so
// it keeps no lock of its own. The mempool stays a separate, lock-free
// structure that tolerates concurrent writers; it does not know about this
// state, and each of its removals reports the ids it removed so the owner can
// account for them here. Invariant: this tracks exactly the transactions the
// owner admitted to the pool and has not seen leave it.
struct KB_API mempool_limit {
    // A transaction entered the pool.
    void added(mempool_entry const& entry);

    // Transactions left the pool. Ids it does not track are ignored.
    void removed(std::vector<hash_digest> const& txids);

    // The estimated memory of the tracked transactions, in bytes.
    [[nodiscard]]
    uint64_t usage() const { return usage_; }

    // The memory one entry is estimated to take: the transaction, its slots in
    // the pool's maps and its place here (BCHN limits DynamicMemoryUsage, not
    // the serialized size).
    [[nodiscard]]
    static uint64_t entry_usage(mempool_entry const& entry);

    // Evict from `pool` the transaction with the lowest fee per 1000 virtual
    // bytes, with its descendants, until usage() is at most `limit`; at equal
    // rates the most recent goes first. Each eviction raises the minimum fee to
    // the evicted rate plus `increment_per_kb`. Returns how many left the pool.
    std::size_t trim(mempool& pool, uint64_t limit, uint64_t increment_per_kb);

    // The fee per 1000 virtual bytes a transaction must pay to enter the pool
    // now: 0 until the pool has been full. After a block it halves every 12
    // hours, faster while usage is under half or a quarter of `limit`. `now` is
    // unix seconds.
    [[nodiscard]]
    uint64_t minimum_fee_per_kb(uint64_t limit, uint64_t now);

    // A block was connected at `now`: the minimum fee may start to decay.
    void note_block_connected(uint64_t now);

private:
    // The eviction order: lowest rate first, then the most recent.
    struct key {
        uint64_t rate_per_kb;
        uint64_t sequence;
        hash_digest txid;

        friend bool operator<(key const& a, key const& b) {
            if (a.rate_per_kb != b.rate_per_kb) return a.rate_per_kb < b.rate_per_kb;
            if (a.sequence != b.sequence) return a.sequence > b.sequence;
            return a.txid < b.txid;
        }
    };

    struct tracked {
        key order;
        uint64_t usage;
    };

    std::set<key> order_;
    std::map<hash_digest, tracked> by_txid_;
    uint64_t usage_ = 0;
    uint64_t next_sequence_ = 0;
    double rolling_minimum_per_kb_ = 0;
    uint64_t last_rolling_update_ = 0;
    bool block_since_rolling_bump_ = false;
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_LIMIT_HPP
