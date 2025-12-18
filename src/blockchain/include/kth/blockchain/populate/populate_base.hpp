// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_POPULATE_BASE_HPP
#define KTH_BLOCKCHAIN_POPULATE_BASE_HPP

#include <cstddef>
#include <cstdint>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

#include <asio/any_io_executor.hpp>

namespace kth::blockchain {

// Forward declaration
struct block_chain;

/// This class is NOT thread safe.
class KB_API populate_base {
protected:
    using executor_type = ::asio::any_io_executor;

    populate_base(executor_type executor, size_t threads, block_chain const& chain);

    void populate_duplicate(size_t maximum_height, domain::chain::transaction const& tx, bool require_confirmed) const;
    void populate_pooled(domain::chain::transaction const& tx, uint32_t forks) const;
    void populate_prevout(size_t maximum_height, domain::chain::output_point const& outpoint, bool require_confirmed) const;

    // Thread pool executor for parallel operations
    executor_type executor_;
    size_t threads_;

    // The store is protected by caller not invoking populate concurrently.
    block_chain const& chain_;
};

} // namespace kth::blockchain

#endif
