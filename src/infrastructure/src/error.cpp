// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/error.hpp>

#include <boost/system/error_code.hpp>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/utility/asio_helper.hpp>

using namespace kth;

class error_category_impl
    : public std::error_category
{
public:
    char const* name() const noexcept override;
    std::string message(int ev) const noexcept override;
    std::error_condition default_error_condition(int ev) const noexcept override;
};

static
const error_category_impl& get_error_category_instance() {
    static error_category_impl instance;
    return instance;
}

char const* error_category_impl::name() const noexcept {
    return "bitcoin";
}

std::string error_category_impl::message(int ev) const noexcept {
    static const std::unordered_map<int, std::string> messages = {
        // general codes
        { error::success, "success" },
        { error::deprecated, "deprecated" },
        { error::unknown, "unknown error" },
        { error::not_found, "object does not exist" },
        { error::file_system, "file system error" },
        { error::non_standard, "transaction not standard" },
        { error::not_implemented, "feature not implemented" },
        { error::oversubscribed, "service oversubscribed" },

        // network
        { error::service_stopped, "service stopped" },
        { error::operation_failed, "operation failed" },
        { error::resolve_failed, "resolving hostname failed" },
        { error::network_unreachable, "unable to reach remote host" },
        { error::address_in_use, "address already in use" },
        { error::listen_failed, "incoming connection failed" },
        { error::accept_failed, "connection acceptance failed" },
        { error::bad_stream, "bad data stream" },
        { error::channel_timeout, "connection timed out" },
        { error::address_blocked, "address blocked by policy" },
        { error::channel_stopped, "channel stopped" },
        { error::peer_throttling, "unresponsive peer may be throttling" },

        // database
        { error::store_block_invalid_height, "block out of order" },
        { error::store_block_missing_parent, "block missing parent" },
        { error::store_block_duplicate, "block duplicate" },

        // blockchain
        { error::duplicate_block, "duplicate block" },
        { error::orphan_block, "missing block parent" },
        { error::invalid_previous_block, "previous block failed to validate" },
        { error::insufficient_work, "insufficient work to reorganize" },
        { error::orphan_transaction, "missing transaction parent" },
        { error::insufficient_fee, "insufficient transaction fee" },
        { error::dusty_transaction, "output value too low" },
        { error::stale_chain, "blockchain too far behind" },

        // check header
        { error::invalid_proof_of_work, "proof of work invalid" },
        { error::futuristic_timestamp, "timestamp too far in the future" },

        // accept header
        { error::checkpoints_failed, "block hash rejected by checkpoint" },
        { error::old_version_block, "block version rejected at current height" },
        { error::incorrect_proof_of_work, "proof of work does not match bits field" },
        { error::timestamp_too_early, "block timestamp is too early" },

        // check block
        { error::block_size_limit, "block size limit exceeded" },
        { error::empty_block, "block has no transactions" },
        { error::first_not_coinbase, "first transaction not a coinbase" },
        { error::extra_coinbases, "more than one coinbase" },
        { error::internal_duplicate, "matching transaction hashes in block" },
        { error::block_internal_double_spend, "double spend internal to block" },
        { error::forward_reference, "transactions out of order" },
        { error::merkle_mismatch, "merkle root mismatch" },
        { error::block_legacy_sigop_limit, "too many block legacy signature operations" },

#if defined(KTH_CURRENCY_BCH)
        { error::non_canonical_ordered, "the block is not canonically ordered" },
        { error::block_sigchecks_limit, "too many block SigChecks" },
#endif

        // accept block
        { error::block_non_final, "block contains a non-final transaction" },
        { error::coinbase_height_mismatch, "block height mismatch in coinbase" },
        { error::coinbase_value_limit, "coinbase value too high" },
        { error::block_embedded_sigop_limit, "too many block embedded signature operations" },
        { error::invalid_witness_commitment, "invalid witness commitment" },
        { error::block_weight_limit, "block weight limit exceeded" },

        // check transaction
        { error::empty_transaction, "transaction inputs or outputs empty" },
        { error::previous_output_null, "non-coinbase transaction has input with null previous output" },
        { error::spend_overflow, "spend outside valid range" },
        { error::invalid_coinbase_script_size, "coinbase script too small or large" },
        { error::coinbase_transaction, "coinbase transaction disallowed in memory pool" },
        { error::transaction_internal_double_spend, "double spend internal to transaction" },
        { error::transaction_size_limit, "transaction size limit exceeded" },
        { error::transaction_legacy_sigop_limit, "too many transaction legacy signature operations" },

#if defined(KTH_CURRENCY_BCH)
        { error::transaction_sigchecks_limit, "too many transaction SigChecks" },
#endif
        // accept transaction
        { error::transaction_non_final, "transaction currently non-final for next block" },
        { error::premature_validation, "transaction validation under checkpoint" },
        { error::unspent_duplicate, "matching transaction with unspent outputs" },
        { error::missing_previous_output, "previous output not found" },
        { error::double_spend, "double spend of input" },
        { error::coinbase_maturity, "immature coinbase spent" },
        { error::spend_exceeds_value, "spend exceeds value of inputs" },
        { error::transaction_embedded_sigop_limit, "too many transaction embedded signature operations" },
        { error::sequence_locked, "transaction currently locked" },
        { error::transaction_weight_limit, "transaction weight limit exceeded" },
        { error::transaction_version_out_of_range, "transaction version out of range" },

        // connect input
        { error::invalid_script, "invalid script" },
        { error::invalid_script_size, "invalid script size" },
        { error::invalid_push_data_size, "invalid push data size" },
        { error::invalid_operation_count, "invalid operation count" },
        { error::invalid_stack_size, "invalid stack size" },
        { error::invalid_stack_scope, "invalid stack scope" },
        { error::invalid_script_embed, "invalid script embed" },
        { error::invalid_signature_encoding, "invalid signature encoding" },
        { error::invalid_signature_lax_encoding, "invalid signature lax encoding" },
        { error::incorrect_signature, "incorrect signature" },
        { error::unexpected_witness, "unexpected witness" },
        { error::invalid_witness, "invalid witness" },
        { error::dirty_witness, "dirty witness" },
        { error::stack_false, "stack false" },

        // op eval
        { error::op_disabled, "op_disabled" },
        { error::op_reserved, "op_reserved" },
        { error::op_push_size, "op_push_size" },
        { error::op_push_data, "op_push_data" },
        { error::op_if, "op_if" },
        { error::op_notif, "op_notif" },
        { error::op_else, "op_else" },
        { error::op_endif, "op_endif" },
        { error::op_verify_empty_stack, "op_verify_empty_stack" },
        { error::op_verify_failed, "op_verify_failed" },
        { error::op_return, "op_return" },
        { error::op_to_alt_stack, "op_to_alt_stack" },
        { error::op_from_alt_stack, "op_from_alt_stack" },
        { error::op_drop2, "op_drop2" },
        { error::op_dup2, "op_dup2" },
        { error::op_dup3, "op_dup3" },
        { error::op_over2, "op_over2" },
        { error::op_rot2, "op_rot2" },
        { error::op_swap2, "op_swap2" },
        { error::op_if_dup, "op_if_dup" },
        { error::op_drop, "op_drop" },
        { error::op_dup, "op_dup" },
        { error::op_nip, "op_nip" },
        { error::op_over, "op_over" },
        { error::op_pick, "op_pick" },
        { error::op_roll, "op_roll" },
        { error::op_rot, "op_rot" },
        { error::op_swap, "op_swap" },
        { error::op_tuck, "op_tuck" },

        { error::op_cat, "op_cat" },
        { error::op_split, "op_split" },
        { error::op_reverse_bytes, "op_reverse_bytes" },
        { error::op_num2bin, "op_num2bin" },
        { error::op_num2bin_invalid_size, "op_num2bin_invalid_size" },
        { error::op_num2bin_size_exceeded, "op_num2bin_size_exceeded" },
        { error::op_num2bin_impossible_encoding, "op_num2bin_impossible_encoding" },
        { error::op_bin2num, "op_bin2num" },
        { error::op_bin2num_invalid_number_range, "op_bin2num_invalid_number_range" },

        { error::op_size, "op_size" },

        { error::op_and, "op_and" },
        { error::op_or, "op_or" },
        { error::op_xor, "op_xor" },

        { error::op_equal, "op_equal" },
        { error::op_equal_verify_insufficient_stack, "op_equal_verify_insufficient_stack" },
        { error::op_equal_verify_failed, "op_equal_verify_failed" },
        { error::op_add1, "op_add1" },
        { error::op_sub1, "op_sub1" },
        { error::op_negate, "op_negate" },
        { error::op_abs, "op_abs" },
        { error::op_not, "op_not" },
        { error::op_nonzero, "op_nonzero" },

        { error::op_add, "op_add" },
        { error::op_add_overflow, "op_add_overflow" },

        { error::op_sub, "op_sub" },
        { error::op_sub_underflow, "op_sub_underflow" },
        { error::op_mul, "op_mul" },
        { error::op_mul_overflow, "op_mul_overflow" },
        { error::op_div, "op_div" },
        { error::op_div_by_zero, "op_div_by_zero" },
        { error::op_mod, "op_mod" },
        { error::op_mod_by_zero, "op_mod_by_zero" },

        { error::op_bool_and, "op_bool_and" },
        { error::op_bool_or, "op_bool_or" },
        { error::op_num_equal, "op_num_equal" },
        { error::op_num_equal_verify_insufficient_stack, "op_num_equal_verify_insufficient_stack" },
        { error::op_num_equal_verify_failed, "op_num_equal_verify_failed" },
        { error::op_num_not_equal, "op_num_not_equal" },
        { error::op_less_than, "op_less_than" },
        { error::op_greater_than, "op_greater_than" },
        { error::op_less_than_or_equal, "op_less_than_or_equal" },
        { error::op_greater_than_or_equal, "op_greater_than_or_equal" },
        { error::op_min, "op_min" },
        { error::op_max, "op_max" },
        { error::op_within, "op_within" },
        { error::op_ripemd160, "op_ripemd160" },
        { error::op_sha1, "op_sha1" },
        { error::op_sha256, "op_sha256" },
        { error::op_hash160, "op_hash160" },
        { error::op_hash256, "op_hash256" },
        { error::op_code_seperator, "op_code_seperator" },

        { error::op_check_sig, "op_check_sig" },
        { error::op_check_sig_verify_failed, "op_check_sig_verify_failed" },

        { error::op_check_data_sig, "op_check_data_sig" },
        { error::op_check_data_sig_verify, "op_check_data_sig_verify" },

        { error::multisig_missing_key_count, "multisig_missing_key_count" },
        { error::multisig_invalid_key_count, "multisig_invalid_key_count" },
        { error::multisig_missing_pubkeys, "multisig_missing_pubkeys" },
        { error::multisig_missing_signature_count, "multisig_missing_signature_count" },
        { error::multisig_invalid_signature_count, "multisig_invalid_signature_count" },
        { error::multisig_missing_endorsements, "multisig_missing_endorsements" },
        { error::multisig_empty_stack, "multisig_empty_stack" },
        { error::op_check_multisig, "op_check_multisig" },
        
        // BIP65/BIP112 Script validation errors
        { error::negative_locktime, "negative_locktime" },
        { error::unsatisfied_locktime, "unsatisfied_locktime" },

    // Native Introspection Opcodes
        { error::context_not_present, "context_not_present" },
        { error::op_input_index, "op_input_index" },
        { error::op_active_bytecode, "op_active_bytecode" },
        { error::op_tx_version, "op_tx_version" },
        { error::op_tx_input_count, "op_tx_input_count" },
        { error::op_tx_output_count, "op_tx_output_count" },
        { error::op_tx_locktime, "op_tx_locktime" },
        { error::op_utxo_value, "op_utxo_value" },
        { error::op_utxo_bytecode, "op_utxo_bytecode" },
        { error::op_outpoint_tx_hash, "op_outpoint_tx_hash" },
        { error::op_outpoint_index, "op_outpoint_index" },
        { error::op_input_bytecode, "op_input_bytecode" },
        { error::op_input_sequence_number, "op_input_sequence_number" },
        { error::op_output_value, "op_output_value" },
        { error::op_output_bytecode, "op_output_bytecode" },
        { error::op_utxo_token_category, "op_utxo_token_category" },
        { error::op_utxo_token_commitment, "op_utxo_token_commitment" },
        { error::op_utxo_token_amount, "op_utxo_token_amount" },
        { error::op_output_token_category, "op_output_token_category" },
        { error::op_output_token_commitment, "op_output_token_commitment" },
        { error::op_output_token_amount, "op_output_token_amount" },

        // Database errors
        { error::database_insert_failed, "database_insert_failed" },
        { error::database_push_failed, "database_push_failed" },
        { error::database_concurrent_push_failed, "database_concurrent_push_failed" },
        { error::chain_reorganization_failed, "chain_reorganization_failed" },
        { error::database_pop_failed, "database_pop_failed" },
        
        // Blockchain validation errors
        { error::reorganize_empty_blocks, "reorganize_empty_blocks" },
        { error::chain_state_invalid, "chain_state_invalid" },
        { error::pool_state_failed, "pool_state_failed" },
        { error::transaction_lookup_failed, "transaction_lookup_failed" },
        { error::branch_work_failed, "branch_work_failed" },
        { error::block_validation_state_failed, "block_validation_state_failed" },
        { error::transaction_validation_state_failed, "transaction_validation_state_failed" },
        
        // Script validation errors
        { error::pubkey_type, "pubkey_type" },
        { error::cleanstack, "cleanstack" },
        
        // BIP62/Signature validation errors
        { error::sig_hashtype, "sig_hashtype" },
        { error::sig_pushonly, "sig_pushonly" },
        { error::sig_high_s, "sig_high_s" },
        { error::sig_nullfail, "sig_nullfail" },
        { error::minimaldata, "minimaldata" },
        { error::minimalif, "minimalif" },
        { error::minimal_number, "minimal_number" },
        { error::strict_encoding, "strict_encoding" },
        
        // Fork/Schnorr signature errors
        { error::sighash_forkid, "sighash_forkid" },
        { error::sig_badlength, "sig_badlength" },
        { error::sig_nonschnorr, "sig_nonschnorr" },
        { error::illegal_forkid, "illegal_forkid" },
        { error::must_use_forkid, "must_use_forkid" },
        { error::missing_forkid, "missing_forkid" },
        // Added out of order (bip147).
        { error::multisig_satoshi_bug, "multisig_satoshi_bug" },

        // TX creation
        { error::invalid_output, "invalid output" },
        { error::lock_time_conflict, "lock time conflict" },
        { error::input_index_out_of_range, "input index out of range" },
        { error::input_sign_failed, "input sign failed" },

        // Mining
        { error::low_benefit_transaction, "low benefit transaction" },
        { error::duplicate_transaction, "duplicate transaction" },
        { error::double_spend_mempool, "double spend mempool" },
        { error::double_spend_blockchain, "double spend blockchain" },

        // Numeric operations
        { error::overflow, "overflow" },
        { error::underflow, "underflow" },
        { error::out_of_range, "out of range" },

        // Chip VM limits
        { error::too_many_hash_iters, "too many hash iters" },
        { error::conditional_stack_depth, "conditional stack depth" }
    };

    auto const message = messages.find(ev);
    return message != messages.end() ? message->second : "invalid code";
}

