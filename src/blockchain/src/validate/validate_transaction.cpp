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

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/blockchain/settings.hpp>
#include <kth/blockchain/validate/validate_input.hpp>
#include <kth/domain.hpp>
#include <kth/domain/multi_crypto_support.hpp>

#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::machine;
using namespace std::placeholders;

#define NAME "validate_transaction"

// Database access is limited to: populator:
// spend: { spender }
// transaction: { exists, height, output }


#if defined(KTH_WITH_MEMPOOL)
validate_transaction::validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, mining::mempool const& mp)
#else
validate_transaction::validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings)
#endif
  : stopped_(true),
    retarget_(settings.retarget),
    executor_(std::move(executor)),
    threads_(threads),

#if defined(KTH_WITH_MEMPOOL)
    transaction_populator_(executor_, threads_, chain, mp),
#else
    transaction_populator_(executor_, threads_, chain),
#endif
    chain_(chain)
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

::asio::awaitable<code> validate_transaction::check(transaction_const_ptr tx) const {
    // Run context free checks.
    co_return tx->check(true, retarget_);
}

// Accept sequence.
//-----------------------------------------------------------------------------
// These checks require chain and tx state (net height and enabled forks).

::asio::awaitable<code> validate_transaction::accept(transaction_const_ptr tx) const {
    // Populate chain state of the next block (tx pool).
    tx->validation.state = chain_.chain_state();

    if ( ! tx->validation.state) {
        co_return error::transaction_validation_state_failed;
    }

    auto const populate_ec = co_await transaction_populator_.populate(tx);

    if (stopped()) {
        co_return error::service_stopped;
    }

    if (populate_ec) {
        co_return populate_ec;
    }

    KTH_ASSERT(tx->validation.state);

    // Run contextual tx checks.
    co_return tx->accept();
}

// Connect sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, block state and perform script validation.

::asio::awaitable<code> validate_transaction::connect(transaction_const_ptr tx) const {
    KTH_ASSERT(tx->validation.state);
    auto const total_inputs = tx->inputs().size();

    // Return if there are no inputs to validate (will fail later).
    if (total_inputs == 0) {
        co_return error::success;
    }

    auto const buckets = std::min(threads_, total_inputs);
    KTH_ASSERT(buckets != 0);

    // Use a channel to collect results from parallel tasks
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        ::asio::post(executor_, [this, tx, bucket, buckets, channel]() {
            auto result = connect_inputs_sync(tx, bucket, buckets);
            channel->try_send(std::error_code{}, result);
        });
    }

    // Wait for all results - return first error or success if all succeed
    code final_result = error::success;
    for (size_t i = 0; i < buckets; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            // Channel error - shouldn't happen
            co_return error::operation_failed;
        }
        if (result != error::success && final_result == error::success) {
            final_result = result;
        }
    }

    co_return final_result;
}

code validate_transaction::connect_inputs_sync(transaction_const_ptr tx, size_t bucket, size_t buckets) const {
    KTH_ASSERT(bucket < buckets);

#if defined(KTH_CURRENCY_BCH)
    size_t tx_sigchecks = 0;
#endif

    auto const forks = tx->validation.state->enabled_forks();
    auto const& inputs = tx->inputs();

    for (auto input_index = bucket; input_index < inputs.size(); input_index = ceiling_add(input_index, buckets)) {
        if (stopped()) {
            return error::service_stopped;
        }

        auto const& prevout = inputs[input_index].previous_output();

        if ( ! prevout.validation.cache.is_valid()) {
            return error::missing_previous_output;
        }

        auto res = validate_input::verify_script(*tx, input_index, forks);
        if (res.first != error::success) {
            return res.first;
        }

#if defined(KTH_CURRENCY_BCH)
        tx_sigchecks += res.second;
        if (tx_sigchecks > max_tx_sigchecks) {
            return error::transaction_sigchecks_limit;
        }
#endif
    }
    return error::success;
}

} // namespace kth::blockchain
