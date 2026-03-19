// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_other.h>
#include <cstdio>
#include <latch>
#include <memory>

// #include <boost/thread/latch.hpp>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/chain/block_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>

namespace {

inline
kth::blockchain::block_chain& safe_chain(kth_chain_t chain) {
    return *static_cast<kth::blockchain::block_chain*>(chain);
}

inline
kth::domain::message::transaction::const_ptr tx_shared(kth_transaction_t tx) {
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
kth::domain::message::block::const_ptr block_shared(kth_block_t block) {
    auto const& block_ref = *static_cast<kth::domain::chain::block const*>(block);
    auto* block_new = new kth::domain::message::block(block_ref);
    return kth::domain::message::block::const_ptr(block_new);
}

inline
kth_block_t cast_block(kth::domain::chain::block const& x) {
    return const_cast<kth::domain::chain::block*>(&x);
}

} /* end of anonymous namespace */


// ---------------------------------------------------------------------------
extern "C" {

// Subscribers.
//-------------------------------------------------------------------------

void kth_chain_subscribe_blockchain(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_block_handler_t handler) {
    auto& bc = safe_chain(chain);
    auto channel = bc.subscribe_blockchain();

    if ( ! channel) {
        // Broadcaster is stopped
        handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), 0, nullptr, nullptr);
        return;
    }

    ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]() -> ::asio::awaitable<void> {
        while (channel->is_open()) {
            auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            auto& [ec, fork_height, incoming, replaced_blocks] = result;

            if (ec) {
                // Channel closed or error
                handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), 0, nullptr, nullptr);
                break;
            }

            if (bc.is_stale()) {
                continue;
            }

            kth_block_list_t incoming_cpp = nullptr;
            if (incoming) {
                incoming_cpp = kth_chain_block_list_construct_default();
                for (auto&& x : *incoming) {
                    kth_chain_block_list_push_back(incoming_cpp, cast_block(*x));
                }
            }

            kth_block_list_t replaced_blocks_cpp = nullptr;
            if (replaced_blocks) {
                replaced_blocks_cpp = kth_chain_block_list_construct_default();
                for (auto&& x : *replaced_blocks) {
                    kth_chain_block_list_push_back(replaced_blocks_cpp, cast_block(*x));
                }
            }

            auto res = handler(exec, chain, ctx, kth::to_c_err(std::error_code{}), fork_height, incoming_cpp, replaced_blocks_cpp);
            if (res == 0) {
                // Unsubscribe requested
                bc.unsubscribe_blockchain(channel);
                break;
            }
        }
    }, ::asio::detached);
}

void kth_chain_subscribe_transaction(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_transaction_handler_t handler) {
    auto& bc = safe_chain(chain);
    auto channel = bc.subscribe_transaction();

    if ( ! channel) {
        handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
        return;
    }

    ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]() -> ::asio::awaitable<void> {
        while (channel->is_open()) {
            auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            auto& [ec, tx] = result;

            if (ec) {
                handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
                break;
            }

            auto res = handler(exec, chain, ctx, kth::to_c_err(std::error_code{}), kth::leak(tx));
            if (res == 0) {
                bc.unsubscribe_transaction(channel);
                break;
            }
        }
    }, ::asio::detached);
}

void kth_chain_subscribe_ds_proof(kth_node_t exec, kth_chain_t chain, void* ctx, kth_subscribe_ds_proof_handler_t handler) {
    auto& bc = safe_chain(chain);
    auto channel = bc.subscribe_ds_proof();

    if ( ! channel) {
        handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
        return;
    }

    ::asio::co_spawn(bc.executor(), [exec, chain, ctx, handler, channel, &bc]() -> ::asio::awaitable<void> {
        while (channel->is_open()) {
            auto result = co_await channel->async_receive(::asio::as_tuple(::asio::use_awaitable));
            auto& [ec, dsp] = result;

            if (ec) {
                handler(exec, chain, ctx, kth::to_c_err(kth::error::service_stopped), nullptr);
                break;
            }

            auto res = handler(exec, chain, ctx, kth::to_c_err(std::error_code{}), kth::leak(dsp));
            if (res == 0) {
                bc.unsubscribe_ds_proof(channel);
                break;
            }
        }
    }, ::asio::detached);
}

