// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_other.h>
#include <cstdio>
#include <latch>
#include <memory>


#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/domain/chain/block_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

#include <asio/co_spawn.hpp>
#include <kth/capi/detail/async_exception.hpp>
#include <asio/use_awaitable.hpp>
#include <optional>
#include <vector>

namespace {

inline
kth::blockchain::block_chain& safe_chain(kth_chain_t chain) {
    return *static_cast<kth::blockchain::block_chain*>(chain);
}

inline
kth::domain::message::transaction::const_ptr tx_shared(kth_transaction_const_t tx) {
    auto const& tx_ref = *static_cast<kth::domain::chain::transaction const*>(tx);
    auto* tx_new = new kth::domain::message::transaction(tx_ref);
    return kth::domain::message::transaction::const_ptr(tx_new);
}

// inline
// kth::domain::chain::transaction::const_ptr tx_shared(kth_transaction_t tx) {
//     auto const& tx_ref = *static_cast<kth::domain::chain::transaction const*>(tx);
//     auto* tx_new = new kth::domain::chain::transaction(tx_ref);
//     return kth::domain::chain::transaction::const_ptr(tx_new);
// }

inline
kth::domain::message::block::const_ptr block_shared(kth_block_const_t block) {
    auto const& block_ref = *static_cast<kth::domain::chain::block const*>(block);
    auto* block_new = new kth::domain::message::block(block_ref);
    return kth::domain::message::block::const_ptr(block_new);
}

inline
kth_block_const_t cast_block(kth::domain::chain::block const& x) {
    return &x;
}

// Cleanup precedes terminal delivery, including failure before the receiver starts.
// A handler returning zero has withdrawn its context: never call it again.
template<class Channel, class Unsubscribe>
kth_error_code_t release_subscription(kth::blockchain::block_chain& chain,
                                     Channel const& channel, Unsubscribe unsubscribe) noexcept {
    try {
        (chain.*unsubscribe)(channel);
        return kth_ec_success;
    } catch (...) {
        auto status = kth::capi::async_exception_status(std::current_exception());
        // Even if removing the registration fails, stop further channel delivery.
        try { channel->close(); } catch (...) {}
        return status;
    }
}

template<class Blocks>
auto copy_blocks(Blocks const& blocks) {
    std::unique_ptr<std::vector<kth::domain::chain::block>> result;
    if (blocks) {
        result = std::make_unique<std::vector<kth::domain::chain::block>>();
        result->reserve(blocks->size());
        for (auto const& block : *blocks) result->push_back(*block);
    }
    return result;
}

} /* end of anonymous namespace */


// ---------------------------------------------------------------------------
extern "C" {

// Subscribers.
//-------------------------------------------------------------------------

void kth_chain_subscribe_blockchain(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_blockchain_handler_t handler) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto& bc = safe_chain(chain);
    kth::blockchain::block_chain::block_channel_ptr channel;
    auto unsubscribe = &kth::blockchain::block_chain::unsubscribe_blockchain;
    try {
        channel = bc.subscribe_blockchain();
        if (!channel) {
            handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), 0, nullptr, nullptr);
            return;
        }
        ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]()
                -> ::asio::awaitable<std::optional<kth_error_code_t>> {
            while (channel->is_open()) {
                auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
                auto& [ec, fork_height, incoming, replaced_blocks] = result;
                if (ec) co_return kth::to_c_err(kth::error::service_stopped);
                // A tip wake-up carries no block event for C subscribers.
                if (!incoming && !replaced_blocks) continue;
                if (bc.is_stale()) continue;
                auto incoming_owned = copy_blocks(incoming);
                auto replaced_owned = copy_blocks(replaced_blocks);
                if (handler(exec, chain, ctx, kth_ec_success, fork_height,
                            incoming_owned.release(), replaced_owned.release()) == 0) co_return std::nullopt;
            }
            co_return kth::to_c_err(kth::error::service_stopped);
        }, [exec, chain, ctx, handler, channel, &bc, unsubscribe]
           (std::exception_ptr failure, std::optional<kth_error_code_t> terminal) {
            if (failure) terminal = kth::capi::async_exception_status(failure);
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (terminal) {
                if (cleanup != kth_ec_success) terminal = cleanup;
                handler(exec, chain, ctx, *terminal, 0, nullptr, nullptr);
            }
        });
    } catch (...) {
        auto status = kth::capi::async_exception_status(std::current_exception());
        if (channel) {
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (cleanup != kth_ec_success) status = cleanup;
        }
        handler(exec, chain, ctx, status, 0, nullptr, nullptr);
    }
}

