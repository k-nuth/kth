// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POOLS_MEMPOOL_STATS_HPP
#define KTH_BLOCKCHAIN_POOLS_MEMPOOL_STATS_HPP

#include <atomic>
#include <cstdint>

#include <kth/infrastructure/utility/stats.hpp>

namespace kth::blockchain {

// Per-mempool counters/timers, gated by KTH_WITH_STATS (see stats.hpp). Used to
// compare the two map backends (cfm vs parlay) on a running node.
struct mempool_stats {
    // Admission.
    std::atomic<uint64_t> add_calls{0};
    std::atomic<uint64_t> add_inserted{0};
    std::atomic<uint64_t> add_rejected_duplicate{0};
    std::atomic<uint64_t> add_rejected_conflict{0};
    std::atomic<uint64_t> add_time_ns{0};

    // Block connect / reorg eviction.
    std::atomic<uint64_t> remove_for_block_calls{0};
    std::atomic<uint64_t> removed_confirmed{0};
    std::atomic<uint64_t> removed_conflict{0};
    std::atomic<uint64_t> remove_time_ns{0};

    // Prevout resolution (chained-tx overlay).
    std::atomic<uint64_t> resolve_calls{0};
    std::atomic<uint64_t> resolve_hits{0};
    std::atomic<uint64_t> resolve_time_ns{0};

    // Query readers (JSON-RPC-facing: getrawmempool / getmempoolentry /
    // getmempoolinfo / getmempoolancestors / getmempooldescendants).
    std::atomic<uint64_t> entry_calls{0};
    std::atomic<uint64_t> entry_time_ns{0};
    std::atomic<uint64_t> parents_calls{0};
    std::atomic<uint64_t> parents_time_ns{0};
    std::atomic<uint64_t> children_calls{0};
    std::atomic<uint64_t> children_time_ns{0};
    std::atomic<uint64_t> ancestors_calls{0};
    std::atomic<uint64_t> ancestors_time_ns{0};
    std::atomic<uint64_t> descendants_calls{0};
    std::atomic<uint64_t> descendants_time_ns{0};
    std::atomic<uint64_t> txids_calls{0};
    std::atomic<uint64_t> txids_time_ns{0};
    std::atomic<uint64_t> info_calls{0};
    std::atomic<uint64_t> info_time_ns{0};

    void reset_all() {
        add_calls = 0; add_inserted = 0; add_rejected_duplicate = 0;
        add_rejected_conflict = 0; add_time_ns = 0;
        remove_for_block_calls = 0; removed_confirmed = 0; removed_conflict = 0;
        remove_time_ns = 0;
        resolve_calls = 0; resolve_hits = 0; resolve_time_ns = 0;
        entry_calls = 0; entry_time_ns = 0;
        parents_calls = 0; parents_time_ns = 0;
        children_calls = 0; children_time_ns = 0;
        ancestors_calls = 0; ancestors_time_ns = 0;
        descendants_calls = 0; descendants_time_ns = 0;
        txids_calls = 0; txids_time_ns = 0;
        info_calls = 0; info_time_ns = 0;
    }
};

} // namespace kth::blockchain

#endif // KTH_BLOCKCHAIN_POOLS_MEMPOOL_STATS_HPP
