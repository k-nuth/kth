// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/blockchain/populate/populate_transaction.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

#include <kth/blockchain/define.hpp>
#include <kth/blockchain/interface/block_chain.hpp>
#include <kth/blockchain/pools/branch.hpp>
#include <kth/domain.hpp>

#include <asio/co_spawn.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/experimental/concurrent_channel.hpp>

namespace kth::blockchain {

using namespace kd::chain;
using namespace std::placeholders;

#define NAME "populate_transaction"

// Database access is limited to calling populate_base.

#if defined(KTH_WITH_MEMPOOL)
populate_transaction::populate_transaction(executor_type executor, size_t threads, block_chain const& chain, mining::mempool const& mp)
    : populate_base(std::move(executor), threads, chain)
    , mempool_(mp)
{}
#else
populate_transaction::populate_transaction(executor_type executor, size_t threads, block_chain const& chain)
    : populate_base(std::move(executor), threads, chain)
{}
#endif

::asio::awaitable<code> populate_transaction::populate(transaction_const_ptr tx) const {
    auto const state = tx->validation.state;
    KTH_ASSERT(state);

    // Chain state is for the next block, so always > 0.
    KTH_ASSERT(tx->validation.state->height() > 0);
    auto const chain_height = tx->validation.state->height() - 1u;

    //*************************************************************************
    // CONSENSUS:
    // It is OK for us to restrict *pool* transactions to those that do not
    // collide with any in the chain (as well as any in the pool) as collision
    // will result in monetary destruction and we don't want to facilitate it.
    // We must allow collisions in *block* validation if that is configured as
    // otherwise will will not follow the chain when a collision is mined.
    //*************************************************************************
    populate_base::populate_duplicate(chain_height, *tx, false);

    // Because txs include no proof of work we much short circuit here.
    // Otherwise a peer can flood us with repeat transactions to validate.
    if (tx->validation.duplicate) {
        co_return error::unspent_duplicate;
    }

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
        ::asio::post(executor_, [this, tx, chain_height, bucket, buckets, channel]() {
            auto result = populate_inputs_sync(tx, chain_height, bucket, buckets);
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

code populate_transaction::populate_inputs_sync(transaction_const_ptr tx, size_t chain_height, size_t bucket, size_t buckets) const {
    KTH_ASSERT(bucket < buckets);
    auto const& inputs = tx->inputs();

    for (auto input_index = bucket; input_index < inputs.size(); input_index = ceiling_add(input_index, buckets)) {
        auto const& input = inputs[input_index];
        auto const& prevout = input.previous_output();
        populate_prevout(chain_height, prevout, false);

#if defined(KTH_WITH_MEMPOOL)
        if ( ! prevout.validation.cache.is_valid()) {
            // BUSCAR EN UTXO DEL MEMPOOL y marcar
            prevout.validation.cache = mempool_.get_utxo(prevout);
            if (prevout.validation.cache.is_valid()) {
                prevout.validation.from_mempool = true;
            }
        }
#endif

    }

    return error::success;
}

} // namespace kth::blockchain
