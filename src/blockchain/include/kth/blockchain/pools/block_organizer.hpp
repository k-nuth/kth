// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_BLOCK_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_BLOCK_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <future>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_block.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/utility/prioritized_mutex.hpp>

#include <kth/infrastructure/utility/threadpool.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/infrastructure/utility/broadcaster.hpp>

namespace kth::blockchain {

// Forward declaration
struct block_chain;

/// This class is thread safe.
/// Organises blocks via the block pool to the blockchain.
struct KB_API block_organizer {
    using executor_type = ::asio::any_io_executor;
    using ptr = std::shared_ptr<block_organizer>;
    using block_handler = std::function<bool(code, size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr)>;
    using block_broadcaster = broadcaster<size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr>;

    /// Construct an instance.
#if defined(KTH_WITH_MEMPOOL)
    block_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool& mp);
#else
    block_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions);
#endif

    bool start();
    bool stop();

    /// Organize a block - coroutine version
    /// @param headers_pre_validated If true, skip header validation (for headers-first sync)
    [[nodiscard]]
    ::asio::awaitable<code> organize(block_const_ptr block, bool headers_pre_validated = false);

    [[nodiscard]]
    block_broadcaster::channel_ptr subscribe();
    void unsubscribe(block_broadcaster::channel_ptr const& channel);

    /// Remove all message vectors that match block hashes.
    void filter(get_data_ptr message) const;

protected:
    bool stopped() const;

private:
    // Utility.
    bool set_branch_height(branch::ptr branch);

#if ! defined(KTH_DB_READONLY)
    ::asio::awaitable<code> handle_reorganized(branch::const_ptr branch, block_const_ptr_list_ptr outgoing);
#endif

#if defined(KTH_WITH_MEMPOOL)
    void populate_prevout_1(branch::const_ptr branch, domain::chain::output_point const& outpoint, bool require_confirmed) const;
    void populate_prevout_2(branch::const_ptr branch, domain::chain::output_point const& outpoint, local_utxo_set_t const& branch_utxo) const;
    void populate_transaction_inputs(branch::const_ptr branch, domain::chain::input::list const& inputs, local_utxo_set_t const& branch_utxo) const;
    void populate_transactions(branch::const_ptr branch, domain::chain::block const& block, local_utxo_set_t const& branch_utxo) const;
    void organize_mempool(branch::const_ptr branch, block_const_ptr_list_const_ptr const& incoming_blocks, block_const_ptr_list_ptr const& outgoing_blocks);
#endif

    bool is_branch_double_spend(branch::ptr const& branch) const;

    // Subscription.
    void notify(size_t branch_height, block_const_ptr_list_const_ptr branch, block_const_ptr_list_const_ptr original);

    // This must be protected by the implementation.
    block_chain& chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    executor_type executor_;
    size_t threads_;
    block_pool block_pool_;
    validate_block validator_;
    block_broadcaster broadcaster_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool& mempool_;
#endif
};

} // namespace kth::blockchain

#endif
