// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_block.hpp>

#include <algorithm>
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
using kd::script_flags_t;

// Database access is limited to: populator:
// spend: { spender }
// block: { bits, version, timestamp }
// transaction: { exists, height, output }

#if defined(KTH_WITH_MEMPOOL)
validate_block::validate_block(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool const& mp)
#else
validate_block::validate_block(executor_type executor, size_t threads, block_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions)
#endif
    : stopped_(true)
    , chain_(chain)
    , network_(network)
    , executor_(std::move(executor))
    , threads_(threads)
    , hits_(0)
    , queries_(0)
#if defined(KTH_WITH_MEMPOOL)
    , block_populator_(executor_, threads_, chain, relay_transactions, mp)
#else
    , block_populator_(executor_, threads_, chain, relay_transactions)
#endif
{}

// Start/stop sequences.
//-----------------------------------------------------------------------------

void validate_block::start() {
    stopped_ = false;
}

void validate_block::stop() {
    stopped_ = true;
}

// Check.
//-----------------------------------------------------------------------------
// These checks are context free.

::asio::awaitable<code> validate_block::check(block_const_ptr block) const {
    // The block hasn't been checked yet.
    if (block->transactions().empty()) {
        co_return error::success;
    }

    // TODO: make configurable for each parallel segment.
    // This one is more efficient with one thread than parallel.
    auto const threads = std::min(size_t(1), threads_);
    auto const count = block->transactions().size();
    auto const buckets = std::min(threads, count);
    KTH_ASSERT(buckets != 0);

    // Use a channel to collect results from parallel tasks
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        ::asio::post(executor_, [this, block, bucket, buckets, channel]() {
            auto result = check_block_bucket(block, bucket, buckets);
            channel->try_send(std::error_code{}, result);
        });
    }

    // Wait for all results
    code final_result = error::success;
    for (size_t i = 0; i < buckets; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            co_return error::operation_failed;
        }
        if (result != error::success && final_result == error::success) {
            final_result = result;
        }
    }

    if (final_result) {
        co_return final_result;
    }

    // Run context free checks, sets time internally.
    co_return block->check();
}

code validate_block::check_block_bucket(block_const_ptr block, size_t bucket, size_t buckets) const {
    if (stopped()) {
        return error::service_stopped;
    }

    auto const& txs = block->transactions();

    // Generate each tx hash (stored in tx cache).
    for (auto tx = bucket; tx < txs.size(); tx = ceiling_add(tx, buckets)) {
        txs[tx].hash();
    }

    return error::success;
}

// Accept sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, and block state if not under checkpoint.

::asio::awaitable<code> validate_block::accept(branch::const_ptr branch) const {
    auto const block = branch->top();
    KTH_ASSERT(block);

    // The block has no population timer, so set externally.
    block->validation.start_populate = asio::steady_clock::now();

    // Populate chain state for the next block.
    block->validation.state = chain_.chain_state(branch);

    if ( ! block->validation.state) {
        co_return error::block_validation_state_failed;
    }

    // Populate block state for the top block (others are valid).
    auto const populate_ec = co_await block_populator_.populate(branch);

    if (stopped()) {
        co_return error::service_stopped;
    }

    if (populate_ec) {
        co_return populate_ec;
    }

    auto const height = block->validation.state->height();

    // Run contextual block non-tx checks (sets start time).
    auto const error_code = block->accept(false);

    if (error_code) {
        co_return error_code;
    }

    auto const sigops = std::make_shared<atomic_counter>(0);
    auto const state = block->validation.state;
    KTH_ASSERT(state);
#if defined(KTH_CURRENCY_BCH)
    bool const bip141 = false;
#else
    auto const bip141 = state->is_enabled(domain::machine::rule_fork::bip141_rule);
#endif

    if (state->is_under_checkpoint()) {
        co_return error::success;
    }

    auto const count = block->transactions().size();
    auto const bip16 = state->is_enabled(domain::machine::rule_fork::bip16_rule);
    auto const buckets = std::min(threads_, count);
    KTH_ASSERT(buckets != 0);

    // Use a channel to collect results from parallel tasks
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        ::asio::post(executor_, [this, block, bucket, buckets, sigops, bip16, bip141, channel]() {
            auto result = accept_transactions_bucket(block, bucket, buckets, sigops, bip16, bip141);
            channel->try_send(std::error_code{}, result);
        });
    }

    // Wait for all results
    code accept_result = error::success;
    for (size_t i = 0; i < buckets; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            co_return error::operation_failed;
        }
        if (result != error::success && accept_result == error::success) {
            accept_result = result;
        }
    }

    if (accept_result) {
        co_return accept_result;
    }

    // Check sigops limit
#if defined(KTH_CURRENCY_BCH)
    if (block->validation.state->is_fermat_enabled()) {
        co_return error::success;
    }

    size_t allowed_sigops = get_allowed_sigops(block->serialized_size(1));
    auto const exceeded = *sigops > allowed_sigops;
#else
    auto const max_sigops = bip141 ? max_fast_sigops : get_allowed_sigops(block->serialized_size(1));
    auto const exceeded = *sigops > max_sigops;
#endif
    co_return exceeded ? error::block_embedded_sigop_limit : error::success;
}

