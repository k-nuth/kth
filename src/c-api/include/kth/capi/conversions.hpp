// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CONVERSIONS_HPP_
#define KTH_CAPI_CONVERSIONS_HPP_

#include <vector>

#include <kth/capi/list_creator.h>
#include <kth/capi/primitives.h>
#include <kth/capi/type_conversions.h>
#include <kth/capi/wallet/primitives.h>

#include <kth/infrastructure/utility/binary.hpp>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/stealth.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/message/compact_block.hpp>
#include <kth/domain/message/get_blocks.hpp>
#include <kth/domain/message/get_headers.hpp>
#include <kth/domain/message/merkle_block.hpp>
#include <kth/domain/message/prefilled_transaction.hpp>
#include <kth/domain/wallet/ec_private.hpp>
#include <kth/domain/wallet/hd_private.hpp>
#include <kth/domain/wallet/ec_public.hpp>
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

// #ifndef __EMSCRIPTEN__
#include <kth/blockchain/interface/safe_chain.hpp>
// #endif

// block conversion functions take const/mut handle types directly. Defined
// in src/chain/block.cpp.
kth::domain::chain::block const& kth_chain_block_const_cpp(kth_block_const_t o);
kth::domain::chain::block&       kth_chain_block_mut_cpp(kth_block_mut_t o);

// binary conversion functions. Defined in src/binary.cpp.
kth::binary const& kth_core_binary_const_cpp(kth_binary_const_t o);
kth::binary&       kth_core_binary_mut_cpp(kth_binary_mut_t o);

// get_blocks conversion functions. Defined in src/chain/get_blocks.cpp.
kth::domain::message::get_blocks const& kth_chain_get_blocks_const_cpp(kth_get_blocks_const_t o);
kth::domain::message::get_blocks&       kth_chain_get_blocks_mut_cpp(kth_get_blocks_mut_t o);

// get_headers conversion functions. Defined in src/chain/get_headers.cpp.
kth::domain::message::get_headers const& kth_chain_get_headers_const_cpp(kth_get_headers_const_t o);
kth::domain::message::get_headers&       kth_chain_get_headers_mut_cpp(kth_get_headers_mut_t o);

// merkle_block conversion functions. Defined in src/chain/merkle_block.cpp.
kth::domain::message::merkle_block const& kth_chain_merkle_block_const_cpp(kth_merkle_block_const_t o);
kth::domain::message::merkle_block&       kth_chain_merkle_block_mut_cpp(kth_merkle_block_mut_t o);

// prefilled_transaction conversion functions. Defined in src/chain/prefilled_transaction.cpp.
kth::domain::message::prefilled_transaction const& kth_chain_prefilled_transaction_const_cpp(kth_prefilled_transaction_const_t o);
kth::domain::message::prefilled_transaction&       kth_chain_prefilled_transaction_mut_cpp(kth_prefilled_transaction_mut_t o);

// prefilled_transaction_list — inline view over the opaque handle, same
// pattern as input_list / output_list above.
inline std::vector<kth::domain::message::prefilled_transaction> const&
kth_chain_prefilled_transaction_list_const_cpp(kth_prefilled_transaction_list_const_t l) {
    return *static_cast<std::vector<kth::domain::message::prefilled_transaction> const*>(l);
}
inline std::vector<kth::domain::message::prefilled_transaction>&
kth_chain_prefilled_transaction_list_mut_cpp(kth_prefilled_transaction_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::message::prefilled_transaction>*>(l);
}

// compact_block conversion functions. Defined in src/chain/compact_block.cpp.
kth::domain::message::compact_block const& kth_chain_compact_block_const_cpp(kth_compact_block_const_t o);
kth::domain::message::compact_block&       kth_chain_compact_block_mut_cpp(kth_compact_block_mut_t o);

// header conversion functions take const/mut handle types directly. Defined
// in src/chain/header.cpp.
kth::domain::chain::header const& kth_chain_header_const_cpp(kth_header_const_t o);
kth::domain::chain::header&       kth_chain_header_mut_cpp(kth_header_mut_t o);

