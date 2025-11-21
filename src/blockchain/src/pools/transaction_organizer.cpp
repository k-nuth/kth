// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/pools/transaction_organizer.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <utility>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/interface/safe_chain.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_transaction.hpp>
#include <kth/domain.hpp>

namespace kth::blockchain {

using namespace std::placeholders;

#define NAME "transaction_organizer"

// TODO(legacy): create priority pool at blockchain level and use in both organizers.

#if defined(KTH_WITH_MEMPOOL)
transaction_organizer::transaction_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings, mining::mempool& mp)
#else
transaction_organizer::transaction_organizer(prioritized_mutex& mutex, dispatcher& dispatch, threadpool& thread_pool, fast_chain& chain, settings const& settings)
#endif
    : fast_chain_(chain)
    , mutex_(mutex)
    , stopped_(true)
    , settings_(settings)
    , dispatch_(dispatch)
    , transaction_pool_(settings)

#if defined(KTH_WITH_MEMPOOL)
    , validator_(dispatch, fast_chain_, settings, mp)
#else
    , validator_(dispatch, fast_chain_, settings)
#endif

    , subscriber_(std::make_shared<transaction_subscriber>(thread_pool, NAME))
    , ds_proof_subscriber_(std::make_shared<ds_proof_subscriber>(thread_pool, NAME))


#if defined(KTH_WITH_MEMPOOL)
    , mempool_(mp)
#endif
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
    subscriber_->start();
    ds_proof_subscriber_->start();
    validator_.start();
    return true;
}

bool transaction_organizer::stop() {
    validator_.stop();
    subscriber_->stop();
    subscriber_->invoke(error::service_stopped, {});
    ds_proof_subscriber_->stop();
    ds_proof_subscriber_->invoke(error::service_stopped, {});
    stopped_ = true;
    return true;
}

// Validate Transaction sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::transaction_validate.
void transaction_organizer::transaction_validate(transaction_const_ptr tx, result_handler handler) const {
    auto const check_handler = std::bind(&transaction_organizer::validate_handle_check, this, _1, tx, handler);
    // Checks that are independent of chain state.
    validator_.check(tx, check_handler);
}

// private
void transaction_organizer::validate_handle_check(code const& ec, transaction_const_ptr tx, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    auto const accept_handler = std::bind(&transaction_organizer::validate_handle_accept, this, _1, tx, handler);
    // Checks that are dependent on chain state and prevouts.
    validator_.accept(tx, accept_handler);
}

// private
void transaction_organizer::validate_handle_accept(code const& ec, transaction_const_ptr tx, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    if (tx->fees() < price(tx)) {
        handler(error::insufficient_fee);
        return;
    }

    if (tx->is_dusty(settings_.minimum_output_satoshis)) {
        handler(error::dusty_transaction);
        return;
    }

    auto const connect_handler = std::bind(&transaction_organizer::validate_handle_connect, this, _1, tx, handler);

    // Checks that include script validation.
    validator_.connect(tx, connect_handler);
}

// private
void transaction_organizer::validate_handle_connect(code const& ec, transaction_const_ptr tx, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    handler(error::success);
    return;
}

// DSProof Organize sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::organize.
void transaction_organizer::organize(double_spend_proof_const_ptr ds_proof, result_handler handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    if (stopped()) {
        mutex_.unlock_low_priority();
        handler(error::service_stopped);
        return;
    }

    ds_proofs_.try_emplace(hash(*ds_proof), ds_proof);

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    // This gets picked up by node DSProof-out protocol for announcement to peers.
    notify_ds_proof(ds_proof);

    // Invoke caller handler outside of critical section.
    handler(error::success);
}

// Transaction Organize sequence.
//-----------------------------------------------------------------------------

// This is called from blockchain::organize.
void transaction_organizer::organize(transaction_const_ptr tx, result_handler handler) {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    if (stopped()) {
        mutex_.unlock_low_priority();
        handler(error::service_stopped);
        return;
    }

    // Reset the reusable promise.
    resume_ = std::promise<code>();

    result_handler const complete = std::bind(&transaction_organizer::signal_completion, this, _1);
    auto const check_handler = std::bind(&transaction_organizer::handle_check, this, _1, tx, complete);

    // Checks that are independent of chain state.
    validator_.check(tx, check_handler);

    // Wait on completion signal.
    // This is necessary in order to continue on a non-priority thread.
    // If we do not wait on the original thread there may be none left.
    auto ec = resume_.get_future().get();

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    // Invoke caller handler outside of critical section.
    handler(ec);
}

