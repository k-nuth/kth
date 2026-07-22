// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_RPC_JOB_STORE_HPP
#define KTH_NODE_RPC_JOB_STORE_HPP

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <kth/blockchain/pools/block_template.hpp>

namespace kth::node::rpc {

// Stores the transaction selection behind each getblocktemplatelight job so a
// later submitblocklight can reconstruct the full block from the miner's solved
// header + coinbase alone (transactions never travel over the wire). Bounded by
// count (gbtcachesize) with insertion-order eviction and a TTL (gbtstoretime).
// Thread-safe: RPC handlers run on the shared asio executor.
struct job_store {
    job_store(std::size_t max_jobs, std::uint32_t ttl_seconds);

    // Store `txs` under a content-derived job id (hex) and return it. Re-adding
    // the same selection yields the same id (idempotent).
    std::string add(std::vector<transaction_const_ptr> txs);

    // The transactions for `job_id`, or nullopt if unknown or expired. Used by
    // submitblocklight (F1b).
    std::optional<std::vector<transaction_const_ptr>> get(std::string const& job_id) const;

private:
    struct entry {
        std::vector<transaction_const_ptr> txs;
        std::uint32_t created;
    };

    void evict_locked();

    mutable std::mutex mutex_;
    boost::unordered_flat_map<std::string, entry> jobs_;
    std::deque<std::string> order_;   // insertion order, for count eviction
    std::size_t max_jobs_;
    std::uint32_t ttl_seconds_;
};

} // namespace kth::node::rpc

#endif // KTH_NODE_RPC_JOB_STORE_HPP
