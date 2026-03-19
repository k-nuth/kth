// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POPULATE_TRANSACTION_HPP
#define KTH_BLOCKCHAIN_POPULATE_TRANSACTION_HPP

#include <cstddef>

#include <kth/blockchain/define.hpp>
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
struct KB_API populate_transaction : populate_base {
public:

#if defined(KTH_WITH_MEMPOOL)
    populate_transaction(executor_type executor, size_t threads, block_chain const& chain, mining::mempool const& mp);
#else
    populate_transaction(executor_type executor, size_t threads, block_chain const& chain);
#endif

    /// Populate validation state for the transaction.
    [[nodiscard]]
    ::asio::awaitable<code> populate(transaction_const_ptr tx) const;

protected:
    code populate_inputs_sync(transaction_const_ptr tx, size_t chain_height, size_t bucket, size_t buckets) const;

private:
#if defined(KTH_WITH_MEMPOOL)
    mining::mempool const& mempool_;
#endif
};

} // namespace kth::blockchain

#endif
