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
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/blockchain/pools/block_pool.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_block.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/utility/prioritized_mutex.hpp>
#include <kth/infrastructure/utility/resubscriber.hpp>

namespace kth::blockchain {

/// This class is thread safe.
/// Organises blocks via the block pool to the blockchain.
struct KB_API block_organizer {
    using result_handler = handle0;
    using ptr = std::shared_ptr<block_organizer>;
    using reorganize_handler = safe_chain::reorganize_handler;
    using reorganize_subscriber = resubscriber<code, size_t, block_const_ptr_list_const_ptr, block_const_ptr_list_const_ptr>;

    /// Construct an instance.
#if defined(KTH_WITH_MEMPOOL)
    block_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool& mp);
#else
    block_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, domain::config::network network, bool relay_transactions);
#endif

    bool start();
    bool stop();

    void organize(block_const_ptr block, result_handler handler);
    void subscribe(reorganize_handler&& handler);
    void unsubscribe();

    /// Remove all message vectors that match block hashes.
    void filter(get_data_ptr message) const;

protected:
    bool stopped() const;

private:
    // Utility.
    bool set_branch_height(branch::ptr branch);

    // Verify sub-sequence.
    void handle_check(code const& ec, block_const_ptr block, result_handler handler);
    void handle_accept(code const& ec, branch::ptr branch, result_handler handler);
    void handle_connect(code const& ec, branch::ptr branch, result_handler handler);
    void organized(branch::ptr branch, result_handler handler);

#if ! defined(KTH_DB_READONLY)
    void handle_reorganized(code const& ec, branch::const_ptr branch, block_const_ptr_list_ptr outgoing, result_handler handler);
#endif

    void signal_completion(code const& ec);

#if defined(KTH_WITH_MEMPOOL)
    void populate_prevout_1(branch::const_ptr branch, domain::chain::output_point const& outpoint, bool require_confirmed) const;
    void populate_prevout_2(branch::const_ptr branch, domain::chain::output_point const& outpoint, local_utxo_set_t const& branch_utxo) const;
    void populate_transaction_inputs(branch::const_ptr branch, domain::chain::input::list const& inputs, local_utxo_set_t const& branch_utxo) const;
    void populate_transactions(branch::const_ptr branch, domain::chain::block const& block, local_utxo_set_t const& branch_utxo) const;
    // void organize_mempool(branch::const_ptr branch, block_const_ptr_list_const_ptr const& incoming_blocks, block_const_ptr_list_ptr const& outgoing_blocks, local_utxo_set_t const& branch_utxo);
    void organize_mempool(branch::const_ptr branch, block_const_ptr_list_const_ptr const& incoming_blocks, block_const_ptr_list_ptr const& outgoing_blocks);
#endif

    bool is_branch_double_spend(branch::ptr const& branch) const;

    // Subscription.
    void notify(size_t branch_height, block_const_ptr_list_const_ptr branch, block_const_ptr_list_const_ptr original);

    // This must be protected by the implementation.
    fast_chain& fast_chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    std::promise<code> resume_;
    dispatcher& dispatch_;
    block_pool block_pool_;
    validate_block validator_;
    reorganize_subscriber::ptr subscriber_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool& mempool_;
#endif
};

} // namespace kth::blockchain

#endif
