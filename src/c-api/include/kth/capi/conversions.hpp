// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_CONVERSIONS_HPP_
#define KTH_CAPI_CONVERSIONS_HPP_

#include <vector>

#include <kth/capi/primitives.h>
#include <kth/capi/wallet/primitives.h>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/output.hpp>
#include <kth/domain/chain/output_point.hpp>
#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/stealth.hpp>
#include <kth/domain/chain/block.hpp>
#include <kth/domain/chain/header.hpp>
#include <kth/domain/chain/transaction.hpp>
// #include <kth/domain/wallet/ec_public.hpp>
#include <kth/domain/wallet/transaction_functions.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

// #ifndef __EMSCRIPTEN__
#include <kth/blockchain/interface/safe_chain.hpp>
// #endif

// AUTO-GENERATED-START — do not edit this section manually
// Extra conversion function declarations
kth::domain::chain::chain_state const& kth_chain_chain_state_const_cpp(kth_chain_state_const_t);

// Class conversion function declarations
kth::domain::chain::block& kth_chain_block_cpp(kth_block_mut_t);
kth::domain::chain::block const& kth_chain_block_const_cpp(kth_block_const_t);
kth::domain::chain::header& kth_chain_header_cpp(kth_header_mut_t);
kth::domain::chain::header const& kth_chain_header_const_cpp(kth_header_const_t);
kth::domain::chain::point& kth_chain_point_cpp(kth_point_mut_t);
kth::domain::chain::point const& kth_chain_point_const_cpp(kth_point_const_t);
kth::domain::chain::script& kth_chain_script_cpp(kth_script_mut_t);
kth::domain::chain::script const& kth_chain_script_const_cpp(kth_script_const_t);
kth::domain::chain::output& kth_chain_output_cpp(kth_output_mut_t);
kth::domain::chain::output const& kth_chain_output_const_cpp(kth_output_const_t);
kth::domain::chain::input& kth_chain_input_cpp(kth_input_mut_t);
kth::domain::chain::input const& kth_chain_input_const_cpp(kth_input_const_t);
kth::domain::chain::output_point& kth_chain_output_point_cpp(kth_output_point_mut_t);
kth::domain::chain::output_point const& kth_chain_output_point_const_cpp(kth_output_point_const_t);
kth::domain::chain::transaction& kth_chain_transaction_cpp(kth_transaction_mut_t);
kth::domain::chain::transaction const& kth_chain_transaction_const_cpp(kth_transaction_const_t);
kth::domain::chain::utxo& kth_chain_utxo_cpp(kth_utxo_mut_t);
kth::domain::chain::utxo const& kth_chain_utxo_const_cpp(kth_utxo_const_t);

