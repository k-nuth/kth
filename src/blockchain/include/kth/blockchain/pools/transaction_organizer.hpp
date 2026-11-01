// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP
#define KTH_BLOCKCHAIN_TRANSACTION_ORGANIZER_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/pools/mempool.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>
#include <kth/domain.hpp>

#include <kth/infrastructure/handlers.hpp>
#include <kth/infrastructure/utility/prioritized_mutex.hpp>
#include <kth/infrastructure/utility/threadpool.hpp>

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>

#include <kth/infrastructure/utility/broadcaster.hpp>

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

    transaction_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings);

    bool start();
    bool stop();

    [[nodiscard]]
    ::asio::awaitable<code> organize(transaction_const_ptr tx);

    // As above, for a transaction first seen at `first_seen` (unix seconds):
    // one reloaded from mempool.dat keeps its age, so expiry still counts from
    // when it was first seen.
    [[nodiscard]]
    ::asio::awaitable<code> organize(transaction_const_ptr tx, uint64_t first_seen);

    [[nodiscard]]
    ::asio::awaitable<code> organize(double_spend_proof_const_ptr ds_proof);

    // The fee per byte a transaction must pay now: the configured floor, or
    // more for a while after a full mempool evicted transactions.
    [[nodiscard]]
    double minimum_byte_fee() const;

    // That floor in satoshis per 1000 bytes, to the nearest satoshi: what peers
    // are told in fee_filter. A hint for them; admission checks the exact floor.
    [[nodiscard]]
    uint64_t fee_filter() const;

    // The clock (unix seconds) the rising floor and mempool expiry read.
    // Replaceable so tests can let hours pass.
    [[nodiscard]]
    uint64_t now() const;
    void set_clock(std::function<uint64_t()> clock);

    [[nodiscard]]
    ::asio::awaitable<code> transaction_validate(transaction_const_ptr tx) const;

    [[nodiscard]]
    transaction_broadcaster::channel_ptr subscribe();
    [[nodiscard]]
    ds_proof_broadcaster::channel_ptr subscribe_ds_proof();
    void unsubscribe(transaction_broadcaster::channel_ptr const& channel);
    void unsubscribe_ds_proof(ds_proof_broadcaster::channel_ptr const& channel);

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

    // After a full mempool evicted transactions up to `evicted`, the floor
    // becomes that rate plus the configured floor, so they cannot come straight
    // back at the price that just lost.
    void raise_floor(fee_rate evicted);

    // The floor at `now`; floor_mutex_ held.
    double floor_at(uint64_t now) const;

    // This must be protected by the implementation.
    block_chain& chain_;

    // These are thread safe.
    prioritized_mutex& mutex_;
    std::atomic<bool> stopped_;
    settings const& settings_;
    executor_type executor_;
    size_t threads_;
    validate_transaction validator_;
    transaction_broadcaster broadcaster_;
    ds_proof_broadcaster ds_proof_broadcaster_;

    std::unordered_map<hash_digest, double_spend_proof_const_ptr> ds_proofs_;

    // The raised floor (satoshis per byte) and when it was raised. Its excess
    // over the configured floor halves every 12 hours.
    mutable std::mutex floor_mutex_;
    double raised_floor_ = 0.0;
    uint64_t raised_at_ = 0;
    std::function<uint64_t()> clock_;
};

} // namespace kth::blockchain

#endif
