// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/transaction_pool.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/utility/prioritized_mutex.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/infrastructure/utility/broadcaster.hpp>

#if defined(KTH_WITH_MEMPOOL)
#include <kth/mining/mempool.hpp>
#endif

namespace kth::blockchain {

using kth::awaitable_expected;

// Forward declaration
struct block_chain;

/// This class is thread safe.
/// Organises transactions via the transaction pool to the blockchain.
struct KB_API transaction_organizer {
    using executor_type = ::asio::any_io_executor;
    using ptr = std::shared_ptr<transaction_organizer>;
    using transaction_handler = std::function<bool(code, transaction_const_ptr)>;
    using ds_proof_handler = std::function<bool(code, double_spend_proof_const_ptr)>;
    using transaction_broadcaster = broadcaster<transaction_const_ptr>;
    using ds_proof_broadcaster = broadcaster<double_spend_proof_const_ptr>;

#if defined(KTH_WITH_MEMPOOL)
    transaction_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings, mining::mempool& mp);
#else
    transaction_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings);
#endif

    bool start();
    bool stop();

    [[nodiscard]]
    ::asio::awaitable<code> organize(transaction_const_ptr tx);

    [[nodiscard]]
    ::asio::awaitable<code> organize(double_spend_proof_const_ptr ds_proof);

    [[nodiscard]]
    ::asio::awaitable<code> transaction_validate(transaction_const_ptr tx) const;

    [[nodiscard]]
    transaction_broadcaster::channel_ptr subscribe();
    [[nodiscard]]
    ds_proof_broadcaster::channel_ptr subscribe_ds_proof();
    void unsubscribe(transaction_broadcaster::channel_ptr const& channel);
    void unsubscribe_ds_proof(ds_proof_broadcaster::channel_ptr const& channel);

    [[nodiscard]]
    awaitable_expected<std::pair<merkle_block_ptr, size_t>> fetch_template() const;

    [[nodiscard]]
    awaitable_expected<inventory_ptr> fetch_mempool(size_t maximum) const;

    [[nodiscard]]
    awaitable_expected<double_spend_proof_const_ptr> fetch_ds_proof(hash_digest const& hash) const;

protected:
    bool stopped() const;
    uint64_t price(transaction_const_ptr tx) const;

private:
    // Subscription.
    void notify(transaction_const_ptr tx);
    void notify_ds_proof(double_spend_proof_const_ptr tx);

    // This must be protected by the implementation.
    block_chain& chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    settings const& settings_;
    executor_type executor_;
    size_t threads_;
    transaction_pool transaction_pool_;
    validate_transaction validator_;
    transaction_broadcaster broadcaster_;
    ds_proof_broadcaster ds_proof_broadcaster_;

#if defined(KTH_WITH_MEMPOOL)
    mining::mempool& mempool_;
#endif

    std::unordered_map<hash_digest, double_spend_proof_const_ptr> ds_proofs_;
};

} // namespace kth::blockchain

#endif
