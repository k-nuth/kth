// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/validate/validate_block.hpp>

#include <algorithm>
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

#define NAME "validate_block"

// Database access is limited to: populator:
// spend: { spender }
// block: { bits, version, timestamp }
// transaction: { exists, height, output }

// If the priority threadpool is shut down when this is running the handlers
// will never be invoked, resulting in a threadpool.join indefinite hang.

#if defined(KTH_WITH_MEMPOOL)
validate_block::validate_block(dispatcher& dispatch, fast_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions, mining::mempool const& mp)
#else
validate_block::validate_block(dispatcher& dispatch, fast_chain const& chain, settings const& settings, domain::config::network network, bool relay_transactions)
#endif
    : stopped_(true)
    , fast_chain_(chain)
    , network_(network)
    , priority_dispatch_(dispatch)
#if defined(KTH_WITH_MEMPOOL)
    , block_populator_(dispatch, chain, relay_transactions, mp)
#else
    , block_populator_(dispatch, chain, relay_transactions)
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

void validate_block::check(block_const_ptr block, result_handler handler) const {
    // The block hasn't been checked yet.
    if (block->transactions().empty()) {
        handler(error::success);
        return;
    }

    result_handler complete_handler = std::bind(&validate_block::handle_checked, this, _1, block, handler);

    // TODO: make configurable for each parallel segment.
    // This one is more efficient with one thread than parallel.
    auto const threads = std::min(size_t(1), priority_dispatch_.size());
    auto const count = block->transactions().size();
    auto const buckets = std::min(threads, count);
    KTH_ASSERT(buckets != 0);

    auto const join_handler = synchronize(std::move(complete_handler), buckets, NAME "_check");

    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        priority_dispatch_.concurrent(&validate_block::check_block, this, block, bucket, buckets, join_handler);
    }
}

void validate_block::check_block(block_const_ptr block, size_t bucket, size_t buckets, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    auto const& txs = block->transactions();

    // Generate each tx hash (stored in tx cache).
    for (auto tx = bucket; tx < txs.size(); tx = ceiling_add(tx, buckets)) {
        txs[tx].hash();
    }

    handler(error::success);
}

void validate_block::handle_checked(code const& ec, block_const_ptr block, result_handler handler) const {
    if (ec) {
        handler(ec);
        return;
    }

    // Run context free checks, sets time internally.
    handler(block->check());
}

// Accept sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, and block state if not under checkpoint.

void validate_block::accept(branch::const_ptr branch, result_handler handler) const {
    auto const block = branch->top();
    KTH_ASSERT(block);

    // The block has no population timer, so set externally.
    block->validation.start_populate = asio::steady_clock::now();

    // Populate chain state for the next block.
    block->validation.state = fast_chain_.chain_state(branch);

    if ( ! block->validation.state) {
        handler(error::block_validation_state_failed);
        return;
    }

    // Populate block state for the top block (others are valid).
    block_populator_.populate(branch, std::bind(&validate_block::handle_populated, this, _1, block, handler));
}

void validate_block::handle_populated(code const& ec, block_const_ptr block, result_handler handler) const {
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    if (ec) {
        handler(ec);
        return;
    }

    auto const height = block->validation.state->height();

    // Run contextual block non-tx checks (sets start time).
    auto const error_code = block->accept(false);

    if (error_code) {
        handler(error_code);
        return;
    }

    auto const sigops = std::make_shared<atomic_counter>(0);
    auto const state = block->validation.state;
    KTH_ASSERT(state);
#if defined(KTH_CURRENCY_BCH)
    bool const bip141 = false;
#else
    auto const bip141 = state->is_enabled(domain::machine::rule_fork::bip141_rule);
#endif

    result_handler complete_handler = std::bind(&validate_block::handle_accepted, this, _1, block, sigops, bip141, handler);

    if (state->is_under_checkpoint()) {
        complete_handler(error::success);
        return;
    }

    auto const count = block->transactions().size();
    auto const bip16 = state->is_enabled(domain::machine::rule_fork::bip16_rule);
    auto const buckets = std::min(priority_dispatch_.size(), count);
    KTH_ASSERT(buckets != 0);

    auto const join_handler = synchronize(std::move(complete_handler), buckets, NAME "_accept");

    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        priority_dispatch_.concurrent(&validate_block::accept_transactions, this, block, bucket, buckets, sigops, bip16, bip141, join_handler);
    }
}

void validate_block::accept_transactions(block_const_ptr block, size_t bucket, size_t buckets, atomic_counter_ptr sigops, bool bip16, bool bip141, result_handler handler) const {
#if defined(KTH_CURRENCY_BCH)
    bip141 = false;
#endif
    if (stopped()) {
        handler(error::service_stopped);
        return;
    }

    code ec(error::success);
    auto const& state = *block->validation.state;
    auto const& txs = block->transactions();
    auto const count = txs.size();

    // Run contextual tx non-script checks (not in tx order).
    for (auto tx = bucket; tx < count && !ec; tx = ceiling_add(tx, buckets)) {
        auto const& transaction = txs[tx];
        if ( ! transaction.validation.validated) {
            // LOG_INFO(LOG_BLOCKCHAIN, "Transaction ", encode_hash(transaction.hash()), " has to be validated.");
            ec = transaction.accept(state, false);
        } else {
            // LOG_INFO(LOG_BLOCKCHAIN, "Transaction ", encode_hash(transaction.hash()), " validation could be skiped.");
        }
        *sigops += transaction.signature_operations(bip16, bip141);
    }

    handler(ec);
}

