// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_VALIDATE_BLOCK_HPP
#define KTH_BLOCKCHAIN_VALIDATE_BLOCK_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/populate/populate_block.hpp>
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
struct KB_API validate_block {
    using executor_type = ::asio::any_io_executor;

#if defined(KTH_WITH_MEMPOOL)
    validate_block(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool const& mp);
#else
    validate_block(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions);
#endif

    void start();
    void stop();

    /// @param headers_pre_validated If true, skip header validation (for headers-first sync)
    [[nodiscard]]
    ::asio::awaitable<code> check(block_const_ptr block, bool headers_pre_validated = false) const;

    /// @param headers_pre_validated If true, use accept_body() to skip header validation
    [[nodiscard]]
    ::asio::awaitable<code> accept(branch::const_ptr branch, bool headers_pre_validated = false) const;

    [[nodiscard]]
    ::asio::awaitable<code> connect(branch::const_ptr branch) const;

    // =========================================================================
    // Static validation functions (pure, no side effects)
    // These replace the accept/connect methods that were in block_basis.
    // =========================================================================

    /// Validate block body against chain state (skip header validation).
    /// Validates: block size, transaction ordering, coinbase, finality.
    /// @param block The block to validate.
    /// @param state The chain state for context.
    /// @return error::success or validation error.
    [[nodiscard]]
    static code accept_block_body(
        domain::chain::block const& block,
        domain::chain::chain_state const& state);

protected:
    [[nodiscard]]
    bool stopped() const {
        return stopped_;
    }

    [[nodiscard]]
    float hit_rate() const;

private:
    using atomic_counter = std::atomic<size_t>;
    using atomic_counter_ptr = std::shared_ptr<atomic_counter>;

    static
    void dump(code const& ec, domain::chain::transaction const& tx, uint32_t input_index, uint32_t forks, size_t height);

    // Synchronous helpers for parallel execution (called within asio::post)
    [[nodiscard]]
    code check_block_bucket(block_const_ptr block, size_t bucket, size_t buckets) const;

    [[nodiscard]]
    code accept_transactions_bucket(block_const_ptr block, size_t bucket, size_t buckets, atomic_counter_ptr sigops, bool bip16, bool bip141) const;

    [[nodiscard]]
    code connect_inputs_bucket(block_const_ptr block, size_t bucket, size_t buckets) const;

    // These are thread safe.
    std::atomic<bool> stopped_;
    block_chain const& chain_;
    domain::config::network network_;
    executor_type executor_;
    size_t threads_;
    mutable atomic_counter hits_;
    mutable atomic_counter queries_;

    // Caller must not invoke accept/connect concurrently.
    populate_block block_populator_;
};

} // namespace kth::blockchain

#endif
