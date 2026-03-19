// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/interface/block_chain.hpp>

namespace kth::blockchain {

// =============================================================================
// DEPRECATED: Block storage moved to flat files (blk*.dat)
// These functions need to be reimplemented to read from flat files
// =============================================================================

void block_chain::for_each_transaction(size_t from, size_t to, bool witness, for_each_tx_handler const& handler) const {
    // LMDB block storage removed - blocks now in flat files
    (void)from;
    (void)to;
    (void)witness;
    handler(error::not_found, 0, domain::chain::transaction{});
}

void block_chain::for_each_transaction_non_coinbase(size_t from, size_t to, bool witness, for_each_tx_handler const& handler) const {
    // LMDB block storage removed - blocks now in flat files
    (void)from;
    (void)to;
    (void)witness;
    handler(error::not_found, 0, domain::chain::transaction{});
}

} // namespace kth::blockchain