// private
void transaction_organizer::signal_completion(code const& ec) {
    // This must be protected so that it is properly cleared.
    // Signal completion, which results in original handler invoke with code.
    resume_.set_value(ec);
}

// Verify sub-sequence.
//-----------------------------------------------------------------------------

// private
void transaction_organizer::handle_check(code const& ec, transaction_const_ptr tx, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    auto const accept_handler = std::bind(&transaction_organizer::handle_accept, this, _1, tx, handler);

    // Checks that are dependent on chain state and prevouts.
    validator_.accept(tx, accept_handler);
}

// private
void transaction_organizer::handle_accept(code const& ec, transaction_const_ptr tx, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    if (tx->fees() < price(tx)) {
        handler(error::insufficient_fee);
        return;
    }

    if (tx->is_dusty(settings_.minimum_output_satoshis)) {
        handler(error::dusty_transaction);
        return;
    }

    auto const connect_handler = std::bind(&transaction_organizer::handle_connect, this, _1, tx, handler);

    // Checks that include script validation.
    validator_.connect(tx, connect_handler);
}

// private
void transaction_organizer::handle_connect(code const& ec, transaction_const_ptr tx, result_handler handler) {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    // TODO: create a simulated validation path that does not block others.
    if (tx->validation.simulate) {
        handler(error::success);
        return;
    }

#if defined(KTH_WITH_MEMPOOL)
    auto res = mempool_.add(*tx);
    if (res == error::double_spend_mempool || res == error::double_spend_blockchain) {
        handler(res);
        return;
    }
    // spdlog::info("[blockchain] Transaction {} added to mempool.", encode_hash(tx->hash()));
#endif

#if ! defined(KTH_DB_READONLY)
    auto const pushed_handler = std::bind(&transaction_organizer::handle_pushed, this, _1, tx, handler);
    //#########################################################################
    fast_chain_.push(tx, dispatch_, pushed_handler);
    //#########################################################################
#endif
}

#if ! defined(KTH_DB_READONLY)
// private
void transaction_organizer::handle_pushed(code const& ec, transaction_const_ptr tx, result_handler handler) {
    if (ec) {
        spdlog::critical("[blockchain] Failure writing transaction to store, is now corrupted: {}", ec.message());
        handler(ec);
        return;
    }

    // This gets picked up by node tx-out protocol for announcement to peers.
    notify(tx);

    handler(error::success);
}
#endif // ! defined(KTH_DB_READONLY)

// Subscription.
//-----------------------------------------------------------------------------

// private
void transaction_organizer::notify(transaction_const_ptr tx) {
    // This invokes handlers within the criticial section (deadlock risk).
    subscriber_->invoke(error::success, tx);
}

void transaction_organizer::notify_ds_proof(double_spend_proof_const_ptr tx) {
    // This invokes handlers within the criticial section (deadlock risk).
    ds_proof_subscriber_->invoke(error::success, tx);
}

void transaction_organizer::subscribe(transaction_handler&& handler) {
    subscriber_->subscribe(std::move(handler), error::service_stopped, {});
}

void transaction_organizer::subscribe_ds_proof(ds_proof_handler&& handler) {
    ds_proof_subscriber_->subscribe(std::move(handler), error::service_stopped, {});
}

void transaction_organizer::unsubscribe() {
    subscriber_->relay(error::success, {});
}

void transaction_organizer::unsubscribe_ds_proof() {
    ds_proof_subscriber_->relay(error::success, {});
}

// Queries.
//-----------------------------------------------------------------------------

void transaction_organizer::fetch_template(merkle_block_fetch_handler handler) const {
    transaction_pool_.fetch_template(handler);
}

void transaction_organizer::fetch_mempool(size_t maximum, inventory_fetch_handler handler) const {
    transaction_pool_.fetch_mempool(maximum, handler);
}

void transaction_organizer::fetch_ds_proof(hash_digest const& hash, ds_proof_fetch_handler handler) const {
    // Critical Section
    ///////////////////////////////////////////////////////////////////////////
    mutex_.lock_low_priority();

    auto it = ds_proofs_.find(hash);
    if (it == ds_proofs_.end()) {
        mutex_.unlock_low_priority();
        handler(error::not_found, nullptr);
        return;
    }

    auto const res = it->second;

    mutex_.unlock_low_priority();
    ///////////////////////////////////////////////////////////////////////////

    // Invoke caller handler outside of critical section.
    handler(error::success, res);
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
    auto sigop = sigop_fee > 0 ? sigop_fee * tx->signature_operations() : 0;

    // Require at least one satoshi per tx if there are any fees configured.
    return std::max(uint64_t(1), static_cast<uint64_t>(byte + sigop));
}

} // namespace kth::blockchain