// We are not currently using this.
std::error_condition error_category_impl::default_error_condition(int ev) const noexcept {
    return {ev, *this};
}

namespace kth {
namespace error {

code make_error_code(error_code_t e) {
    return {e, get_error_category_instance()};
}

std::error_condition make_error_condition(error_condition_t e) {
    return {e, get_error_category_instance()};
}

error_code_t boost_to_error_code(boost_code const& ec) {
    namespace boost_error = boost::system::errc;

#ifdef _MSC_VER
    // TODO: is there a means to map ASIO errors to boost errors?
    // ASIO codes are unique on Windows but not on Linux.
    namespace asio_error = ::asio::error;
#endif
    // TODO: use a static map (hash table)
    switch (ec.value()) {
        // There should be no boost mapping to the shutdown sentinel.
        //    return error::service_stopped;

        case boost_error::success:
            return error::success;

        // network errors
#ifdef _MSC_VER
        case asio_error::connection_aborted:
        case asio_error::connection_reset:
        case asio_error::operation_aborted:
        case asio_error::operation_not_supported:
#endif
        case boost_error::connection_aborted:
        case boost_error::connection_refused:
        case boost_error::connection_reset:
        case boost_error::not_connected:
        case boost_error::operation_canceled:
        case boost_error::operation_not_permitted:
        case boost_error::operation_not_supported:
        case boost_error::owner_dead:
        case boost_error::permission_denied:
            return error::operation_failed;

#ifdef _MSC_VER
        case asio_error::address_family_not_supported:
#endif
        case boost_error::address_family_not_supported:
        case boost_error::address_not_available:
        case boost_error::bad_address:
        case boost_error::destination_address_required:
            return error::resolve_failed;

        case boost_error::broken_pipe:
        case boost_error::host_unreachable:
        case boost_error::network_down:
        case boost_error::network_reset:
        case boost_error::network_unreachable:
        case boost_error::no_link:
        case boost_error::no_protocol_option:
        case boost_error::no_such_file_or_directory:
        case boost_error::not_a_socket:
        case boost_error::protocol_not_supported:
        case boost_error::wrong_protocol_type:
            return error::network_unreachable;

        case boost_error::address_in_use:
        case boost_error::already_connected:
        case boost_error::connection_already_in_progress:
        case boost_error::operation_in_progress:
            return error::address_in_use;

        case boost_error::bad_message:
        case boost_error::illegal_byte_sequence:
        case boost_error::io_error:
        case boost_error::message_size:
        case boost_error::no_message_available:
        case boost_error::no_message:
        case boost_error::no_stream_resources:
        case boost_error::not_a_stream:
        case boost_error::protocol_error:
            return error::bad_stream;

#ifdef _MSC_VER
        case asio_error::timed_out:
#endif
        case boost_error::stream_timeout:
        case boost_error::timed_out:
            return error::channel_timeout;

        // file system errors
        case boost_error::cross_device_link:
        case boost_error::bad_file_descriptor:
        case boost_error::device_or_resource_busy:
        case boost_error::directory_not_empty:
        case boost_error::executable_format_error:
        case boost_error::file_exists:
        case boost_error::file_too_large:
        case boost_error::filename_too_long:
        case boost_error::invalid_seek:
        case boost_error::is_a_directory:
        case boost_error::no_space_on_device:
        case boost_error::no_such_device:
        case boost_error::no_such_device_or_address:
        case boost_error::read_only_file_system:
        // same as operation_would_block on non-windows
        //case boost_error::resource_unavailable_try_again:
        case boost_error::text_file_busy:
        case boost_error::too_many_files_open:
        case boost_error::too_many_files_open_in_system:
        case boost_error::too_many_links:
        case boost_error::too_many_symbolic_link_levels:
            return error::file_system;

        // unknown errors
        case boost_error::argument_list_too_long:
        case boost_error::argument_out_of_domain:
        case boost_error::function_not_supported:
        case boost_error::identifier_removed:
        case boost_error::inappropriate_io_control_operation:
        case boost_error::interrupted:
        case boost_error::invalid_argument:
        case boost_error::no_buffer_space:
        case boost_error::no_child_process:
        case boost_error::no_lock_available:
        case boost_error::no_such_process:
        case boost_error::not_a_directory:
        case boost_error::not_enough_memory:
        case boost_error::operation_would_block:
        case boost_error::resource_deadlock_would_occur:
        case boost_error::result_out_of_range:
        case boost_error::state_not_recoverable:
        case boost_error::value_too_large:
        default:
            return error::unknown;
    }
}

error_code_t posix_to_error_code(int ec) {
    // TODO(legacy): expand mapping for database scenario.
    switch (ec) {
        // protocol codes (from zeromq)
        case ENOBUFS:
        case ENOTSUP:
        case EPROTONOSUPPORT:
            return error::operation_failed;
        case ENETDOWN:
            return error::network_unreachable;
        case EADDRINUSE:
            return error::address_in_use;
        case EADDRNOTAVAIL:
            return error::resolve_failed;
        case ECONNREFUSED:
            return error::accept_failed;
        case EINPROGRESS:
            return error::channel_timeout;
            return error::bad_stream;
        case EAGAIN:
            return error::channel_timeout;
        case EFAULT:
            return error::bad_stream;
        case EINTR:
        case ENOTSOCK:
            return error::service_stopped;
        default:
            return error::unknown;
    }
}

} // namespace error
} // namespace kth