code validate_block::accept_transactions_bucket(block_const_ptr block, size_t bucket, size_t buckets, atomic_counter_ptr sigops, bool bip16, bool bip141) const {
#if defined(KTH_CURRENCY_BCH)
    bip141 = false;
#endif
    if (stopped()) {
        return error::service_stopped;
    }

    code ec(error::success);
    auto const& state = *block->validation.state;
    auto const& txs = block->transactions();
    auto const count = txs.size();

    // Run contextual tx non-script checks (not in tx order).
    for (auto tx = bucket; tx < count && !ec; tx = ceiling_add(tx, buckets)) {
        auto const& transaction = txs[tx];
        if ( ! transaction.validation.validated) {
            ec = transaction.accept(state, false);
        }
        *sigops += transaction.signature_operations(bip16, bip141);
    }

    return ec;
}

// Connect sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, block state and perform script validation.

::asio::awaitable<code> validate_block::connect(branch::const_ptr branch) const {
    auto const block = branch->top();
    KTH_ASSERT(block && block->validation.state);

    // We are reimplementing connect, so must set timer externally.
    block->validation.start_connect = asio::steady_clock::now();

    if (block->validation.state->is_under_checkpoint()) {
        co_return error::success;
    }

    auto const non_coinbase_inputs = block->total_inputs(false);

    // Return if there are no non-coinbase inputs to validate.
    if (non_coinbase_inputs == 0) {
        co_return error::success;
    }

    // Reset statistics for each block (treat coinbase as cached).
    hits_ = 0;
    queries_ = 0;

    auto const buckets = std::min(threads_, non_coinbase_inputs);
    KTH_ASSERT(buckets != 0);

    // Use a channel to collect results from parallel tasks
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        ::asio::post(executor_, [this, block, bucket, buckets, channel]() {
            auto result = connect_inputs_bucket(block, bucket, buckets);
            channel->try_send(std::error_code{}, result);
        });
    }

    // Wait for all results
    code connect_result = error::success;
    for (size_t i = 0; i < buckets; ++i) {
        auto [ec, result] = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
        if (ec) {
            co_return error::operation_failed;
        }
        if (result != error::success && connect_result == error::success) {
            connect_result = result;
        }
    }

    block->validation.cache_efficiency = hit_rate();
    co_return connect_result;
}

code validate_block::connect_inputs_bucket(block_const_ptr block, size_t bucket, size_t buckets) const {
    KTH_ASSERT(bucket < buckets);
    code ec(error::success);
    auto const flags = block->validation.state->enabled_flags();
    auto const& txs = block->transactions();
    size_t position = 0;

#if defined(KTH_CURRENCY_BCH)
    size_t block_sigchecks = 0;
#endif

    // Must skip coinbase here as it is already accounted for.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {
        ++queries_;

        // The tx is pooled with current fork state so outputs are validated.
        if (tx->validation.current) {
            ++hits_;
            continue;
        }

        // The tx was validated before its insertion in the mempool
        if (tx->validation.validated) {
            ++hits_;
            continue;
        }

        size_t input_index;
        auto const& inputs = tx->inputs();

        for (input_index = 0; input_index < inputs.size(); ++input_index, ++position) {
            if (position % buckets != bucket) {
                continue;
            }

            if (stopped()) {
                return error::service_stopped;
            }

            auto const& prevout = inputs[input_index].previous_output();

            if ( ! prevout.validation.cache.is_valid()) {
                ec = error::missing_previous_output;
                break;
            }

            size_t sigchecks;
            std::tie(ec, sigchecks) = validate_input::verify_script(*tx, input_index, flags);
            if (ec != error::success) {
                break;
            }

#if defined(KTH_CURRENCY_BCH)
            block_sigchecks += sigchecks;
            if (block_sigchecks > block->validation.state->dynamic_max_block_sigchecks()) {
                ec = error::block_sigchecks_limit;
                break;
            }
#endif
        }

        if (ec) {
            auto const height = block->validation.state->height();
            dump(ec, *tx, input_index, flags, height);
            break;
        }
    }

    return ec;
}

// The tx pool cache hit rate.
float validate_block::hit_rate() const {
    // These values could overflow or divide by zero, but that's okay.
    return queries_ == 0 ? 0.0f : (hits_ * 1.0f / queries_);
}

// Utility.
//-----------------------------------------------------------------------------

void validate_block::dump(code const& ec, transaction const& tx, uint32_t input_index, script_flags_t flags, size_t height) {
    auto const& prevout = tx.inputs()[input_index].previous_output();
    auto const script = prevout.validation.cache.script().to_data(false);
    auto const hash = encode_hash(prevout.hash());
    auto const tx_hash = encode_hash(tx.hash());

    spdlog::debug("[blockchain] Verify failed [{}] : {}\n"
        " flags        : {}\n"
        " outpoint     : {}:{}\n"
        " script       : {}\n"
        " value        : {}\n"
        " inpoint      : {}:{}\n"
        " transaction  : {}",
        height, ec.message(), flags, hash, prevout.index(), encode_base16(script),
        prevout.validation.cache.value(), tx_hash, input_index, encode_base16(tx.to_data(true)));
}

} // namespace kth::blockchain