// chain_state is not yet a first-class C-API type but header::accept needs it,
// so we expose lightweight inline conversions over the opaque handle.
inline kth::domain::chain::chain_state const& kth_chain_chain_state_const_cpp(kth_chain_state_const_t o) {
    return *static_cast<kth::domain::chain::chain_state const*>(o);
}
inline kth::domain::chain::chain_state& kth_chain_chain_state_mut_cpp(kth_chain_state_mut_t o) {
    return *static_cast<kth::domain::chain::chain_state*>(o);
}

// transaction conversion functions take const/mut handle types directly.
// Defined in src/chain/transaction.cpp.
kth::domain::chain::transaction const& kth_chain_transaction_const_cpp(kth_transaction_const_t o);
kth::domain::chain::transaction&       kth_chain_transaction_mut_cpp(kth_transaction_mut_t o);

// std::vector<std::array<uint8_t, 33>> matches the existing
// `ec_compressed_cpp_t` alias used by the legacy converters further down.
// We spell it out here to keep the const/mut shim self-contained.
inline std::vector<std::array<uint8_t, 33>> const&
kth_wallet_ec_compressed_list_const_cpp(kth_ec_compressed_list_const_t l) {
    return *static_cast<std::vector<std::array<uint8_t, 33>> const*>(l);
}
inline std::vector<std::array<uint8_t, 33>>&
kth_wallet_ec_compressed_list_mut_cpp(kth_ec_compressed_list_mut_t l) {
    return *static_cast<std::vector<std::array<uint8_t, 33>>*>(l);
}

// token_data is not migrated to const/mut yet. The legacy
// KTH_CONV_DECLARE(chain, kth_token_data_t, ...) declares the converter
// taking a non-const void*; the migrated chain/output API needs the const
// view, so we add an inline shim here.
inline kth::domain::chain::token_data_t const&
kth_chain_token_data_const_cpp(kth_token_data_const_t o) {
    return *static_cast<kth::domain::chain::token_data_t const*>(o);
}
inline kth::domain::chain::token_data_t&
kth_chain_token_data_mut_cpp(kth_token_data_mut_t o) {
    return *static_cast<kth::domain::chain::token_data_t*>(o);
}

// data_stack — vector of variable-length byte buffers (the script runtime
// stack). Exposed only as an opaque handle for now; ownership and element
// access live on the user side until we wire a proper data_stack module.
inline std::vector<std::vector<uint8_t>> const&
kth_chain_data_stack_const_cpp(kth_data_stack_const_t s) {
    return *static_cast<std::vector<std::vector<uint8_t>> const*>(s);
}
inline std::vector<std::vector<uint8_t>>&
kth_chain_data_stack_mut_cpp(kth_data_stack_mut_t s) {
    return *static_cast<std::vector<std::vector<uint8_t>>*>(s);
}
// Legacy `KTH_CONV_DECLARE(chain, kth_input_t, ...)` removed: the const/mut
// converters below (defined out-of-line in src/chain/input.cpp) cover both
// modern `kth_input_const_t` callers and legacy `kth_input_t` callers via
// the implicit `void* → void const*` qualification conversion. The macro
// expansion in src/chain/input_list.cpp depends on this fallthrough.
// input conversion functions take const/mut handle types directly. Defined
// in src/chain/input.cpp.
kth::domain::chain::input const& kth_chain_input_const_cpp(kth_input_const_t o);
kth::domain::chain::input&       kth_chain_input_mut_cpp(kth_input_mut_t o);

