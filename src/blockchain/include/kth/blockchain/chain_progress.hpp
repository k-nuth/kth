// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_CHAIN_PROGRESS_HPP
#define KTH_BLOCKCHAIN_CHAIN_PROGRESS_HPP

#include <cstddef>
#include <cstdint>
#include <kth/infrastructure/math/hash.hpp>

namespace kth::blockchain {

// Owned metadata for one block, relative to one captured connected publication.
// Height counts blocks; ticks count scheduled seconds, never elapsed real time.
struct block_progress {
    size_t height;
    uint64_t ticks;
    uint64_t target_time;
    uint64_t next_target_time;
    int64_t confirmations;
    int64_t confirmations_ticks;
    hash_digest block_hash;
};

} // namespace kth::blockchain
#endif
