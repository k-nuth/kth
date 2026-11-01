// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/transaction_organizer.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <utility>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>
#include <kth/domain.hpp>

#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>

namespace kth::blockchain {

using namespace std::placeholders;

#define NAME "transaction_organizer"

// TODO(legacy): create priority pool at blockchain level and use in both organizers.

transaction_organizer::transaction_organizer(prioritized_mutex& mutex, executor_type executor, size_t threads, threadpool& thread_pool, block_chain& chain, settings const& settings)
    : chain_(chain)
    , mutex_(mutex)
    , stopped_(true)
    , settings_(settings)
    , executor_(std::move(executor))
    , threads_(threads)
    , validator_(executor_, threads_, chain_, settings)
    , broadcaster_(executor_)
    , ds_proof_broadcaster_(executor_)
    , clock_([] {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    })
{}

// Properties.
//-----------------------------------------------------------------------------

bool transaction_organizer::stopped() const {
    return stopped_;
}

// Start/stop sequences.
//-----------------------------------------------------------------------------

bool transaction_organizer::start() {
    stopped_ = false;
    broadcaster_.start();
    ds_proof_broadcaster_.start();
    validator_.start();
    return true;
}

bool transaction_organizer::stop() {
    validator_.stop();
    broadcaster_.stop();
    ds_proof_broadcaster_.stop();
    stopped_ = true;
    return true;
}

// Validate Transaction sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::transaction_validate.
::asio::awaitable<code> transaction_organizer::transaction_validate(transaction_const_ptr tx) const {
    // Checks that are independent of chain state.
    auto ec = co_await validator_.check(tx);

    if (stopped()) {
        co_return error::service_stopped;
    }

    if (ec) {
        co_return ec;
    }

    // Checks that are dependent on chain state and prevouts.
    // NOTE: this path (c-api single-tx validate) runs lock-free, so the store
    // entry that validator_.accept creates is written off the validation mutex.
    // That is safe only because this path is not run concurrently with block or
    // mempool organization (same pre-existing assumption as the accept-time
    // insert). Erase on every exit after the entry was created so the store does
    // not accumulate one entry per validated tx (idempotent if none was made).
    ec = co_await validator_.accept(tx);

    if (stopped()) {
        chain_.transaction_validations().erase(tx->hash());
        co_return error::service_stopped;
    }

    if (ec) {
        chain_.transaction_validations().erase(tx->hash());
        co_return ec;
    }

    if (tx->fees() < price(tx)) {
        chain_.transaction_validations().erase(tx->hash());
        co_return error::insufficient_fee;
    }

#if !defined(KTH_CURRENCY_BCH)
    if ( ! settings_.accept_non_standard_transactions &&
         tx->is_dusty(settings_.minimum_output_satoshis)) {
        chain_.transaction_validations().erase(tx->hash());
        co_return error::dusty_transaction;
    }
#endif

    // Checks that include script validation.
    auto const connected = co_await validator_.connect(tx);

    chain_.transaction_validations().erase(tx->hash());

    if (stopped()) {
        co_return error::service_stopped;
    }

    if (!connected) co_return connected.error();
    co_return validator_.check_standardness(tx);
}

// DSProof Organize sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::organize.
::asio::awaitable<code> transaction_organizer::organize(double_spend_proof_const_ptr ds_proof) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    if (stopped()) {
        mutex_.unlock_low_priority();
        co_return error::service_stopped;
    }

    ds_proofs_.try_emplace(hash(*ds_proof), ds_proof);

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    // This gets picked up by node DSProof-out protocol for announcement to peers.
    notify_ds_proof(ds_proof);

    co_return error::success;
}

// Transaction Organize sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::organize.
::asio::awaitable<code> transaction_organizer::organize(transaction_const_ptr tx) {
    co_return co_await organize(std::move(tx), now());
}

::asio::awaitable<code> transaction_organizer::organize(transaction_const_ptr tx,
    uint64_t first_seen) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    if (stopped()) {
        mutex_.unlock_low_priority();
        co_return error::service_stopped;
    }

    // Checks that are independent of chain state.
    auto ec = co_await validator_.check(tx);

    if (stopped()) {
        mutex_.unlock_low_priority();
        co_return error::service_stopped;
    }

    if (ec) {
        mutex_.unlock_low_priority();
        co_return ec;
    }

    // The tx-validation store entry created during validator_.accept below is
    // in-flight validation scratch: nothing reads it once this coroutine
    // returns (a mempool tx that is later mined has its entry recreated by the
    // block populator, and mempool eviction — including mempool_v2's internal
    // has_room_for drop — never needs it). So erase it on every exit after it
    // was created. All erases run under the low-priority validation mutex.
    auto const erase_tx_validation = [this, tx] {
        chain_.transaction_validations().erase(tx->hash());
    };

    // Checks that are dependent on chain state and prevouts.
    ec = co_await validator_.accept(tx);

    if (stopped()) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return error::service_stopped;
    }

    if (ec) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return ec;
    }

    if (tx->fees() < price(tx)) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return error::insufficient_fee;
    }

#if !defined(KTH_CURRENCY_BCH)
    if ( ! settings_.accept_non_standard_transactions &&
         tx->is_dusty(settings_.minimum_output_satoshis)) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return error::dusty_transaction;
    }
