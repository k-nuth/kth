// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_transaction.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include <kth/blockchain/interface/fast_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_input.hpp>
#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <kth/infrastructure/utility/synchronizer.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::machine;
using namespace std::placeholders;

#define NAME "validate_transaction"

// Database access is limited to: populator:
// spend: { spender }
// transaction: { exists, height, output }


#if defined(KTH_WITH_MEMPOOL)
validate_transaction::validate_transaction(dispatcher& dispatch, fast_chain const& chain, settings const& settings, mining::mempool const& mp)
#else
validate_transaction::validate_transaction(dispatcher& dispatch, fast_chain const& chain, settings const& settings)
#endif
  : stopped_(true),
    retarget_(settings.retarget),
    dispatch_(dispatch),

#if defined(KTH_WITH_MEMPOOL)
    transaction_populator_(dispatch, chain, mp),
#else
    transaction_populator_(dispatch, chain),
#endif
    fast_chain_(chain)
{
}

// Start/stop sequences.
//-----------------------------------------------------------------------------

void validate_transaction::start()
{
    stopped_ = false;
}

void validate_transaction::stop()
{
    stopped_ = true;
}

// Check.
//-----------------------------------------------------------------------------
// These checks are context free.

void validate_transaction::check(transaction_const_ptr tx, result_handler handler) const {
    // Run context free checks.
    handler(tx->check(true, retarget_));
}

// Accept sequence.
//-----------------------------------------------------------------------------
// These checks require chain and tx state (net height and enabled forks).

void validate_transaction::accept(transaction_const_ptr tx, result_handler handler) const {
    // Populate chain state of the next block (tx pool).
    tx->validation.state = fast_chain_.chain_state();

    if ( ! tx->validation.state) {
        handler(error::transaction_validation_state_failed);
        return;
    }

    transaction_populator_.populate(tx, std::bind(&validate_transaction::handle_populated, this, _1, tx, handler));
}

void validate_transaction::handle_populated(code const& ec, transaction_const_ptr tx, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    KTH_ASSERT(tx->validation.state);

    // Run contextual tx checks.
    handler(tx->accept());
}

// Connect sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, block state and perform script validation.

void validate_transaction::connect(transaction_const_ptr tx, result_handler handler) const {
    KTH_ASSERT(tx->validation.state);
    auto const total_inputs = tx->inputs().size();

    // Return if there are no inputs to validate (will fail later).
    if (total_inputs == 0) {
        handler(error::success);
        return;
    }

    auto const buckets = std::min(dispatch_.size(), total_inputs);
    auto const join_handler = synchronize(handler, buckets, NAME "_validate");
    KTH_ASSERT(buckets != 0);

    // If the priority threadpool is shut down when this is called the handler
    // will never be invoked, resulting in a threadpool.join indefinite hang.
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        dispatch_.concurrent(&validate_transaction::connect_inputs, this, tx, bucket, buckets, join_handler);
    }
}

void validate_transaction::connect_inputs(transaction_const_ptr tx, size_t bucket, size_t buckets, result_handler handler) const {
    KTH_ASSERT(bucket < buckets);

#if defined(KTH_CURRENCY_BCH)
    size_t tx_sigchecks = 0;
#endif

    auto const forks = tx->validation.state->enabled_forks();
    auto const& inputs = tx->inputs();

    for (auto input_index = bucket; input_index < inputs.size(); input_index = ceiling_add(input_index, buckets)) {
        if (stopped()) {
            handler(error::service_stopped);
            return;
        }

        auto const& prevout = inputs[input_index].previous_output();

        if ( ! prevout.validation.cache.is_valid()) {
            handler(error::missing_previous_output);
            return;
        }

        auto res = validate_input::verify_script(*tx, input_index, forks);
        if (res.first != error::success) {
            handler(res.first);
            return;
        }

#if defined(KTH_CURRENCY_BCH)
        tx_sigchecks += res.second;
        if (tx_sigchecks > max_tx_sigchecks) {
            handler(error::transaction_sigchecks_limit);
            return;
        }
#endif
    }
    handler(error::success);
}

} // namespace kth::blockchain