void kth_chain_subscribe_transaction(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_transaction_handler_t handler) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto& bc = safe_chain(chain);
    kth::blockchain::block_chain::transaction_channel_ptr channel;
    auto unsubscribe = &kth::blockchain::block_chain::unsubscribe_transaction;
    try {
        channel = bc.subscribe_transaction();
        if (!channel) {
            handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
            return;
        }
        ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]()
                -> ::asio::awaitable<std::optional<kth_error_code_t>> {
            while (channel->is_open()) {
                auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
                auto& [ec, tx] = result;
                if (ec) co_return kth::to_c_err(kth::error::service_stopped);
                if (handler(exec, chain, ctx, kth_ec_success, kth::to_owned(tx)) == 0) co_return std::nullopt;
            }
            co_return kth::to_c_err(kth::error::service_stopped);
        }, [exec, chain, ctx, handler, channel, &bc, unsubscribe]
           (std::exception_ptr failure, std::optional<kth_error_code_t> terminal) {
            if (failure) terminal = kth::capi::async_exception_status(failure);
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (terminal) {
                if (cleanup != kth_ec_success) terminal = cleanup;
                handler(exec, chain, ctx, *terminal, nullptr);
            }
        });
    } catch (...) {
        auto status = kth::capi::async_exception_status(std::current_exception());
        if (channel) {
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (cleanup != kth_ec_success) status = cleanup;
        }
        handler(exec, chain, ctx, status, nullptr);
    }
}

void kth_chain_subscribe_ds_proof(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_ds_proof_handler_t handler) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(handler != nullptr);
    auto& bc = safe_chain(chain);
    kth::blockchain::block_chain::ds_proof_channel_ptr channel;
    auto unsubscribe = &kth::blockchain::block_chain::unsubscribe_ds_proof;
    try {
        channel = bc.subscribe_ds_proof();
        if (!channel) {
            handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
            return;
        }
        ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]()
                -> ::asio::awaitable<std::optional<kth_error_code_t>> {
            while (channel->is_open()) {
                auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
                auto& [ec, dsp] = result;
                if (ec) co_return kth::to_c_err(kth::error::service_stopped);
                if (handler(exec, chain, ctx, kth_ec_success, kth::to_owned(dsp)) == 0) co_return std::nullopt;
            }
            co_return kth::to_c_err(kth::error::service_stopped);
        }, [exec, chain, ctx, handler, channel, &bc, unsubscribe]
           (std::exception_ptr failure, std::optional<kth_error_code_t> terminal) {
            if (failure) terminal = kth::capi::async_exception_status(failure);
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (terminal) {
                if (cleanup != kth_ec_success) terminal = cleanup;
                handler(exec, chain, ctx, *terminal, nullptr);
            }
        });
    } catch (...) {
        auto status = kth::capi::async_exception_status(std::current_exception());
        if (channel) {
            auto cleanup = release_subscription(bc, channel, unsubscribe);
            if (cleanup != kth_ec_success) status = cleanup;
        }
        handler(exec, chain, ctx, status, nullptr);
    }
}

// Note: kth_chain_unsubscribe is no longer needed with the new model
// Each subscription manages its own lifecycle via the returned channel
void kth_chain_unsubscribe(kth_chain_t /*chain*/) {
    // No-op: subscriptions are now managed individually
    // Callers should return 0 from their handler to unsubscribe
}

void kth_chain_transaction_validate(kth_chain_t chain, void* ctx, kth_transaction_mut_t tx, kth_validate_tx_handler_t handler) {
    if (handler == nullptr) return;
    KTH_PRECONDITION(chain != nullptr);
    auto completion = kth::capi::async_completion{
        [chain, ctx, handler](auto ec) {
            if (ec) {
                handler(chain, ctx, kth::to_c_err(ec), ec.message().c_str());
            } else {
                handler(chain, ctx, kth::to_c_err(ec), nullptr);
            }
        },
        [chain, ctx, handler](kth_error_code_t error) noexcept {
            handler(chain, ctx, error, nullptr);
        }
    };
    try {
        auto& bc = safe_chain(chain);
        auto tx_cpp = tx_shared(tx);
        ::asio::co_spawn(bc.executor(), [&bc, tx_cpp]() { return bc.transaction_validate(tx_cpp); }, completion);
    } catch (...) {
        completion.fail(std::current_exception());
    }
}

// Properties.
//-------------------------------------------------------------------------

/// True if the blockchain is stale based on configured age limit.
kth_bool_t kth_chain_is_stale(kth_chain_t chain) {
    return kth::bool_to_int(safe_chain(chain).is_stale());
}

} // extern "C"








//-------------------------------------------------------------------------





// ------------------------------------------------------------------
//virtual void fetch_locator_block_hashes(get_blocks_const_ptr locator, hash_digest const& threshold, size_t limit, inventory_fetch_handler handler) const = 0;

//void kth_chain_fetch_locator_block_hashes(kth_chain_t chain, void* ctx, kth_get_blocks_ptr_t locator, kth_hash_t threshold, kth_size_t limit, inventory_fetch_handler handler) {
//}


//virtual void fetch_locator_block_headers(get_headers_const_ptr locator, hash_digest const& threshold, size_t limit, locator_block_headers_fetch_handler handler) const = 0;
//

//// Transaction Pool.
////-------------------------------------------------------------------------
//
//virtual void fetch_template(merkle_block_fetch_handler handler) const = 0;
//virtual void fetch_mempool(size_t count_limit, uint64_t minimum_fee, inventory_fetch_handler handler) const = 0;
//


//// Filters.
////-------------------------------------------------------------------------
//
//virtual void filter_blocks(get_data_ptr chain, result_handler handler) const = 0;

//void kth_chain_filter_blocks(kth_chain_t chain, void* ctx, get_data_ptr chain, result_handler handler) {
//}


//virtual void filter_transactions(get_data_ptr message, result_handler handler) const = 0;
// ------------------------------------------------------------------
