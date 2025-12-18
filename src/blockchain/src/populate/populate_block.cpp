// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_block.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>

#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/domain.hpp>

#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace kd::machine;

// Database access is limited to calling populate_base.

#if defined(KTH_WITH_MEMPOOL)
populate_block::populate_block(executor_type executor, size_t threads, block_chain const& chain, bool relay_transactions, mining::mempool const& mp)
#else
populate_block::populate_block(executor_type executor, size_t threads, block_chain const& chain, bool relay_transactions)
#endif
    : populate_base(std::move(executor), threads, chain)
    , relay_transactions_(relay_transactions)
#if defined(KTH_WITH_MEMPOOL)
    , mempool_(mp)
#endif
{}

::asio::awaitable<code> populate_block::populate(branch::const_ptr branch) const {
    auto const block = branch->top();
    KTH_ASSERT(block);

    auto const state = block->validation.state;
    KTH_ASSERT(state);

    // Return if this blocks is under a checkpoint, block state not required.
    if (state->is_under_checkpoint()) {
        co_return error::success;
    }

    // Handle the coinbase as a special case tx.
    populate_coinbase(branch, block);

    auto const non_coinbase_inputs = block->total_inputs(false);

    // Return if there are no non-coinbase inputs to validate.
    if (non_coinbase_inputs == 0) {
        co_return error::success;
    }

    auto const buckets = std::min(threads_, non_coinbase_inputs);
    KTH_ASSERT(buckets != 0);

    auto branch_utxo = create_branch_utxo_set(branch);

#if defined(KTH_WITH_MEMPOOL)
    auto validated_txs = mempool_.get_validated_txs_high();
#endif

    // Use a channel to collect results from parallel tasks
    using result_channel = ::asio::experimental::concurrent_channel<void(std::error_code, code)>;
    auto channel = std::make_shared<result_channel>(executor_, buckets);

    // Launch parallel tasks
    for (size_t bucket = 0; bucket < buckets; ++bucket) {
#if defined(KTH_WITH_MEMPOOL)
        ::asio::post(executor_, [this, branch, bucket, buckets, branch_utxo, validated_txs, channel]() {
            auto result = populate_transactions_sync(branch, bucket, buckets, branch_utxo, validated_txs);
            channel->try_send(std::error_code{}, result);
        });
#else
        ::asio::post(executor_, [this, branch, bucket, buckets, branch_utxo, channel]() {
            auto result = populate_transactions_sync(branch, bucket, buckets, branch_utxo);
            channel->try_send(std::error_code{}, result);
        });
#endif
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

// Initialize the coinbase input for subsequent validation.
void populate_block::populate_coinbase(branch::const_ptr branch, block_const_ptr block) const {
    auto const& txs = block->transactions();
    auto const state = block->validation.state;
    KTH_ASSERT( ! txs.empty());

    auto const& coinbase = txs.front();
    KTH_ASSERT(coinbase.is_coinbase());

    // A coinbase tx guarantees exactly one input.
    auto const& input = coinbase.inputs().front();
    auto& prevout = input.previous_output().validation;

    // A coinbase input cannot be a double spend since it originates coin.
    prevout.spent = false;

    // A coinbase is confirmed as long as its block is valid (context free).
    prevout.confirmed = true;

    // A coinbase does not spend a previous output so these are unused/default.
    prevout.cache = domain::chain::output{};
    prevout.coinbase = false;
    prevout.height = 0;
    prevout.median_time_past = 0;

    //*************************************************************************
    // CONSENSUS: Satoshi implemented allow collisions in Nov 2015. This is a
    // hard fork that destroys unspent outputs in case of hash collision.
    // The tx duplicate check must apply to coinbase txs, handled here.
    //*************************************************************************
    if ( ! state->is_enabled(domain::machine::script_flags::allow_collisions)) {
        populate_base::populate_duplicate(branch->height(), coinbase, true);
    }
}

populate_block::utxo_pool_t populate_block::get_reorg_subset_conditionally(size_t first_height, size_t& out_chain_top) const {
    auto const heights = chain_.get_last_heights();
    if ( ! heights) {
        out_chain_top = 0;
        return {};
    }

    out_chain_top = heights->first;  // header_height

    if (first_height > out_chain_top) {
        return {};
    }

    auto p = chain_.get_utxo_pool_from(first_height, out_chain_top);
    if ( ! p) {
        return {};
    }

    return std::move(*p);
}


void populate_block::populate_transaction_inputs(branch::const_ptr branch, domain::chain::input::list const& inputs, size_t bucket, size_t buckets, size_t input_position, local_utxo_set_t const& branch_utxo, size_t first_height, size_t chain_top, utxo_pool_t const& reorg_subset) const {
    auto const branch_height = branch->height();

    for (size_t input_index = 0; input_index < inputs.size(); ++input_index, ++input_position) {
        if (input_position % buckets != bucket) {
            continue;
        }

        auto const& input = inputs[input_index];
        auto const& prevout = input.previous_output();
        populate_base::populate_prevout(branch_height, prevout, true);  //Populate from Database
        populate_prevout(branch, prevout, branch_utxo);                 //Populate from the Blocks in the Branch

        if (first_height <= chain_top) {
            populate_from_reorg_subset(prevout, reorg_subset);
        }
    }
}

#if defined(KTH_WITH_MEMPOOL)
code populate_block::populate_transactions_sync(branch::const_ptr branch, size_t bucket, size_t buckets, local_utxo_set_t const& branch_utxo, mining::mempool::hash_index_t const& validated_txs) const {
#else
code populate_block::populate_transactions_sync(branch::const_ptr branch, size_t bucket, size_t buckets, local_utxo_set_t const& branch_utxo) const {
#endif
    KTH_ASSERT(bucket < buckets);
    auto const block = branch->top();
    auto const branch_height = branch->height();
    auto const& txs = block->transactions();
    size_t input_position = 0;

    auto const state = block->validation.state;

    // Must skip coinbase here as it is already accounted for.
    auto const first = bucket == 0 ? buckets : bucket;

    for (auto position = first; position < txs.size(); position = ceiling_add(position, buckets)) {
        auto const& tx = txs[position];

        //---------------------------------------------------------------------
        // This prevents output validation and full tx deposit respectively.
        // The tradeoff is a read per tx that may not be cached. This is
        // bypassed by checkpoints. This will be optimized using the tx pool.
        // Until that time this is a material population performance hit.
        // However the hit is necessary in preventing store tx duplication
        // unless tx relay is disabled. In that case duplication is unlikely.
        //---------------------------------------------------------------------

        if (relay_transactions_) {
            populate_base::populate_pooled(tx, state->height());
        }

        //*********************************************************************
        // CONSENSUS: Satoshi implemented allow collisions in Nov 2015. This is
        // a hard fork that destroys unspent outputs in case of hash collision.
        //*********************************************************************
        //Knuth: we are not validating tx duplicates.
        // if ( ! collide) {
        //     populate_base::populate_duplicate(branch->height(), tx, true);
        // }
    }

    size_t first_height = branch_height + 1u;
    size_t chain_top;
    auto reorg_subset = get_reorg_subset_conditionally(first_height, /*out*/ chain_top);

    // Must skip coinbase here as it is already accounted for.
    for (auto tx = txs.begin() + 1; tx != txs.end(); ++tx) {

#if defined(KTH_WITH_MEMPOOL)
        auto it = validated_txs.find(tx->hash());
        if (it == validated_txs.end()) {
            auto const& inputs = tx->inputs();
            populate_transaction_inputs(branch, inputs, bucket, buckets, input_position, branch_utxo, first_height, chain_top, reorg_subset);
        } else {
            tx->validation.validated = true;
            auto const& tx_cached = it->second.second;
            for (size_t i = 0; i < tx_cached.inputs().size(); ++i) {
                tx->inputs()[i].previous_output().validation = tx_cached.inputs()[i].previous_output().validation;
            }
        }
#else
        auto const& inputs = tx->inputs();
        populate_transaction_inputs(branch, inputs, bucket, buckets, input_position, branch_utxo, first_height, chain_top, reorg_subset);
#endif // defined(KTH_WITH_MEMPOOL)
    }

    return error::success;
}

void populate_block::populate_from_reorg_subset(output_point const& outpoint, utxo_pool_t const& reorg_subset) const {
    if (outpoint.validation.cache.is_valid()) {
        return;
    }

    auto it = reorg_subset.find(outpoint);
    if (it != reorg_subset.end()) {
        auto& val = outpoint.validation;
        auto const& entry = it->second;
        val.height = entry.height();
        val.median_time_past = entry.median_time_past();
        val.cache = entry.output();
        val.coinbase = entry.coinbase();
    }

}

void populate_block::populate_prevout(branch::const_ptr branch, output_point const& outpoint, local_utxo_set_t const& branch_utxo) const {
    if ( ! outpoint.validation.spent) {
        branch->populate_spent(outpoint);
    }

    // Populate the previous output even if it is spent.
    if ( ! outpoint.validation.cache.is_valid()) {
        branch->populate_prevout(outpoint, branch_utxo);
    }
}

} // namespace kth::blockchain
