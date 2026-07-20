// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_transaction.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <utility>

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
using kd::script_flags_t;

#define NAME "validate_transaction"

// Database access is limited to: populator:
// spend: { spender }
// transaction: { exists, height, output }


validate_transaction::validate_transaction(executor_type executor, size_t threads, block_chain const& chain, settings const& settings)
  : stopped_(true),
    retarget_(settings.retarget),
    executor_(std::move(executor)),
    threads_(threads),

    transaction_populator_(executor_, threads_, chain),
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
    // NOTE: on the transaction_validate (c-api single-tx validate) path this
    // store write happens off the organizer mutex, but the tx is private to
    // the caller so no other worker touches this hash concurrently.
    {
        auto const state = chain_.chain_state();
        chain_.transaction_validations().mutate(tx->hash(), [&](auto& tv){ tv.state = state; });
        if ( ! state) {
            co_return error::transaction_validation_state_failed;
        }
    }

    auto const populate_ec = co_await transaction_populator_.populate(tx);

    if (stopped()) {
        co_return error::service_stopped;
    }

    if (populate_ec) {
        co_return populate_ec;
    }

    domain::chain::chain_state::ptr state_ptr;
    bool duplicate = false;
    chain_.transaction_validations().visit(tx->hash(), [&](auto const& tv){ state_ptr = tv.state; duplicate = tv.duplicate; });
    KTH_ASSERT(state_ptr);
    auto const& state = *state_ptr;

    // Run contextual tx checks.
    co_return tx->accept(
        state.enabled_flags(),
        state.height(),
        state.median_time_past(),
        state.is_lobachevski_enabled()
            ? state.dynamic_max_block_sigops()
            : static_max_block_sigops(state.network()),
        state.is_under_checkpoint(),
        true /*transaction_pool*/,
        duplicate
    );
}

// Connect sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, block state and perform script validation.

awaitable_expected<size_t> validate_transaction::connect(transaction_const_ptr tx) const {
    domain::chain::chain_state::ptr state_ptr;
    chain_.transaction_validations().visit(tx->hash(), [&](auto const& tv){ state_ptr = tv.state; });
    KTH_ASSERT(state_ptr);
    auto const flags = state_ptr->enabled_flags();
    auto const total_inputs = tx->inputs().size();

    // Return if there are no inputs to validate (will fail later).
    if (total_inputs == 0) {
        co_return 0u;
    }

    auto const buckets = std::min(threads_, total_inputs);
    KTH_ASSERT(buckets != 0);

    // Use a channel to collect each bucket's {sigchecks | error} result.
    using bucket_result = std::expected<size_t, code>;
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, bucket_result)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        ::asio::post(executor_, [this, tx, flags, bucket, buckets, channel]() {
            channel->try_send(std::error_code{}, connect_inputs_sync(tx, flags, bucket, buckets));
        });
    }

    // Wait for all results - return first error, or the summed sigchecks.
    code final_result = error::success;
    size_t total_sigchecks = 0;
    for (size_t i = 0; i < buckets; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            // Channel error - shouldn't happen
            co_return std::unexpected(error::operation_failed);
        }
        if ( ! result) {
            if (final_result == error::success) {
                final_result = result.error();
            }
        } else {
            total_sigchecks += *result;
        }
    }

    if (final_result != error::success) {
        co_return std::unexpected(final_result);
    }
    co_return total_sigchecks;
}

std::expected<size_t, code> validate_transaction::connect_inputs_sync(transaction_const_ptr tx, script_flags_t flags, size_t bucket, size_t buckets) const {
    KTH_ASSERT(bucket < buckets);

    size_t sigchecks = 0;
    auto const& inputs = tx->inputs();

    for (auto input_index = bucket; input_index < inputs.size(); input_index = ceiling_add(input_index, buckets)) {
        if (stopped()) {
            return std::unexpected(error::service_stopped);
        }

        auto const& prevout = inputs[input_index].previous_output();

        if ( ! prevout.validation.cache.is_valid()) {
            return std::unexpected(error::missing_previous_output);
        }

        auto res = validate_input::verify_script(*tx, input_index, flags);
        if (res.first != error::success) {
            return std::unexpected(res.first);
        }

#if defined(KTH_CURRENCY_BCH)
        sigchecks += res.second;
        if (sigchecks > max_tx_sigchecks) {
            return std::unexpected(error::transaction_sigchecks_limit);
        }
#endif
    }
    return sigchecks;
}

} // namespace kth::blockchain
