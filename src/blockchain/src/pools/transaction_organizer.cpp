// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/transaction_organizer.hpp>

#include <algorithm>
#include <cstddef>
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

    if (tx->is_dusty(settings_.minimum_output_satoshis)) {
        chain_.transaction_validations().erase(tx->hash());
        co_return error::dusty_transaction;
    }

    // Checks that include script validation.
    ec = co_await validator_.connect(tx);

    chain_.transaction_validations().erase(tx->hash());

    if (stopped()) {
        co_return error::service_stopped;
    }

    co_return ec;
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

    if (tx->is_dusty(settings_.minimum_output_satoshis)) {
        erase_tx_validation();
        mutex_.unlock_low_priority();
        co_return error::dusty_transaction;
    }

    // Checks that include script validation.
    ec = co_await validator_.connect(tx);

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

    // TODO: create a simulated validation path that does not block others.
    bool simulate = false;
    chain_.transaction_validations().visit(tx->hash(), [&](auto const& tv){ simulate = tv.simulate; });

    // Last read of the store entry; safe to drop it now for every path below.
    erase_tx_validation();

    if (simulate) {
        mutex_.unlock_low_priority();
        co_return error::success;
    }

#if ! defined(KTH_DB_READONLY)
    //#########################################################################
    auto push_ec = co_await chain_.push(tx);
    if (push_ec) {
        mutex_.unlock_low_priority();
        co_return push_ec;
    }
    //#########################################################################
#endif

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

awaitable_expected<std::pair<merkle_block_ptr, size_t>>
transaction_organizer::fetch_template() const {
    // TODO(mempool): block-template assembly (GBT) needs the new mempool.
    // The old transaction_pool stub was removed; wire this to the rebuilt
    // fee-prioritized mempool once it lands.
    co_return std::unexpected(error::not_implemented);
}

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
    auto const byte_fee = settings_.byte_fee_satoshis;
    auto const sigop_fee = settings_.sigop_fee_satoshis;

    // Guard against summing signed values by testing independently.
    if (byte_fee == 0.0f && sigop_fee == 0.0f) return 0;

    // TODO: this is a second pass on size and sigops, implement cache.
    // This at least prevents uncached calls when zero fee is configured.
    auto byte = byte_fee > 0 ? byte_fee * tx->serialized_size(true) : 0;
    // BCH: P2SH always active, no segwit.
    auto sigop = sigop_fee > 0 ? sigop_fee * tx->signature_operations(true /*bip16*/, false /*bip141*/) : 0;

    // Require at least one satoshi per tx if there are any fees configured.
    return std::max(uint64_t(1), uint64_t(byte + sigop));
}

} // namespace kth::blockchain