// List conversion function declarations
std::vector<kth::domain::chain::transaction> const& kth_chain_transaction_list_const_cpp(kth_transaction_list_const_t);
std::vector<kth::domain::chain::transaction>& kth_chain_transaction_list_cpp(kth_transaction_list_mut_t);
kth_transaction_list_mut_t kth_chain_transaction_list_construct_from_cpp(std::vector<kth::domain::chain::transaction>&);
void const* kth_chain_transaction_list_construct_from_cpp(std::vector<kth::domain::chain::transaction> const&);
std::vector<kth::domain::chain::input> const& kth_chain_input_list_const_cpp(kth_input_list_const_t);
std::vector<kth::domain::chain::input>& kth_chain_input_list_cpp(kth_input_list_mut_t);
kth_input_list_mut_t kth_chain_input_list_construct_from_cpp(std::vector<kth::domain::chain::input>&);
void const* kth_chain_input_list_construct_from_cpp(std::vector<kth::domain::chain::input> const&);
std::vector<kth::domain::chain::output> const& kth_chain_output_list_const_cpp(kth_output_list_const_t);
std::vector<kth::domain::chain::output>& kth_chain_output_list_cpp(kth_output_list_mut_t);
kth_output_list_mut_t kth_chain_output_list_construct_from_cpp(std::vector<kth::domain::chain::output>&);
void const* kth_chain_output_list_construct_from_cpp(std::vector<kth::domain::chain::output> const&);
std::vector<kth::domain::chain::point> const& kth_chain_point_list_const_cpp(kth_point_list_const_t);
std::vector<kth::domain::chain::point>& kth_chain_point_list_cpp(kth_point_list_mut_t);
kth_point_list_mut_t kth_chain_point_list_construct_from_cpp(std::vector<kth::domain::chain::point>&);
void const* kth_chain_point_list_construct_from_cpp(std::vector<kth::domain::chain::point> const&);
std::vector<kth::domain::chain::block> const& kth_chain_block_list_const_cpp(kth_block_list_const_t);
std::vector<kth::domain::chain::block>& kth_chain_block_list_cpp(kth_block_list_mut_t);
kth_block_list_mut_t kth_chain_block_list_construct_from_cpp(std::vector<kth::domain::chain::block>&);
void const* kth_chain_block_list_construct_from_cpp(std::vector<kth::domain::chain::block> const&);
std::vector<kth::domain::chain::output_point> const& kth_chain_output_point_list_const_cpp(kth_output_point_list_const_t);
std::vector<kth::domain::chain::output_point>& kth_chain_output_point_list_cpp(kth_output_point_list_mut_t);
kth_output_point_list_mut_t kth_chain_output_point_list_construct_from_cpp(std::vector<kth::domain::chain::output_point>&);
void const* kth_chain_output_point_list_construct_from_cpp(std::vector<kth::domain::chain::output_point> const&);
std::vector<kth::domain::chain::utxo> const& kth_chain_utxo_list_const_cpp(kth_utxo_list_const_t);
std::vector<kth::domain::chain::utxo>& kth_chain_utxo_list_cpp(kth_utxo_list_mut_t);
kth_utxo_list_mut_t kth_chain_utxo_list_construct_from_cpp(std::vector<kth::domain::chain::utxo>&);
void const* kth_chain_utxo_list_construct_from_cpp(std::vector<kth::domain::chain::utxo> const&);
std::vector<kth::domain::chain::history_compact> const& kth_chain_history_compact_list_const_cpp(kth_history_compact_list_const_t);
std::vector<kth::domain::chain::history_compact>& kth_chain_history_compact_list_cpp(kth_history_compact_list_mut_t);
kth_history_compact_list_mut_t kth_chain_history_compact_list_construct_from_cpp(std::vector<kth::domain::chain::history_compact>&);
void const* kth_chain_history_compact_list_construct_from_cpp(std::vector<kth::domain::chain::history_compact> const&);
std::vector<kth::blockchain::mempool_transaction_summary> const& kth_chain_mempool_transaction_list_const_cpp(kth_mempool_transaction_list_const_t);
std::vector<kth::blockchain::mempool_transaction_summary>& kth_chain_mempool_transaction_list_cpp(kth_mempool_transaction_list_mut_t);
kth_mempool_transaction_list_mut_t kth_chain_mempool_transaction_list_construct_from_cpp(std::vector<kth::blockchain::mempool_transaction_summary>&);
void const* kth_chain_mempool_transaction_list_construct_from_cpp(std::vector<kth::blockchain::mempool_transaction_summary> const&);
std::vector<kth::domain::chain::stealth_compact> const& kth_chain_stealth_compact_list_const_cpp(kth_stealth_compact_list_const_t);
std::vector<kth::domain::chain::stealth_compact>& kth_chain_stealth_compact_list_cpp(kth_stealth_compact_list_mut_t);
kth_stealth_compact_list_mut_t kth_chain_stealth_compact_list_construct_from_cpp(std::vector<kth::domain::chain::stealth_compact>&);
void const* kth_chain_stealth_compact_list_construct_from_cpp(std::vector<kth::domain::chain::stealth_compact> const&);
std::vector<kth::domain::machine::operation> const& kth_chain_operation_list_const_cpp(kth_operation_list_const_t);
std::vector<kth::domain::machine::operation>& kth_chain_operation_list_cpp(kth_operation_list_mut_t);
kth_operation_list_mut_t kth_chain_operation_list_construct_from_cpp(std::vector<kth::domain::machine::operation>&);
void const* kth_chain_operation_list_construct_from_cpp(std::vector<kth::domain::machine::operation> const&);
std::vector<kth_size_t> const& kth_chain_block_indexes_const_cpp(kth_block_indexes_const_t);
std::vector<kth_size_t>& kth_chain_block_indexes_cpp(kth_block_indexes_mut_t);
kth_block_indexes_mut_t kth_chain_block_indexes_construct_from_cpp(std::vector<kth_size_t>&);
void const* kth_chain_block_indexes_construct_from_cpp(std::vector<kth_size_t> const&);
std::vector<kth::hash_digest> const& kth_core_hash_list_const_cpp(kth_hash_list_const_t);
std::vector<kth::hash_digest>& kth_core_hash_list_cpp(kth_hash_list_mut_t);
kth_hash_list_mut_t kth_core_hash_list_construct_from_cpp(std::vector<kth::hash_digest>&);
void const* kth_core_hash_list_construct_from_cpp(std::vector<kth::hash_digest> const&);
std::vector<double> const& kth_core_double_list_const_cpp(kth_double_list_const_t);
std::vector<double>& kth_core_double_list_cpp(kth_double_list_mut_t);
kth_double_list_mut_t kth_core_double_list_construct_from_cpp(std::vector<double>&);
void const* kth_core_double_list_construct_from_cpp(std::vector<double> const&);
std::vector<uint32_t> const& kth_core_u32_list_const_cpp(kth_u32_list_const_t);
std::vector<uint32_t>& kth_core_u32_list_cpp(kth_u32_list_mut_t);
kth_u32_list_mut_t kth_core_u32_list_construct_from_cpp(std::vector<uint32_t>&);
void const* kth_core_u32_list_construct_from_cpp(std::vector<uint32_t> const&);
std::vector<uint64_t> const& kth_core_u64_list_const_cpp(kth_u64_list_const_t);
std::vector<uint64_t>& kth_core_u64_list_cpp(kth_u64_list_mut_t);
kth_u64_list_mut_t kth_core_u64_list_construct_from_cpp(std::vector<uint64_t>&);
void const* kth_core_u64_list_construct_from_cpp(std::vector<uint64_t> const&);
std::vector<std::string> const& kth_core_string_list_const_cpp(kth_string_list_const_t);
std::vector<std::string>& kth_core_string_list_cpp(kth_string_list_mut_t);
kth_string_list_mut_t kth_core_string_list_construct_from_cpp(std::vector<std::string>&);
void const* kth_core_string_list_construct_from_cpp(std::vector<std::string> const&);
// AUTO-GENERATED-END

