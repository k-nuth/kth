// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/chain_sync.h>
#include "chain_sync_private.hpp"

#include <array>
#include <system_error>

#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/blockchain/interface/block_chain.hpp>

#include <kth/capi/detail/mining_template.hpp>

#include <kth/capi/domain/chain/block_list.h>
#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>

extern "C" {

kth_error_code_t kth_chain_sync_last_height(kth_chain_t chain, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_last_height());
        if ( ! result) {
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        *out_height = result->block;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_header_by_height(kth_chain_t chain, kth_size_t height, kth_header_mut_t* out_header, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_header != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block_header(height));
        if ( ! result) {
            *out_header = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [header, h] = *result;
        auto* header_owned = kth::to_owned(header);
        *out_header = header_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_header_by_hash(kth_chain_t chain, kth_hash_t hash, kth_header_mut_t* out_header, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_header != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block_header(hash_cpp));
        if ( ! result) {
            *out_header = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [header, h] = *result;
        auto* header_owned = kth::to_owned(header);
        *out_header = header_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_by_height(kth_chain_t chain, kth_size_t height, kth_block_mut_t* out_block, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block(height));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_by_hash(kth_chain_t chain, kth_hash_t hash, kth_block_mut_t* out_block, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block(hash_cpp));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_header_byhash_txs_size(
    kth_chain_t chain,
    kth_hash_t hash,
    kth_header_mut_t* out_header,
    uint64_t* out_block_height,
    kth_hash_list_mut_t* out_tx_hashes,
    uint64_t* out_serialized_size
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_header != nullptr);
    KTH_PRECONDITION(out_block_height != nullptr);
    KTH_PRECONDITION(out_tx_hashes != nullptr);
    KTH_PRECONDITION(out_serialized_size != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block_header_txs_size(hash_cpp));
        if ( ! result) {
            *out_header = nullptr;
            *out_block_height = 0;
            *out_tx_hashes = nullptr;
            *out_serialized_size = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [header, block_height, tx_hashes, block_serialized_size] = *result;
        auto header_owned = kth::stage_owned(kth::to_owned(header));
        auto tx_hashes_owned = kth::stage_owned(kth::to_owned(tx_hashes));
        *out_header = header_owned.release();
        *out_block_height = block_height;
        *out_tx_hashes = tx_hashes_owned.release();
        *out_serialized_size = block_serialized_size;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_merkle_block_by_height(
    kth_chain_t chain,
    kth_size_t height,
    kth_merkle_block_mut_t* out_block,
    kth_size_t* out_height
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_merkle_block(height));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_merkle_block_by_hash(kth_chain_t chain, kth_hash_t hash, kth_merkle_block_mut_t* out_block, kth_size_t* out_height) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_merkle_block(hash_cpp));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_compact_block_by_height(
    kth_chain_t chain,
    kth_size_t height,
    kth_compact_block_mut_t* out_block,
    kth_size_t* out_height
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_compact_block(height));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_compact_block_by_hash(
    kth_chain_t chain,
    kth_hash_t hash,
    kth_compact_block_mut_t* out_block,
    kth_size_t* out_height
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_block != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_compact_block(hash_cpp));
        if ( ! result) {
            *out_block = nullptr;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [block, h] = *result;
        auto* block_owned = kth::to_owned(block);
        *out_block = block_owned;
        *out_height = h;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_by_height_timestamp(kth_chain_t chain, kth_size_t height, kth_hash_t* out_hash, uint32_t* out_timestamp) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    KTH_PRECONDITION(out_timestamp != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_block_hash_timestamp(height));
        if ( ! result) {
            kth::copy_c_hash(kth::null_hash, out_hash);
            *out_timestamp = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [hash, timestamp, h] = *result;
        kth::copy_c_hash(hash, out_hash);
        *out_timestamp = timestamp;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_block_hash(kth_chain_t chain, kth_size_t height, kth_hash_t* out_hash) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_hash != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = bc.get_block_hash(height);
        if ( ! result) return kth_ec_not_found;
        kth::copy_c_hash(*result, out_hash);
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_transaction(
    kth_chain_t chain,
    kth_hash_t hash,
    int require_confirmed,
    kth_transaction_mut_t* out_transaction,
    kth_size_t* out_height,
    kth_size_t* out_index
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_transaction != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    KTH_PRECONDITION(out_index != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_transaction(hash_cpp, kth::int_to_bool(require_confirmed)));
        if ( ! result) {
            *out_transaction = nullptr;
            *out_height = 0;
            *out_index = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [transaction, i, h] = *result;
        auto* transaction_owned = kth::to_owned(transaction);
        *out_transaction = transaction_owned;
        *out_height = h;
        *out_index = i;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

kth_error_code_t kth_chain_sync_transaction_position(
    kth_chain_t chain,
    kth_hash_t hash,
    int require_confirmed,
    kth_size_t* out_position,
    kth_size_t* out_height
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out_position != nullptr);
    KTH_PRECONDITION(out_height != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto hash_cpp = std::to_array(hash.hash);
        auto result = kth::capi::sync_wait(bc, bc.fetch_transaction_position(hash_cpp, kth::int_to_bool(require_confirmed)));
        if ( ! result) {
            *out_position = 0;
            *out_height = 0;
            return kth::to_c_err(result.error());
        }
        auto const& [position, height] = *result;
        *out_position = position;
        *out_height = height;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

int kth_chain_sync_organize_block(kth_chain_t chain, kth_block_mut_t block) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(block != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto ec = kth::capi::sync_wait(bc, bc.organize(block_shared(block)));
        return kth::to_c_err(ec);
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return static_cast<int>(kth::capi_invoke(operation));
#else
    return static_cast<int>(operation());
#endif
}

int kth_chain_sync_organize_transaction(kth_chain_t chain, kth_transaction_mut_t transaction) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(transaction != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto ec = kth::capi::sync_wait(bc, bc.organize(tx_shared(transaction)));
        return kth::to_c_err(ec);
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return static_cast<int>(kth::capi_invoke(operation));
#else
    return static_cast<int>(operation());
#endif
}

// `out` and `out_txs` are published together, after both are built; they are
// untouched on an exception and, as before, on a domain error.
kth_error_code_t kth_chain_sync_mining_template(
    kth_chain_t chain,
    uint64_t coinbase_reserve_size,
    kth_mining_template_t* out,
    kth_transaction_list_mut_t* out_txs
) {
    KTH_PRECONDITION(chain != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(out_txs != nullptr);
    auto operation = [&]() noexcept(false) -> kth_error_code_t {
        auto& bc = kth::cpp_ref<kth::blockchain::block_chain>(chain);
        auto result = kth::capi::sync_wait(bc, bc.fetch_mining_template(coinbase_reserve_size));
        if ( ! result) return kth::to_c_err(result.error());
        kth_mining_template_t staged_header{};
        kth_transaction_list_mut_t staged_txs = nullptr;
        kth::capi::fill_mining_template(staged_header, staged_txs, *result);
        *out_txs = staged_txs;
        *out = staged_header;
        return kth_ec_success;
    };
#if defined(KTH_CAPI_EXCEPTION_BARRIERS)
    return kth::capi_invoke(operation);
#else
    return operation();
#endif
}

} // extern "C"
