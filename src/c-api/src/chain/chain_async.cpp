// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_async.h>
#include <cstdio>
#include <memory>
#include <system_error>

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

inline
kth::domain::message::block::const_ptr block_shared(kth_block_const_t block) {
    auto const& block_ref = *static_cast<kth::domain::chain::block const*>(block);
    auto* block_new = new kth::domain::message::block(block_ref);
    return kth::domain::message::block::const_ptr(block_new);
}

} /* end of anonymous namespace */


// ---------------------------------------------------------------------------
extern "C" {

void kth_chain_async_last_height(kth_chain_t chain, void* ctx, kth_last_height_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_last_height();
        if (result) {
            handler(chain, ctx, kth::to_c_err(std::error_code{}), result->block);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_height(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_height_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_height(hash_cpp);
        if (result) {
            handler(chain, ctx, kth::to_c_err(std::error_code{}), *result);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_header_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_block_header_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_header(height);
        if (result) {
            auto const& [header, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(header, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_header_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_header_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_header(hash_cpp);
        if (result) {
            auto const& [header, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(header, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_block_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block(height);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block(hash_cpp);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_header_by_hash_txs_size(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_block_header_txs_size_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_header_txs_size(hash_cpp);
        if (result) {
            auto const& [header, block_height, tx_hashes, block_serialized_size] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}),
                    kth::leak_if_success(header, std::error_code{}),
                    block_height,
                    kth::leak_if_success(tx_hashes, std::error_code{}),
                    block_serialized_size);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0, nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_merkle_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_merkle_block_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_merkle_block(height);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_merkle_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_merkle_block_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_merkle_block(hash_cpp);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_compact_block_by_height(kth_chain_t chain, void* ctx, kth_size_t height, kth_compact_block_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_compact_block(height);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_compact_block_by_hash(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_compact_block_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_compact_block(hash_cpp);
        if (result) {
            auto const& [block, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(block, std::error_code{}), h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_block_by_height_timestamp(kth_chain_t chain, void* ctx, kth_size_t height, kth_blockhash_timestamp_fetch_handler_t handler) {
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_block_hash_timestamp(height);
        if (result) {
            auto const& [hash, timestamp, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::to_hash_t(hash), timestamp, h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), kth::to_hash_t(kth::null_hash), 0, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_transaction(kth_chain_t chain, void* ctx, kth_hash_t hash, kth_bool_t require_confirmed, kth_transaction_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto const confirmed = kth::int_to_bool(require_confirmed);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, confirmed, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_transaction(hash_cpp, confirmed);
        if (result) {
            auto const& [transaction, i, h] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(transaction, std::error_code{}), i, h);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr, 0, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_transaction_position(kth_chain_t chain, void* ctx, kth_hash_t hash, int require_confirmed, kth_transaction_index_fetch_handler_t handler) {
    auto hash_cpp = kth::to_array(hash.hash);
    auto const confirmed = kth::int_to_bool(require_confirmed);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, hash_cpp, confirmed, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_transaction_position(hash_cpp, confirmed);
        if (result) {
            auto const& [position, height] = *result;
            handler(chain, ctx, kth::to_c_err(std::error_code{}), position, height);
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), 0, 0);
        }
    }, ::asio::detached);
}

void kth_chain_async_spend(kth_chain_t chain, void* ctx, kth_output_point_const_t op, kth_spend_fetch_handler_t handler) {
    auto const* outpoint_cpp = static_cast<kth::domain::chain::output_point const*>(op);
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, outpoint_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_spend(*outpoint_cpp);
        if (result) {
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(*result, std::error_code{}));
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr);
        }
    }, ::asio::detached);
}

void kth_chain_async_history(kth_chain_t chain, void* ctx, kth_payment_address_t address, kth_size_t limit, kth_size_t from_height, kth_history_fetch_handler_t handler) {
    auto const addr_hash = kth::cpp_ref<kth::domain::wallet::payment_address>(address).hash20();
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, addr_hash, limit, from_height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_history(addr_hash, limit, from_height);
        if (result) {
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(*result, std::error_code{}));
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr);
        }
    }, ::asio::detached);
}

void kth_chain_async_confirmed_transactions(kth_chain_t chain, void* ctx, kth_payment_address_t address, uint64_t max, uint64_t start_height, kth_transactions_by_address_fetch_handler_t handler) {
    auto const addr_hash = kth::cpp_ref<kth::domain::wallet::payment_address>(address).hash20();
    auto& bc = safe_chain(chain);
    ::asio::co_spawn(bc.executor(), [&bc, addr_hash, max, start_height, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto result = co_await bc.fetch_confirmed_transactions(addr_hash, max, start_height);
        if (result) {
            handler(chain, ctx, kth::to_c_err(std::error_code{}), kth::leak_if_success(*result, std::error_code{}));
        } else {
            handler(chain, ctx, kth::to_c_err(result.error()), nullptr);
        }
    }, ::asio::detached);
}

// Organizers.
//-------------------------------------------------------------------------

void kth_chain_async_organize_block(kth_chain_t chain, void* ctx, kth_block_mut_t block, kth_result_handler_t handler) {
    auto& bc = safe_chain(chain);
    auto block_cpp = block_shared(block);
    ::asio::co_spawn(bc.executor(), [&bc, block_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto ec = co_await bc.organize(block_cpp);
        handler(chain, ctx, kth::to_c_err(ec));
    }, ::asio::detached);
}

void kth_chain_async_organize_transaction(kth_chain_t chain, void* ctx, kth_transaction_mut_t transaction, kth_result_handler_t handler) {
    auto& bc = safe_chain(chain);
    auto tx_cpp = tx_shared(transaction);
    ::asio::co_spawn(bc.executor(), [&bc, tx_cpp, chain, ctx, handler]() -> ::asio::awaitable<void> {
        auto ec = co_await bc.organize(tx_cpp);
        handler(chain, ctx, kth::to_c_err(ec));
    }, ::asio::detached);
}

} // extern "C"
