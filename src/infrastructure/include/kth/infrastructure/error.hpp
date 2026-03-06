// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ERROR_HPP
#define KTH_INFRASTRUCTURE_ERROR_HPP

#include <string>
#include <system_error>
#include <unordered_map>


#if defined(ASIO_STANDALONE)
#include <asio/error_code.hpp>
#else
#include <boost/system/error_code.hpp>
#endif

#include <kth/infrastructure/define.hpp>

namespace kth {

/// Console result codes, positive values are domain-specific.
enum console_result : int {
    failure = -1,
    okay = 0,
    invalid = 1
};

/// Alias for error code declarations.
using code = std::error_code;

/// Alias for boost error code declarations.

#if defined(ASIO_STANDALONE)
using boost_code = ::asio::error_code;
#else
using boost_code = boost::system::error_code;
#endif

namespace error {

// The numeric values of these codes may change without notice.
enum error_code_t {
    // general codes
    success = 0,
    deprecated = 6,
    unknown = 43,
    not_found = 3,
    file_system = 42,
    non_standard = 17,
    not_implemented = 4,
    oversubscribed = 71,

    // network
    service_stopped = 1,
    operation_failed = 2,
    resolve_failed = 7,
    network_unreachable = 8,
    address_in_use = 9,
    listen_failed = 10,
    accept_failed = 11,
    bad_stream = 12,
    channel_timeout = 13,
    address_blocked = 44,
    channel_stopped = 45,
    peer_throttling = 73,

    // database
    store_block_duplicate = 66,
    store_block_invalid_height = 67,
    store_block_missing_parent = 68,

    // blockchain
    duplicate_block = 51,
    orphan_block = 5,
    invalid_previous_block = 24,
    insufficient_work = 48,
    orphan_transaction = 14,
    insufficient_fee = 70,
    dusty_transaction = 76,
    stale_chain = 75,

    // check header
    invalid_proof_of_work = 26,
    futuristic_timestamp = 27,

    // accept header
    checkpoints_failed = 35,
    old_version_block = 36,
    incorrect_proof_of_work = 32,
    timestamp_too_early = 33,

    // check block
    block_size_limit = 50,
    empty_block = 47,
    first_not_coinbase = 28,
    extra_coinbases = 29,
    internal_duplicate = 49,
    block_internal_double_spend = 15,
    forward_reference = 79,
    merkle_mismatch = 31,
    block_legacy_sigop_limit = 30,

#if defined(KTH_CURRENCY_BCH)
    non_canonical_ordered = 84,
    block_sigchecks_limit = 85,
#endif

    // accept block
    block_non_final = 34,
    coinbase_height_mismatch = 37,
    coinbase_value_limit = 41,
    block_embedded_sigop_limit = 52,
    invalid_witness_commitment = 25,
    block_weight_limit = 82,

    // check transaction
    empty_transaction = 20,
    previous_output_null = 23,
    spend_overflow = 21,
    invalid_coinbase_script_size = 22,
    coinbase_transaction = 16,
    transaction_internal_double_spend = 72,
    transaction_size_limit = 53,
    transaction_legacy_sigop_limit = 54,

#if defined(KTH_CURRENCY_BCH)
    transaction_sigchecks_limit = 86,
#endif

    // accept transaction
    transaction_non_final = 74,
    premature_validation = 69,
    unspent_duplicate = 38,
    missing_previous_output = 19,
    double_spend = 18,
    coinbase_maturity = 46,
    spend_exceeds_value = 40,
    transaction_embedded_sigop_limit = 55,
    sequence_locked = 78,
    transaction_weight_limit = 83,
    transaction_version_out_of_range = 87,

    // connect input
    invalid_script = 39,
    invalid_script_size = 56,
    invalid_push_data_size = 57,
    invalid_operation_count = 58,
    invalid_stack_size = 59,
    invalid_stack_scope = 60,
    invalid_script_embed = 61,
    invalid_signature_encoding = 62,
    invalid_signature_lax_encoding = 63,
    incorrect_signature = 64,
    unexpected_witness = 77,
    invalid_witness = 80,
    dirty_witness = 81,
    stack_false = 65,

    // Script evaluation categories
    op_disabled = 100,
    op_reserved,
    op_return,

    // BIP65/BIP112 Script validation errors
    negative_locktime,
    unsatisfied_locktime,

    // Native Introspection
    context_not_present,
    invalid_tx_input_index,
    invalid_tx_output_index,

    // Database errors
    database_insert_failed,
    database_push_failed,             
    database_concurrent_push_failed,
    chain_reorganization_failed,
    database_pop_failed,
    
