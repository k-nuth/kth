// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_sync.h>
#include <cstdio>
#include <future>
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
#include <asio/use_future.hpp>

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

// Run an awaitable on the chain's executor and block until it completes.
// Returns the awaitable's value type (typically std::expected<T, code> for
// fetch_* methods, or code for organize()).
template <typename Awaitable>
auto sync_wait(kth::blockchain::block_chain& bc, Awaitable awaitable) {
    auto fut = ::asio::co_spawn(bc.executor(), std::move(awaitable), ::asio::use_future);
    return fut.get();
}

} /* end of anonymous namespace */


// ---------------------------------------------------------------------------
extern "C" {

kth_error_code_t kth_chain_sync_last_height(kth_chain_t chain, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_last_height());
    if (result) {
        *out_height = result->block;
        return kth::to_c_err(std::error_code{});
    }
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_height(kth_chain_t chain, kth_hash_t hash, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_block_height(hash_cpp));
    if (result) {
        *out_height = *result;
        return kth::to_c_err(std::error_code{});
    }
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_header_by_height(kth_chain_t chain, kth_size_t height, kth_header_mut_t* out_header, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_block_header(height));
    if (result) {
        auto const& [header, h] = *result;
        *out_header = kth::leak_if_success(header, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_header = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_header_by_hash(kth_chain_t chain, kth_hash_t hash, kth_header_mut_t* out_header, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_block_header(hash_cpp));
    if (result) {
        auto const& [header, h] = *result;
        *out_header = kth::leak_if_success(header, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_header = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_by_height(kth_chain_t chain, kth_size_t height, kth_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_block(height));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_by_hash(kth_chain_t chain, kth_hash_t hash, kth_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_block(hash_cpp));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_header_byhash_txs_size(kth_chain_t chain, kth_hash_t hash, kth_header_mut_t* out_header, uint64_t* out_block_height, kth_hash_list_mut_t* out_tx_hashes, uint64_t* out_serialized_size) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_block_header_txs_size(hash_cpp));
    if (result) {
        auto const& [header, block_height, tx_hashes, block_serialized_size] = *result;
        *out_header = kth::leak_if_success(header, std::error_code{});
        *out_block_height = block_height;
        *out_tx_hashes = kth::leak_if_success(tx_hashes, std::error_code{});
        *out_serialized_size = block_serialized_size;
        return kth::to_c_err(std::error_code{});
    }
    *out_header = nullptr;
    *out_block_height = 0;
    *out_tx_hashes = nullptr;
    *out_serialized_size = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_merkle_block_by_height(kth_chain_t chain, kth_size_t height, kth_merkle_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_merkle_block(height));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_merkle_block_by_hash(kth_chain_t chain, kth_hash_t hash, kth_merkle_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_merkle_block(hash_cpp));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_compact_block_by_height(kth_chain_t chain, kth_size_t height, kth_compact_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_compact_block(height));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_compact_block_by_hash(kth_chain_t chain, kth_hash_t hash, kth_compact_block_mut_t* out_block, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_compact_block(hash_cpp));
    if (result) {
        auto const& [block, h] = *result;
        *out_block = kth::leak_if_success(block, std::error_code{});
        *out_height = h;
        return kth::to_c_err(std::error_code{});
    }
    *out_block = nullptr;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_block_by_height_timestamp(kth_chain_t chain, kth_size_t height, kth_hash_t* out_hash, uint32_t* out_timestamp) {
    auto& bc = safe_chain(chain);
    auto result = sync_wait(bc, bc.fetch_block_hash_timestamp(height));
    if (result) {
        auto const& [hash, timestamp, h] = *result;
        kth::copy_c_hash(hash, out_hash);
        *out_timestamp = timestamp;
        return kth::to_c_err(std::error_code{});
    }
    kth::copy_c_hash(kth::null_hash, out_hash);
    *out_timestamp = 0;
    return kth::to_c_err(result.error());
}



kth_error_code_t kth_chain_sync_block_hash(kth_chain_t chain, kth_size_t height, kth_hash_t* out_hash) {
    auto result = safe_chain(chain).get_block_hash(height);
    if ( ! result) {
        return kth_ec_not_found;
    }
    kth::copy_c_hash(*result, out_hash);
    return kth_ec_success;
}

kth_error_code_t kth_chain_sync_transaction(kth_chain_t chain, kth_hash_t hash, int require_confirmed, kth_transaction_mut_t* out_transaction, kth_size_t* out_height, kth_size_t* out_index) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_transaction(hash_cpp, kth::int_to_bool(require_confirmed)));
    if (result) {
        auto const& [transaction, i, h] = *result;
        *out_transaction = kth::leak_if_success(transaction, std::error_code{});
        *out_height = h;
        *out_index = i;
        return kth::to_c_err(std::error_code{});
    }
    *out_transaction = nullptr;
    *out_height = 0;
    *out_index = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_transaction_position(kth_chain_t chain, kth_hash_t hash, int require_confirmed, kth_size_t* out_position, kth_size_t* out_height) {
    auto& bc = safe_chain(chain);
    auto hash_cpp = kth::to_array(hash.hash);
    auto result = sync_wait(bc, bc.fetch_transaction_position(hash_cpp, kth::int_to_bool(require_confirmed)));
    if (result) {
        auto const& [position, height] = *result;
        *out_position = position;
        *out_height = height;
        return kth::to_c_err(std::error_code{});
    }
    *out_position = 0;
    *out_height = 0;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_spend(kth_chain_t chain, kth_output_point_const_t op, kth_input_point_mut_t* out_input_point) {
    auto& bc = safe_chain(chain);
    auto const* outpoint_cpp = static_cast<kth::domain::chain::output_point const*>(op);
    auto result = sync_wait(bc, bc.fetch_spend(*outpoint_cpp));
    if (result) {
        *out_input_point = kth::leak_if_success(**result, std::error_code{});
        return kth::to_c_err(std::error_code{});
    }
    *out_input_point = nullptr;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_history(kth_chain_t chain, kth_payment_address_t address, kth_size_t limit, kth_size_t from_height, kth_history_compact_list_mut_t* out_history) {
    auto& bc = safe_chain(chain);
    auto const addr_hash = kth::cpp_ref<kth::domain::wallet::payment_address>(address).hash20();
    auto result = sync_wait(bc, bc.fetch_history(addr_hash, limit, from_height));
    if (result) {
        *out_history = kth::leak_if_success(*result, std::error_code{});
        return kth::to_c_err(std::error_code{});
    }
    *out_history = nullptr;
    return kth::to_c_err(result.error());
}

kth_error_code_t kth_chain_sync_confirmed_transactions(kth_chain_t chain, kth_payment_address_t address, uint64_t max, uint64_t start_height, kth_hash_list_mut_t* out_tx_hashes) {
    auto& bc = safe_chain(chain);
    auto const addr_hash = kth::cpp_ref<kth::domain::wallet::payment_address>(address).hash20();
    auto result = sync_wait(bc, bc.fetch_confirmed_transactions(addr_hash, max, start_height));
    if (result) {
        *out_tx_hashes = kth::leak_if_success(*result, std::error_code{});
        return kth::to_c_err(std::error_code{});
    }
    *out_tx_hashes = nullptr;
    return kth::to_c_err(result.error());
}

// ------------------------------------------------------------------

kth_mempool_transaction_list_t kth_chain_sync_mempool_transactions(kth_chain_t chain, kth_payment_address_t address, kth_bool_t use_testnet_rules) {
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(address);
    if (address_cpp) {
        auto txs = safe_chain(chain).get_mempool_transactions(address_cpp.encoded_cashaddr(false), kth::int_to_bool(use_testnet_rules));
        auto ret_txs = kth::leak(txs);
        return static_cast<kth_mempool_transaction_list_t>(ret_txs);
    }
    auto ret_txs = new std::vector<kth::blockchain::mempool_transaction_summary>();
    return static_cast<kth_mempool_transaction_list_t>(ret_txs);
}

kth_transaction_list_mut_t kth_chain_sync_mempool_transactions_from_wallets(kth_chain_t chain, kth_payment_address_list_t addresses, kth_bool_t use_testnet_rules) {
    auto const& addresses_cpp = *static_cast<std::vector<kth::domain::wallet::payment_address> const*>(addresses);
    auto txs = safe_chain(chain).get_mempool_transactions_from_wallets(addresses_cpp, kth::int_to_bool(use_testnet_rules));
    return kth::leak(std::move(txs));
}

// Organizers.
//-------------------------------------------------------------------------

int kth_chain_sync_organize_block(kth_chain_t chain, kth_block_mut_t block) {
    auto& bc = safe_chain(chain);
    auto ec = sync_wait(bc, bc.organize(block_shared(block)));
    return kth::to_c_err(ec);
}

int kth_chain_sync_organize_transaction(kth_chain_t chain, kth_transaction_mut_t transaction) {
    auto& bc = safe_chain(chain);
    auto ec = sync_wait(bc, bc.organize(tx_shared(transaction)));
    return kth::to_c_err(ec);
}

//-------------------------------------------------------------------------

} // extern "C"