// Note: kth_chain_unsubscribe is no longer needed with the new model
// Each subscription manages its own lifecycle via the returned channel
void kth_chain_unsubscribe(kth_chain_t /*chain*/) {
    // No-op: subscriptions are now managed individually
    // Callers should return 0 from their handler to unsubscribe
}

void kth_chain_transaction_validate_sequential(kth_chain_t chain, void* ctx, kth_transaction_t tx, kth_validate_tx_handler_t handler) {
    if (handler == nullptr) return;

    auto tx_cpp = tx_shared(tx);
    tx_cpp->validation.simulate = true;

    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, tx_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto ec = co_await bc.organize(tx_cpp);
        if (ec) {
            handler(chain, ctx, kth::to_c_err(ec), ec.message().c_str());
        } else {
            handler(chain, ctx, kth::to_c_err(ec), nullptr);
        }
    }, ::asio::detached);
}

void kth_chain_transaction_validate(kth_chain_t chain, void* ctx, kth_transaction_t tx, kth_validate_tx_handler_t handler) {
    if (handler == nullptr) return;

    auto& bc = safe_chain(chain);
    auto tx_cpp = tx_shared(tx);
    ::asio::co_spawn(bc.executor(), [&bc, tx_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto ec = co_await bc.transaction_validate(tx_cpp);
        if (ec) {
            handler(chain, ctx, kth::to_c_err(ec), ec.message().c_str());
        } else {
            handler(chain, ctx, kth::to_c_err(ec), nullptr);
        }
    }, ::asio::detached);
}

// Properties.
//-------------------------------------------------------------------------

/// True if the blockchain is stale based on configured age limit.
kth_bool_t kth_chain_is_stale(kth_chain_t chain) {
    return kth::bool_to_int(safe_chain(chain).is_stale());
}

} // extern "C"








//-------------------------------------------------------------------------

// kth_transaction_t kth_chain_hex_to_tx(char const* tx_hex) {
//
//    static auto const version = kth::domain::chain::version::level::canonical;
//
////    auto const tx = std::make_shared<kth::domain::chain::transaction>();
//    auto* tx = new kth::domain::chain::transaction;
//
//    std::string tx_hex_cpp(tx_hex);
//    std::vector<uint8_t> data(tx_hex_cpp.size() / 2); // (tx_hex_cpp.begin(), tx_hex_cpp.end());
//    //data.reserve(tx_hex_cpp.size() / 2);
//
//    hex2bin(tx_hex_cpp.c_str(), data.data());
//
//    if ( ! tx->from_data(version, data)) {
//        return nullptr;
//    }
//
//    // Simulate organization into our chain.
//    tx->validation.simulate = true;
//
//    return tx;
//}



// ------------------------------------------------------------------
//virtual void fetch_block_locator(chain::block::indexes const& heights, block_locator_fetch_handler handler) const = 0;

//void kth_chain_fetch_block_locator(kth_chain_t chain, void* ctx, kth_block_indexes_t heights, kth_block_locator_fetch_handler_t handler) {
//    auto const& heights_cpp = kth_chain_block_indexes_const_cpp(heights);
//
//    safe_chain(chain).fetch_block_locator(heights_cpp, [chain, ctx, handler](std::error_code const& ec, kth::get_headers_ptr headers) {
//        //TODO: check if the pointer is set, before dereferencing
//        auto* new_headers = new kth::domain::chain::get_headers(*headers);
//        handler(chain, ctx, kth::to_c_err(ec), new_headers);
//    });
//}
//

//kth_error_code_t kth_chain_get_block_locator(kth_chain_t chain, kth_block_indexes_t heights, kth_get_headers_ptr_t* out_headers) {
//    std::latch latch(1); //Note: workaround to fix an error on some versions of Boost.Threads
//    kth_error_code_t res;
//
//    auto const& heights_cpp = kth_chain_block_indexes_const_cpp(heights);
//
//    safe_chain(chain).fetch_block_locator(heights_cpp, [&](std::error_code const& ec, kth::get_headers_ptr headers) {
//        //TODO: check if the pointer is set, before dereferencing
//        *out_headers = new kth::domain::chain::get_headers(*headers);
//        res = kth::to_c_err(ec);
//        latch.count_down();
//    });
//
//    latch.wait();
//    return res;
//}



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