    // Blockchain validation errors
    reorganize_empty_blocks,              
    chain_state_invalid,                  
    pool_state_failed,
    transaction_lookup_failed,            
    branch_work_failed,
    block_validation_state_failed,
    transaction_validation_state_failed,
    
    // Script validation errors
    pubkey_type,                        // Invalid public key type/encoding
    cleanstack,                         // Stack not clean after script execution
    
    // BIP62/Signature validation errors
    sig_hashtype,                       // Invalid signature hash type
    sig_pushonly,                       // Signature push only violation.  
    sig_high_s,                         // High S value in signature            // 230
    sig_nullfail,                       // Null signature must fail
    minimaldata,                        // Non-minimal data encoding
    minimalif,                          // Non-minimal IF encoding
    minimal_number,                     // Non-minimal number encoding
    strict_encoding,                    // Strict DER encoding violation
    
    // Fork/Schnorr signature errors
    sighash_forkid,                     // Invalid sighash forkid usage
    sig_badlength,                      // Invalid signature length
    sig_nonschnorr,                     // Non-Schnorr signature in Schnorr context
    illegal_forkid,                     // Illegal fork ID usage            
    must_use_forkid,                    // Must use fork ID but didn't (BCHN: MUST_USE_FORKID / MISSING_FORKID)
    // Added out of order (bip147).
    multisig_satoshi_bug,

    // TX creation
    invalid_output,
    lock_time_conflict,
    input_index_out_of_range,
    input_sign_failed,

    // Mining
    low_benefit_transaction,
    duplicate_transaction,
    double_spend_mempool,
    double_spend_blockchain,

    // Numeric operations
    overflow,
    underflow,
    out_of_range,

    // Chip VM limits
    op_cost_limit,
    too_many_hash_iters,
    conditional_stack_depth,

    // Transaction structure
    invalid_tx_version,

    // Create transaction template
    insufficient_amount,
    empty_utxo_list,
    invalid_change,

    //TODO: check C-API error codes, add the new ones there
    //TODO: see the error to string functions

    // Cash Tokens
    invalid_bitfield,
    token_amount_negative,
    token_fungible_only_amount_zero,
    token_amount_bitfield_mismatch,
    token_commitment_bitfield_mismatch,
    token_fungible_with_commitment,
    token_commitment_oversized,
    token_coinbase_has_tokens,
    token_unparseable_output,
    token_unparseable_input,
    token_input_created_pre_activation,
    token_inputs_missing_or_spent,
    token_duplicate_genesis,
    token_invalid_category,
    token_fungible_insufficient,
    token_nft_ex_nihilo,
    token_amount_overflow,
    token_pre_activation_input,

    // Domain object serialization/deserialization
    read_past_end_of_buffer,
    skip_past_end_of_buffer,
    invalid_size,
    invalid_script_type,
    script_not_push_only,
    script_invalid_size,
    invalid_address_count,
    bad_inventory_count,
    version_too_low,
    version_too_new,
    invalid_compact_block,
    unsupported_version,
    invalid_filter_add,
    invalid_filter_load,
    bad_merkle_block_count,
    illegal_value,

    // Database cache
    height_not_found,
    hash_not_found,
    empty_cache,
    utxo_not_found,

    // Generic script error categories (used with op_result to pair with the failing opcode)
    insufficient_main_stack,            // Stack has too few elements for the operation
    invalid_operand_size,               // Number exceeds maximum size for arithmetic operation
    insufficient_alt_stack,             // Alt stack empty when operation needs an element
    unbalanced_conditional,             // IF/ELSE/ENDIF mismatch
    division_by_zero,                   // DIV or MOD by zero
    verify_failed,                      // VERIFY-type opcode check failed
    impossible_encoding,                // NUM2BIN cannot encode in requested size
    invalid_split_range,                // SPLIT position out of range
    invalid_number_encoding,            // BIN2NUM result not minimally encoded
    operand_size_mismatch,              // Bitwise operation operands differ in size

    // Last error code.
    last_error_code
};

enum error_condition_t {};

KI_API code make_error_code(error_code_t e);
KI_API std::error_condition make_error_condition(error_condition_t e);
KI_API error_code_t boost_to_error_code(boost_code const& ec);
KI_API error_code_t posix_to_error_code(int ec);

} // namespace error
} // namespace kth

namespace std {

template <>
struct is_error_code_enum<kth::error::error_code_t>
  : public true_type
{};

template <>
struct is_error_condition_enum<kth::error::error_condition_t>
  : public true_type
{};

} // namespace std

#endif
