// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_HANDLES_H_
#define KTH_CAPI_HANDLES_H_

#include <stdint.h>
#include <stdlib.h>

#include <kth/capi/visibility.h>

#define KTH_PRECONDITION(expr) do { if (!(expr)) { abort(); } } while(0)

// Marks a function whose return value (or one of its out-parameters) transfers
// ownership to the caller. The caller is then responsible for releasing the
// returned object via the matching destructor (`kth_<group>_<obj>_destruct`
// for handles, `kth_core_destruct_array` for raw byte buffers,
// `kth_core_destruct_string` for C strings).
//
// On compilers that support it, this also raises a warning when the result of
// such a call is silently discarded, which would otherwise leak memory.
#if defined(__GNUC__) || defined(__clang__)
#define KTH_OWNED __attribute__((warn_unused_result))
#else
#define KTH_OWNED
#endif

// Parameter-level companion to KTH_OWNED. Marks an out-parameter that, on
// successful completion, is filled with an object the caller now owns and
// must release with the matching destructor. The marker is purely informative
// at the C level (it expands to nothing) but is visible at the declaration
// site and is greppable for non-C language backends.
#define KTH_OUT_OWNED

#ifdef __cplusplus
extern "C" {
#endif

// Releases an owned byte buffer returned by a C-API function (e.g. the
// payload of `kth_chain_*_to_data`). Internally a thin wrapper around
// free(); kept as a dedicated entry point so the API does not commit to a
// specific allocator and so language backends can wire it through their
// SafeHandle / Drop equivalents.
KTH_EXPORT
void kth_core_destruct_array(uint8_t* arr);

// Releases an owned C string returned by a C-API function. Same rationale
// as kth_core_destruct_array.
KTH_EXPORT
void kth_core_destruct_string(char* str);

#define KTH_BITCOIN_SHORT_HASH_SIZE 20
#define KTH_BITCOIN_HASH_SIZE 32
#define KTH_BITCOIN_LONG_HASH_SIZE 64
#define KTH_BITCOIN_ENCRYPTED_SEED_SIZE 96

#define KTH_BITCOIN_MINIMUM_SEED_BITS 128
#define KTH_BITCOIN_BYTE_BITS 8
#define KTH_BITCOIN_MINIMUM_SEED_SIZE (KTH_BITCOIN_MINIMUM_SEED_BITS / KTH_BITCOIN_BYTE_BITS)

#define KTH_WALLET_HD_PUBLIC_MAINNET 76067358
#define KTH_WALLET_HD_PUBLIC_TESTNET 70617039
#define KTH_WALLET_HD_PRIVATE_MAINNET 326702167824577054
#define KTH_WALLET_HD_PRIVATE_TESTNET 303293221666392015

typedef int kth_bool_t;

#if defined(__EMSCRIPTEN__)
typedef uint32_t kth_size_t;    // It is std::size_t in the C++ code.
#else
typedef uint64_t kth_size_t;
#endif

#if defined(_WIN32)
typedef wchar_t kth_char_t;
#else
typedef char kth_char_t;
#endif

typedef void* kth_node_t;
typedef void* kth_chain_t;
typedef void* kth_p2p_t;

//typedef struct kth_outputpoint_t {
//    uint8_t* hash;
//    uint32_t index;
//} kth_outputpoint_t;


// TODO(fernando): check if we can encapsulate the pointer into a struct to make them more "type safe"
typedef void* kth_block_mut_t;
typedef void const* kth_block_const_t;
typedef void* kth_block_list_mut_t;
typedef void const* kth_block_list_const_t;
typedef void* kth_compact_block_mut_t;
typedef void const* kth_compact_block_const_t;
typedef void* kth_double_spend_proof_mut_t;
typedef void const* kth_double_spend_proof_const_t;
typedef void* kth_double_spend_proof_spender_mut_t;
typedef void const* kth_double_spend_proof_spender_const_t;
typedef void* kth_header_mut_t;
typedef void const* kth_header_const_t;
typedef void* kth_chain_state_mut_t;
typedef void const* kth_chain_state_const_t;
typedef void* kth_history_compact_mut_t;
typedef void const* kth_history_compact_const_t;
typedef void* kth_history_compact_list_mut_t;
typedef void const* kth_history_compact_list_const_t;
typedef void* kth_input_mut_t;
typedef void const* kth_input_const_t;
typedef void* kth_input_list_mut_t;
typedef void const* kth_input_list_const_t;
typedef void* kth_utxo_list_mut_t;
typedef void const* kth_utxo_list_const_t;
typedef void* kth_inputpoint_t;
typedef void* kth_merkle_block_mut_t;
typedef void const* kth_merkle_block_const_t;
typedef void* kth_prefilled_transaction_mut_t;
typedef void const* kth_prefilled_transaction_const_t;
typedef void* kth_prefilled_transaction_list_mut_t;
typedef void const* kth_prefilled_transaction_list_const_t;
typedef void* kth_script_mut_t;
typedef void const* kth_script_const_t;
typedef void* kth_token_data_t;
typedef void* kth_token_data_mut_t;
typedef void const* kth_token_data_const_t;

typedef void* kth_operation_list_t;
typedef void* kth_operation_list_mut_t;
typedef void const* kth_operation_list_const_t;
typedef void* kth_operation_t;
typedef void* kth_operation_mut_t;
typedef void const* kth_operation_const_t;

typedef void* kth_output_list_mut_t;
typedef void const* kth_output_list_const_t;
typedef void* kth_output_mut_t;
typedef void const* kth_output_const_t;
typedef void* kth_output_point_mut_t;
typedef void const* kth_output_point_const_t;
typedef void* kth_utxo_mut_t;
typedef void const* kth_utxo_const_t;
typedef void* kth_point_mut_t;
typedef void const* kth_point_const_t;
typedef void* kth_point_list_mut_t;
typedef void const* kth_point_list_const_t;
typedef void* kth_output_point_list_mut_t;
typedef void const* kth_output_point_list_const_t;
typedef void* kth_transaction_mut_t;
typedef void const* kth_transaction_const_t;
typedef void* kth_transaction_list_mut_t;
typedef void const* kth_transaction_list_const_t;
typedef void* kth_mempool_transaction_t;
typedef void* kth_mempool_transaction_list_t;
typedef void* kth_get_blocks_mut_t;
typedef void const* kth_get_blocks_const_t;
typedef void* kth_get_headers_mut_t;
typedef void const* kth_get_headers_const_t;
typedef void* kth_payment_address_t;
typedef void* kth_payment_address_mut_t;
typedef void const* kth_payment_address_const_t;
typedef void* kth_payment_address_list_t;
typedef void* kth_payment_address_list_mut_t;
typedef void const* kth_payment_address_list_const_t;
typedef void* kth_binary_mut_t;
typedef void const* kth_binary_const_t;
typedef void* kth_stealth_compact_mut_t;
typedef void const* kth_stealth_compact_const_t;
typedef void* kth_stealth_compact_list_mut_t;
typedef void const* kth_stealth_compact_list_const_t;
typedef void* kth_hash_list_mut_t;
typedef void const* kth_hash_list_const_t;
typedef void* kth_string_list_mut_t;
typedef void const* kth_string_list_const_t;
typedef void* kth_double_list_mut_t;
typedef void const* kth_double_list_const_t;
typedef void* kth_u32_list_mut_t;
typedef void const* kth_u32_list_const_t;
typedef void* kth_u64_list_mut_t;
typedef void const* kth_u64_list_const_t;
typedef void* kth_bool_list_mut_t;
typedef void const* kth_bool_list_const_t;

typedef void* kth_wallet_data_mut_t;
typedef void const* kth_wallet_data_const_t;

typedef void* kth_stealth_address_mut_t;
typedef void const* kth_stealth_address_const_t;
typedef void* kth_bitcoin_uri_mut_t;
typedef void const* kth_bitcoin_uri_const_t;

// BIP39 wordlist handles. `dictionary` is `std::array<char const*, 2048>`
// and `dictionary_list` is `std::vector<dictionary const*>`; callers
// obtain handles through the `kth_wallet_language_*` factories
// (borrowed views into static storage — do not destruct).
typedef void* kth_dictionary_mut_t;
typedef void const* kth_dictionary_const_t;
typedef void* kth_dictionary_list_mut_t;
typedef void const* kth_dictionary_list_const_t;

typedef void* kth_ec_compressed_list_t;
typedef void* kth_ec_compressed_list_mut_t;
typedef void const* kth_ec_compressed_list_const_t;

// Vector of byte buffers — used by Bitcoin script's runtime stack and by
// `to_pay_multisig_pattern` for the signature variant. Variable-length
// elements (`kth_byte_buffer_t` in spirit) are exposed through the
// `kth_core_data_stack_*` module.
typedef void* kth_data_stack_mut_t;
typedef void const* kth_data_stack_const_t;


// VM
typedef void* kth_metrics_mut_t;
typedef void const* kth_metrics_const_t;
typedef void* kth_program_mut_t;
typedef void const* kth_program_const_t;
// Bounded 4-byte script integer (`kth::infrastructure::machine::number`).
typedef void* kth_number_mut_t;
typedef void const* kth_number_const_t;
// Unbounded big-integer for BCH 2025 (`kth::infrastructure::machine::big_number`).
typedef void* kth_big_number_mut_t;
typedef void const* kth_big_number_const_t;
typedef void* kth_debug_snapshot_mut_t;
typedef void const* kth_debug_snapshot_const_t;
typedef void* kth_debug_snapshot_list_mut_t;
typedef void const* kth_debug_snapshot_list_const_t;
typedef void* kth_function_table_mut_t;
typedef void const* kth_function_table_const_t;
typedef void* kth_script_execution_context_mut_t;
typedef void const* kth_script_execution_context_const_t;


// Hash structs

typedef struct kth_shorthash_t {
    uint8_t hash[KTH_BITCOIN_SHORT_HASH_SIZE];  //kth::hash_size
} kth_shorthash_t;

typedef struct kth_hash_t {
    uint8_t hash[KTH_BITCOIN_HASH_SIZE];        //kth::hash_size
} kth_hash_t;

typedef struct kth_longhash_t {
    uint8_t hash[KTH_BITCOIN_LONG_HASH_SIZE];   //kth::long_hash_size
} kth_longhash_t;

typedef struct kth_encrypted_seed_t {
    uint8_t hash[KTH_BITCOIN_ENCRYPTED_SEED_SIZE];
} kth_encrypted_seed_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_HANDLES_H_ */