// input_list / output_list — overloads of the legacy `*_list_const_cpp`
// converters that accept the new const/mut handle aliases used by the
// migrated chain/transaction binding. The legacy void* converter declared
// further below by KTH_LIST_DECLARE_CONVERTERS keeps working for callers
// that haven't migrated yet.
inline std::vector<kth::domain::chain::input> const&
kth_chain_input_list_const_cpp(kth_input_list_const_t l) {
    return *static_cast<std::vector<kth::domain::chain::input> const*>(l);
}
inline std::vector<kth::domain::chain::input>&
kth_chain_input_list_mut_cpp(kth_input_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::chain::input>*>(l);
}
inline std::vector<kth::domain::chain::output> const&
kth_chain_output_list_const_cpp(kth_output_list_const_t l) {
    return *static_cast<std::vector<kth::domain::chain::output> const*>(l);
}
inline std::vector<kth::domain::chain::output>&
kth_chain_output_list_mut_cpp(kth_output_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::chain::output>*>(l);
}
inline std::vector<kth::domain::chain::transaction> const&
kth_chain_transaction_list_const_cpp(kth_transaction_list_const_t l) {
    return *static_cast<std::vector<kth::domain::chain::transaction> const*>(l);
}
inline std::vector<kth::domain::chain::transaction>&
kth_chain_transaction_list_mut_cpp(kth_transaction_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::chain::transaction>*>(l);
}
// output conversion functions take const/mut handle types directly. Defined
// in src/chain/output.cpp.
kth::domain::chain::output const& kth_chain_output_const_cpp(kth_output_const_t o);
kth::domain::chain::output&       kth_chain_output_mut_cpp(kth_output_mut_t o);
// output_point conversion functions take const/mut handle types directly.
// Defined in src/chain/output_point.cpp.
kth::domain::chain::output_point const& kth_chain_output_point_const_cpp(kth_output_point_const_t o);
kth::domain::chain::output_point&       kth_chain_output_point_mut_cpp(kth_output_point_mut_t o);
// script conversion functions take const/mut handle types directly. Defined
// in src/chain/script.cpp.
kth::domain::chain::script const& kth_chain_script_const_cpp(kth_script_const_t o);
kth::domain::chain::script&       kth_chain_script_mut_cpp(kth_script_mut_t o);
// Legacy `KTH_CONV_DECLARE(chain, kth_transaction_t, ...)` removed: the
// const/mut converters above (defined out-of-line in src/chain/transaction.cpp)
// cover both modern `kth_transaction_const_t` callers and legacy `kth_transaction_t`
// callers via the implicit `void* → void const*` qualification conversion.
// point conversion functions take const/mut handle types directly. Defined
// in src/chain/point.cpp.
kth::domain::chain::point const& kth_chain_point_const_cpp(kth_point_const_t o);
kth::domain::chain::point&       kth_chain_point_mut_cpp(kth_point_mut_t o);
KTH_CONV_DECLARE(chain, kth_utxo_t, kth::domain::chain::utxo, utxo)
KTH_CONV_DECLARE(chain, kth_token_data_t, kth::domain::chain::token_data_t, token_data)

// #ifndef __EMSCRIPTEN__
KTH_CONV_DECLARE(chain, kth_mempool_transaction_t, kth::blockchain::mempool_transaction_summary, mempool_transaction)
// #endif

// #ifndef __EMSCRIPTEN__
KTH_CONV_DECLARE(chain, kth_history_compact_t, kth::domain::chain::history_compact, history_compact)
// #endif

KTH_CONV_DECLARE(chain, kth_stealth_compact_t, kth::domain::chain::stealth_compact, stealth_compact)

KTH_LIST_DECLARE_CONSTRUCT_FROM_CPP(chain, kth_utxo_list_t, kth::domain::chain::utxo, utxo_list)
// hash_list — inline converters and construct_from_cpp.
inline std::vector<kth::hash_digest> const&
kth_core_hash_list_const_cpp(kth_hash_list_const_t l) {
    return *static_cast<std::vector<kth::hash_digest> const*>(l);
}
inline std::vector<kth::hash_digest>&
kth_core_hash_list_mut_cpp(kth_hash_list_mut_t l) {
    return *static_cast<std::vector<kth::hash_digest>*>(l);
}
inline kth_hash_list_mut_t kth_core_hash_list_construct_from_cpp(std::vector<kth::hash_digest>& l) {
    return &l;
}
// u64_list — inline converters. Covers vector<uint64_t>, vector<size_t>,
// vector<unsigned long> because those canonicalize to the same underlying
// integer type on 64-bit platforms. Used by
// `kth::domain::message::compact_block::short_ids` and by
// `kth_chain_block_locator_heights`.
inline std::vector<uint64_t> const&
kth_core_u64_list_const_cpp(kth_u64_list_const_t l) {
    return *static_cast<std::vector<uint64_t> const*>(l);
}
inline std::vector<uint64_t>&
kth_core_u64_list_mut_cpp(kth_u64_list_mut_t l) {
    return *static_cast<std::vector<uint64_t>*>(l);
}
KTH_LIST_DECLARE_CONVERTERS(chain, kth_utxo_list_t, kth::domain::chain::utxo, utxo_list)