#endif

    // Checks that include script validation; on success yields the tx sigchecks.
    auto const connected = co_await validator_.connect(tx);

    if (stopped()) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return error::service_stopped;
    }

    if ( ! connected) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return connected.error();
    }

    // Only local Knuth policy can reject after consensus has succeeded.
    ec = validator_.check_standardness(tx);
    if (ec) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return ec;
    }

    // Validation is over and nothing below reads the entry, so drop it here
    // rather than repeating the erase down each remaining exit.
    erase_tx_validation();

    //#########################################################################
    // Admit to the mempool. The tx is already validated (accept / fee / dust /
    // connect above); this only records it. add() enforces first-seen: it
    // returns false on a double-spend conflict with a pooled tx (or a duplicate).
    auto& pool = chain_.mempool_ref();
    auto const admitted = pool.add(mempool_entry{
        tx,
        tx->fees(),
        static_cast<uint32_t>(tx->serialized_size(true)),
        static_cast<uint32_t>(*connected),
        first_seen});
    if ( ! admitted) {
        mutex_.unlock_low_priority();
        co_return error::double_spend_mempool;
    }

    // A full pool makes room by evicting the lowest fee per byte. When that is
    // this transaction it is refused as paying too little; either way the
    // floor rises so what was evicted cannot come straight back.
    if (settings_.mempool_max_bytes > 0) {
        if (auto const evicted = pool.trim_to_size(settings_.mempool_max_bytes)) {
            raise_floor(*evicted);
            if ( ! pool.contains(tx->hash())) {
                mutex_.unlock_low_priority();
                co_return error::insufficient_fee;
            }
        }
    }
    //#########################################################################

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    // This gets picked up by node tx-out protocol for announcement to peers.
    notify(tx);

    co_return error::success;
}

// Subscription.
//-----------------------------------------------------------------------------

// private
void transaction_organizer::notify(transaction_const_ptr tx) {
    broadcaster_.publish(tx);
}

void transaction_organizer::notify_ds_proof(double_spend_proof_const_ptr ds_proof) {
    ds_proof_broadcaster_.publish(ds_proof);
}

transaction_organizer::transaction_broadcaster::channel_ptr transaction_organizer::subscribe() {
    return broadcaster_.subscribe();
}

transaction_organizer::ds_proof_broadcaster::channel_ptr transaction_organizer::subscribe_ds_proof() {
    return ds_proof_broadcaster_.subscribe();
}

void transaction_organizer::unsubscribe(transaction_broadcaster::channel_ptr const& channel) {
    broadcaster_.unsubscribe(channel);
}

void transaction_organizer::unsubscribe_ds_proof(ds_proof_broadcaster::channel_ptr const& channel) {
    ds_proof_broadcaster_.unsubscribe(channel);
}

// Queries.
//-----------------------------------------------------------------------------

awaitable_expected<inventory_ptr>
transaction_organizer::fetch_mempool(size_t maximum) const {
    // TODO(mempool): serve the P2P `mempool` message from the new mempool.
    // The old transaction_pool stub was removed; wire this to the rebuilt
    // mempool once it lands.
    co_return std::unexpected(error::not_implemented);
}

awaitable_expected<double_spend_proof_const_ptr>
transaction_organizer::fetch_ds_proof(hash_digest const& hash) const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    auto it = ds_proofs_.find(hash);
    if (it == ds_proofs_.end()) {
        mutex_.unlock_low_priority();
        co_return std::unexpected(error::not_found);
    }

    auto const res = it->second;

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    co_return res;
}

// Utility.
//-----------------------------------------------------------------------------

uint64_t transaction_organizer::price(transaction_const_ptr tx) const {
    auto const byte_fee = minimum_byte_fee();
    auto const sigop_fee = settings_.sigop_fee_satoshis;

    // Guard against summing signed values by testing independently.
    if (byte_fee == 0.0 && sigop_fee == 0.0f) return 0;

    // TODO: this is a second pass on size and sigops, implement cache.
    // This at least prevents uncached calls when zero fee is configured.
    auto byte = byte_fee > 0 ? byte_fee * tx->serialized_size(true) : 0;
    // BCH: P2SH always active, no segwit.
    auto sigop = sigop_fee > 0 ? sigop_fee * tx->signature_operations(true /*bip16*/, false /*bip141*/) : 0;

    // Require at least one satoshi per tx if there are any fees configured.
    return std::max(uint64_t(1), uint64_t(byte + sigop));
}

// The excess of a raised floor over the configured one halves this often.
constexpr uint64_t floor_half_life_seconds = 12 * 60 * 60;

uint64_t transaction_organizer::now() const {
    return clock_();
}

void transaction_organizer::set_clock(std::function<uint64_t()> clock) {
    clock_ = std::move(clock);
}

double transaction_organizer::floor_at(uint64_t now) const {
    double const configured = settings_.byte_fee_satoshis;
    if (raised_floor_ <= configured) {
        return configured;
    }
    auto const elapsed = now > raised_at_ ? double(now - raised_at_) : 0.0;
    auto const excess = (raised_floor_ - configured)
        * std::pow(0.5, elapsed / double(floor_half_life_seconds));

    // Once the excess is under half the configured floor it is gone: what is
    // left is too small to keep the pool from refilling at the configured price.
    if (excess < configured / 2) {
        return configured;
    }
    return configured + excess;
}

double transaction_organizer::minimum_byte_fee() const {
    std::lock_guard const lock(floor_mutex_);
    return floor_at(now());
}

uint64_t transaction_organizer::fee_filter() const {
    return static_cast<uint64_t>(std::llround(minimum_byte_fee() * 1000.0));
}

void transaction_organizer::raise_floor(fee_rate evicted) {
    double const rate = double(evicted.fee) / double(evicted.size);
    auto const at = now();
    std::lock_guard const lock(floor_mutex_);
    raised_floor_ = std::max(floor_at(at), rate + settings_.byte_fee_satoshis);
    raised_at_ = at;
}

} // namespace kth::blockchain
