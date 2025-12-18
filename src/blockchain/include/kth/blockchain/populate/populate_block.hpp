// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POPULATE_BLOCK_HPP
#define KTH_BLOCKCHAIN_POPULATE_BLOCK_HPP

#include <cstddef>

#include <kth/database.hpp>
#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/populate/populate_base.hpp>
#include <kth/domain.hpp>

#include <asio/awaitable.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

// Forward declaration (already in populate_base.hpp, but explicit for clarity)
struct block_chain;

/// This class is NOT thread safe.
struct KB_API populate_block : populate_base {
public:
    using utxo_pool_t = database::internal_database::utxo_pool_t;

#if defined(KTH_WITH_MEMPOOL)
    populate_block(executor_type executor, size_t threads, block_chain const& chain, bool relay_transactions, mining::mempool const& mp);
#else
    populate_block(executor_type executor, size_t threads, block_chain const& chain, bool relay_transactions);
#endif

    /// Populate validation state for the top block.
    [[nodiscard]]
    ::asio::awaitable<code> populate(branch::const_ptr branch) const;

protected:
    using branch_ptr = branch::const_ptr;

    void populate_coinbase(branch::const_ptr branch, block_const_ptr block) const;

    utxo_pool_t get_reorg_subset_conditionally(size_t first_height, size_t& out_chain_top) const;
    void populate_from_reorg_subset(domain::chain::output_point const& outpoint, utxo_pool_t const& reorg_subset) const;
    void populate_transaction_inputs(branch::const_ptr branch, domain::chain::input::list const& inputs, size_t bucket, size_t buckets, size_t input_position, local_utxo_set_t const& branch_utxo, size_t first_height, size_t chain_top, utxo_pool_t const& reorg_subset) const;

#if defined(KTH_WITH_MEMPOOL)
    code populate_transactions_sync(branch::const_ptr branch, size_t bucket, size_t buckets, local_utxo_set_t const& branch_utxo, mining::mempool::hash_index_t const& validated_txs) const;
#else
    code populate_transactions_sync(branch::const_ptr branch, size_t bucket, size_t buckets, local_utxo_set_t const& branch_utxo) const;
#endif

    void populate_prevout(branch_ptr branch, domain::chain::output_point const& outpoint, local_utxo_set_t const& branch_utxo) const;

private:
    bool const relay_transactions_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool const& mempool_;
#endif

};

} // namespace kth::blockchain

#endif
