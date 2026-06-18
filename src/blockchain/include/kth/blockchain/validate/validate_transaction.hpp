// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_HPP
#define KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_HPP

#include <atomic>
#include <cstddef>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/populate/populate_transaction.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

// Forward declaration
struct block_chain;

/// This class is NOT thread safe.
struct KB_API validate_transaction {
    using executor_type = ::asio::any_io_executor;

#if defined(KTH_WITH_MEMPOOL)
    validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, mining::mempool const& mp);
#else
    validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings);
#endif

    void start();
    void stop();

    [[nodiscard]]
    ::asio::awaitable<code> check(transaction_const_ptr tx) const;

    [[nodiscard]]
    ::asio::awaitable<code> accept(transaction_const_ptr tx) const;

    [[nodiscard]]
    ::asio::awaitable<code> connect(transaction_const_ptr tx) const;

protected:
    inline
    bool stopped() const {
        return stopped_;
    }

private:
    code connect_inputs_sync(transaction_const_ptr tx, size_t bucket, size_t buckets) const;

    // These are thread safe.
    std::atomic<bool> stopped_;
    bool const retarget_;
    block_chain const& chain_;
    executor_type executor_;
    size_t threads_;

    // Caller must not invoke accept/connect concurrently.
    populate_transaction transaction_populator_;
};

} // namespace kth::blockchain

#endif