// operation conversion functions. Defined in src/chain/operation.cpp.
kth::domain::machine::operation const& kth_chain_operation_const_cpp(kth_operation_const_t o);
kth::domain::machine::operation&       kth_chain_operation_mut_cpp(kth_operation_mut_t o);

// operation_list — inline converters (replaces macro-based KTH_LIST_DECLARE_CONVERTERS).
inline std::vector<kth::domain::machine::operation> const&
kth_chain_operation_list_const_cpp(kth_operation_list_const_t l) {
    return *static_cast<std::vector<kth::domain::machine::operation> const*>(l);
}
inline std::vector<kth::domain::machine::operation>&
kth_chain_operation_list_mut_cpp(kth_operation_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::machine::operation>*>(l);
}

// Wallet.
// ------------------------------------------------------------------------------------

// ec_private conversion functions. Defined in src/wallet/ec_private.cpp.
kth::domain::wallet::ec_private const& kth_wallet_ec_private_const_cpp(kth_ec_private_const_t o);
kth::domain::wallet::ec_private&       kth_wallet_ec_private_mut_cpp(kth_ec_private_mut_t o);

// ec_public conversion functions. Defined in src/wallet/ec_public.cpp.
kth::domain::wallet::ec_public const& kth_wallet_ec_public_const_cpp(kth_ec_public_const_t o);
kth::domain::wallet::ec_public&       kth_wallet_ec_public_mut_cpp(kth_ec_public_mut_t o);

// hd_public conversion functions. Defined in src/wallet/hd_public.cpp.
kth::domain::wallet::hd_public const& kth_wallet_hd_public_const_cpp(kth_hd_public_const_t o);
kth::domain::wallet::hd_public&       kth_wallet_hd_public_mut_cpp(kth_hd_public_mut_t o);

// hd_private conversion functions. Defined in src/wallet/hd_private.cpp.
kth::domain::wallet::hd_private const& kth_wallet_hd_private_const_cpp(kth_hd_private_const_t o);
kth::domain::wallet::hd_private&       kth_wallet_hd_private_mut_cpp(kth_hd_private_mut_t o);

// payment_address conversion functions. Defined in src/wallet/payment_address.cpp.
kth::domain::wallet::payment_address const& kth_wallet_payment_address_const_cpp(kth_payment_address_const_t o);
kth::domain::wallet::payment_address&       kth_wallet_payment_address_mut_cpp(kth_payment_address_mut_t o);

KTH_CONV_DECLARE(wallet, kth_wallet_data_t, kth::domain::wallet::wallet_data, wallet_data)

// payment_address_list — inline converters for the generated list binding.
inline std::vector<kth::domain::wallet::payment_address> const&
kth_wallet_payment_address_list_const_cpp(kth_payment_address_list_const_t l) {
    return *static_cast<std::vector<kth::domain::wallet::payment_address> const*>(l);
}
inline std::vector<kth::domain::wallet::payment_address>&
kth_wallet_payment_address_list_mut_cpp(kth_payment_address_list_mut_t l) {
    return *static_cast<std::vector<kth::domain::wallet::payment_address>*>(l);
}


// Core.
// ------------------------------------------------------------------------------------

// string_list — inline construct_from_cpp for wallet_data.cpp callers.
inline kth_string_list_mut_t kth_core_string_list_construct_from_cpp(std::vector<std::string>& l) {
    return &l;
}


// VM.
// ------------------------------------------------------------------------------------
// KTH_CONV_DECLARE(vm, kth_program_t, kth::domain::machine::program, program)
KTH_CONV_DECLARE_JUST_CONST(vm, kth_program_const_t, kth::domain::machine::program, program)
KTH_CONV_DECLARE_JUST_MUTABLE(vm, kth_program_t, kth::domain::machine::program, program)


// TODO: this is not so good
using ec_compressed_cpp_t = std::array<uint8_t, KTH_EC_COMPRESSED_SIZE>;
KTH_LIST_DECLARE_CONVERTERS(wallet, kth_ec_compressed_list_t, ec_compressed_cpp_t, ec_compressed_list)

#endif /* KTH_CAPI_CONVERSIONS_HPP_ */
