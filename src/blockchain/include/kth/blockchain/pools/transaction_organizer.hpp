// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/blockchain/pools/transaction_pool.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/utility/prioritized_mutex.hpp>
#include <kth/infrastructure/utility/resubscriber.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>

#endif

namespace kth::blockchain {

/// This class is thread safe.
/// Organises transactions via the transaction pool to the blockchain.
struct KB_API transaction_organizer {
    using result_handler = handle0;
    using ptr = std::shared_ptr<transaction_organizer>;
    using transaction_handler = safe_chain::transaction_handler;
    using ds_proof_handler = safe_chain::ds_proof_handler;
    using inventory_fetch_handler = safe_chain::inventory_fetch_handler;
    using merkle_block_fetch_handler = safe_chain::merkle_block_fetch_handler;
    using ds_proof_fetch_handler = safe_chain::ds_proof_fetch_handler;
    using transaction_subscriber = resubscriber<code, transaction_const_ptr>;
    using ds_proof_subscriber = resubscriber<code, double_spend_proof_const_ptr>;

    /// Construct an instance.

#if defined(KTH_WITH_MEMPOOL)
    transaction_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, mining::mempool& mp);
#else
    transaction_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings);
#endif

    bool start();
    bool stop();

    void organize(transaction_const_ptr tx, result_handler handler);
    void organize(double_spend_proof_const_ptr ds_proof, result_handler handler);

    void transaction_validate(transaction_const_ptr tx, result_handler handler) const;

    void subscribe(transaction_handler&& handler);
    void subscribe_ds_proof(ds_proof_handler&& handler);
    void unsubscribe();
    void unsubscribe_ds_proof();

    void fetch_template(merkle_block_fetch_handler) const;
    void fetch_mempool(size_t maximum, inventory_fetch_handler) const;
    void fetch_ds_proof(hash_digest const& hash, ds_proof_fetch_handler) const;

protected:
    bool stopped() const;
    uint64_t price(transaction_const_ptr tx) const;

private:
    // Verify sub-sequence.
    void handle_check(code const& ec, transaction_const_ptr tx, result_handler handler);
    void handle_accept(code const& ec, transaction_const_ptr tx, result_handler handler);
    void handle_connect(code const& ec, transaction_const_ptr tx, result_handler handler);

#if ! defined(KTH_DB_READONLY)
    void handle_pushed(code const& ec, transaction_const_ptr tx, result_handler handler);
#endif

    void signal_completion(code const& ec);

    void validate_handle_check(code const& ec, transaction_const_ptr tx, result_handler handler) const;
    void validate_handle_accept(code const& ec, transaction_const_ptr tx, result_handler handler) const;
    void validate_handle_connect(code const& ec, transaction_const_ptr tx, result_handler handler) const;

    // Subscription.
    void notify(transaction_const_ptr tx);
    void notify_ds_proof(double_spend_proof_const_ptr tx);

    // This must be protected by the implementation.
    fast_chain& fast_chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    std::promise<code> resume_;
    settings const& settings_;
    dispatcher& dispatch_;
    transaction_pool transaction_pool_;
    validate_transaction validator_;
    transaction_subscriber::ptr subscriber_;
    ds_proof_subscriber::ptr ds_proof_subscriber_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool& mempool_;
#endif

    std::unordered_map<hash_digest, double_spend_proof_const_ptr> ds_proofs_;
};

} // namespace kth::blockchain

#endif