// Not yet migrated — conversion function declarations (manual)
kth::domain::chain::token_data_t const& kth_chain_token_data_const_cpp(kth_token_data_const_t);
kth::domain::chain::token_data_t& kth_chain_token_data_cpp(kth_token_data_t);
kth::blockchain::mempool_transaction_summary const& kth_chain_mempool_transaction_const_cpp(kth_mempool_transaction_t);
kth::blockchain::mempool_transaction_summary const& kth_chain_mempool_transaction_const_cpp(kth_mempool_transaction_const_t);
kth::blockchain::mempool_transaction_summary& kth_chain_mempool_transaction_cpp(kth_mempool_transaction_t);
kth::domain::chain::history_compact const& kth_chain_history_compact_const_cpp(kth_history_compact_t);
kth::domain::chain::history_compact const& kth_chain_history_compact_const_cpp(kth_history_compact_const_t);
kth::domain::chain::history_compact& kth_chain_history_compact_cpp(kth_history_compact_t);
kth::domain::chain::stealth_compact const& kth_chain_stealth_compact_const_cpp(kth_stealth_compact_t);
kth::domain::chain::stealth_compact const& kth_chain_stealth_compact_const_cpp(kth_stealth_compact_const_t);
kth::domain::chain::stealth_compact& kth_chain_stealth_compact_cpp(kth_stealth_compact_t);
kth::domain::machine::operation const& kth_chain_operation_const_cpp(kth_operation_const_t);
kth::domain::machine::operation const& kth_chain_operation_const_cpp(kth_operation_t);  // legacy overload
kth::domain::machine::operation& kth_chain_operation_cpp(kth_operation_t);

// Wallet.
// ------------------------------------------------------------------------------------
kth::domain::wallet::raw_output const& kth_wallet_raw_output_const_cpp(kth_raw_output_t);
kth::domain::wallet::raw_output& kth_wallet_raw_output_cpp(kth_raw_output_t);
kth::domain::wallet::payment_address const& kth_wallet_payment_address_const_cpp(kth_payment_address_t);
kth::domain::wallet::payment_address& kth_wallet_payment_address_cpp(kth_payment_address_t);
kth::domain::wallet::ec_private const& kth_wallet_ec_private_const_cpp(kth_ec_private_t);
kth::domain::wallet::ec_private& kth_wallet_ec_private_cpp(kth_ec_private_t);
kth::domain::wallet::ec_public const& kth_wallet_ec_public_const_cpp(kth_ec_public_t);
kth::domain::wallet::ec_public& kth_wallet_ec_public_cpp(kth_ec_public_t);
kth::domain::wallet::wallet_data const& kth_wallet_wallet_data_const_cpp(kth_wallet_data_t);
kth::domain::wallet::wallet_data& kth_wallet_wallet_data_cpp(kth_wallet_data_t);

std::vector<kth::domain::wallet::raw_output> const& kth_wallet_raw_output_list_const_cpp(kth_raw_output_list_t l);
std::vector<kth::domain::wallet::raw_output>& kth_wallet_raw_output_list_cpp(kth_raw_output_list_t l);

kth_payment_address_list_t kth_wallet_payment_address_list_construct_from_cpp(std::vector<kth::domain::wallet::payment_address>& l);
void const* kth_wallet_payment_address_list_construct_from_cpp(std::vector<kth::domain::wallet::payment_address> const& l);
std::vector<kth::domain::wallet::payment_address> const& kth_wallet_payment_address_list_const_cpp(kth_payment_address_list_t l);
std::vector<kth::domain::wallet::payment_address>& kth_wallet_payment_address_list_cpp(kth_payment_address_list_t l);


// VM.
// ------------------------------------------------------------------------------------
kth::domain::machine::program const& kth_vm_program_const_cpp(kth_program_const_t);
kth::domain::machine::program& kth_vm_program_cpp(kth_program_t);

// TODO: this is not so good
using ec_compressed_cpp_t = std::array<uint8_t, KTH_EC_COMPRESSED_SIZE>;
std::vector<ec_compressed_cpp_t> const& kth_wallet_ec_compressed_list_const_cpp(kth_ec_compressed_list_t l);
std::vector<ec_compressed_cpp_t>& kth_wallet_ec_compressed_list_cpp(kth_ec_compressed_list_t l);

#endif /* KTH_CAPI_CONVERSIONS_HPP_ */