void validate_block::handle_accepted(code const& ec, block_const_ptr block, atomic_counter_ptr sigops, bool bip141, result_handler handler) const {
    if (ec) {
        handler(ec);
        return;
    }

#if defined(KTH_CURRENCY_BCH)
    if (block->validation.state->is_fermat_enabled()) {
        handler(error::success);
        return;
    }

    size_t allowed_sigops = get_allowed_sigops(block->serialized_size(1));
    auto const exceeded = *sigops > allowed_sigops;
#else
    auto const max_sigops = bip141 ? max_fast_sigops : get_allowed_sigops(block->serialized_size(1));
    auto const exceeded = *sigops > max_sigops;
#endif
    handler(exceeded ? error::block_embedded_sigop_limit : error::success);
}

// Connect sequence.
//-----------------------------------------------------------------------------
// These checks require chain state, block state and perform script validation.

void validate_block::connect(branch::const_ptr branch, result_handler handler) const {
    auto const block = branch->top();
    KTH_ASSERT(block && block->validation.state);

    // We are reimplementing connect, so must set timer externally.
    block->validation.start_connect = asio::steady_clock::now();

    if (block->validation.state->is_under_checkpoint()) {
        handler(error::success);
        return;
    }

    auto const non_coinbase_inputs = block->total_inputs(false);

    // Return if there are no non-coinbase inputs to validate.
    if (non_coinbase_inputs == 0) {
        handler(error::success);
        return;
    }

    // Reset statistics for each block (treat coinbase as cached).
    hits_ = 0;
    queries_ = 0;

    result_handler complete_handler = std::bind(&validate_block::handle_connected, this, _1, block, handler);

    auto const threads = priority_dispatch_.size();
    auto const buckets = std::min(threads, non_coinbase_inputs);
    KTH_ASSERT(buckets != 0);

    auto const join_handler = synchronize(std::move(complete_handler), buckets, NAME "_validate");

    for (size_t bucket = 0; bucket < buckets; ++bucket) {
        priority_dispatch_.concurrent(&validate_block::connect_inputs, this, block, bucket, buckets, join_handler);
    }
}

void validate_block::connect_inputs(block_const_ptr block, size_t bucket, size_t buckets, result_handler handler) const {
    KTH_ASSERT(bucket < buckets);
    code ec(error::success);
    auto const forks = block->validation.state->enabled_forks();
    auto const& txs = block->transactions();
    size_t position = 0;

#if defined(KTH_CURRENCY_BCH)
    size_t block_sigchecks = 0;
#endif

    //TODO(fernando): count the coinbase sigchecks

    // Must skip coinbase here as it is already accounted for.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {
        ++queries_;

        // The tx is pooled with current fork state so outputs are validated.
        if (tx->validation.current) {
            ++hits_;
            continue;
        }

        // The tx was validated before its insertion in the mempool
        // TODO(fernando): what happend with Blockchain forks?
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
                handler(error::service_stopped);
                return;
            }

            auto const& prevout = inputs[input_index].previous_output();

            if ( ! prevout.validation.cache.is_valid()) {
                ec = error::missing_previous_output;
                break;
            }

            size_t sigchecks;
            std::tie(ec, sigchecks) = validate_input::verify_script(*tx, input_index, forks);
            if (ec != error::success) {
                break;
            }

#if defined(KTH_CURRENCY_BCH)
            block_sigchecks += sigchecks;
            // if (block_sigchecks > get_max_block_sigchecks(network_)) {
            if (block_sigchecks > block->validation.state->dynamic_max_block_sigchecks()) {
                ec = error::block_sigchecks_limit;
                break;
            }
#endif
        }

        if (ec) {
            auto const height = block->validation.state->height();
            dump(ec, *tx, input_index, forks, height);
            break;
        }
    }

    handler(ec);
}

// The tx pool cache hit rate.
float validate_block::hit_rate() const {
    // These values could overflow or divide by zero, but that's okay.
    return queries_ == 0 ? 0.0f : (hits_ * 1.0f / queries_);
}

void validate_block::handle_connected(code const& ec, block_const_ptr block, result_handler handler) const {
    block->validation.cache_efficiency = hit_rate();
    handler(ec);
}

// Utility.
//-----------------------------------------------------------------------------

void validate_block::dump(code const& ec, transaction const& tx, uint32_t input_index, uint32_t forks, size_t height) {
    auto const& prevout = tx.inputs()[input_index].previous_output();
    auto const script = prevout.validation.cache.script().to_data(false);
    auto const hash = encode_hash(prevout.hash());
    auto const tx_hash = encode_hash(tx.hash());

    spdlog::debug("[{}] Verify failed [{}] : {}\n"
        " forks        : {}\n"
        " outpoint     : {}:{}\n"
        " script       : {}\n"
        " value        : {}\n"
        " inpoint      : {}:{}\n"
        " transaction  : {}",
        LOG_BLOCKCHAIN, height, ec.message(), forks, hash, prevout.index(), encode_base16(script)
        , prevout.validation.cache.value(), tx_hash, input_index, encode_base16(tx.to_data(true)));
}

} // namespace kth::blockchain
