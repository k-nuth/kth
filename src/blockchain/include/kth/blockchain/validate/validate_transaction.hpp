// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_HPP
#define KTH_BLOCKCHAIN_VALIDATE_TRANSACTION_HPP

#include <atomic>
#include <cstddef>
#include <expected>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/populate/populate_transaction.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/domain.hpp>
#include <kth/infrastructure/handlers.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>


namespace kth::blockchain {

using kth::awaitable_expected;

// Forward declaration
struct block_chain;

/// This class is NOT thread safe.
struct KB_API validate_transaction {
    using executor_type = ::asio::any_io_executor;

    validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings);

    void start();
    void stop();

    [[nodiscard]]
    ::asio::awaitable<code> check(transaction_const_ptr tx) const;

    [[nodiscard]]
    ::asio::awaitable<code> accept(transaction_const_ptr tx) const;

    // Runs script validation across all inputs; on success yields the tx's
    // total sigchecks (BCH), which the organizer caches in the mempool entry.
    [[nodiscard]]
    awaitable_expected<size_t> connect(transaction_const_ptr tx) const;

protected:
    inline
    bool stopped() const {
        return stopped_;
    }

private:
    // Yields this bucket's sigchecks total, or the first script/validation error.
    std::expected<size_t, code> connect_inputs_sync(transaction_const_ptr tx, domain::script_flags_t flags, size_t bucket, size_t buckets) const;

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
