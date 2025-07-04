// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_INTERPRETER_HPP
#define KTH_DOMAIN_MACHINE_INTERPRETER_HPP

#include <cstdint>

#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

class KD_API interpreter {
public:
    using result = error::error_code_t;

    // Operations (shared).
    //-----------------------------------------------------------------------------

    static
    result op_nop(opcode /*unused*/);

    static
    result op_disabled(opcode /*unused*/);

    static
    result op_reserved(opcode /*unused*/);

    static
    result op_push_number(program& program, uint8_t value);

    static
    result op_push_size(program& program, operation const& op);

    static
    result op_push_data(program& program, data_chunk const& data, uint32_t size_limit);

    // Operations (not shared).
    //-----------------------------------------------------------------------------

    static
    result op_if(program& program);

    static
    result op_notif(program& program);

    static
    result op_else(program& program);

    static
    result op_endif(program& program);

    static
    result op_verify(program& program);

    static
    result op_return(program& program);

    static
    result op_to_alt_stack(program& program);

    static
    result op_from_alt_stack(program& program);

    static
    result op_drop2(program& program);

    static
    result op_dup2(program& program);

    static
    result op_dup3(program& program);

    static
    result op_over2(program& program);

    static
    result op_rot2(program& program);

    static
    result op_swap2(program& program);

    static
    result op_if_dup(program& program);

    static
    result op_depth(program& program);

    static
    result op_drop(program& program);

    static
    result op_dup(program& program);

    static
    result op_nip(program& program);

    static
    result op_over(program& program);

    static
    result op_pick(program& program);

    static
    result op_roll(program& program);

    static
    result op_rot(program& program);

    static
    result op_swap(program& program);

    static
    result op_tuck(program& program);

    static
    result op_cat(program& program);

    static
    result op_split(program& program);

    static
    result op_reverse_bytes(program& program);

    static
    result op_num2bin(program& program);

    static
    result op_bin2num(program& program);

    static
    result op_size(program& program);

    static
    result op_and(program& program);

    static
    result op_or(program& program);

    static
    result op_xor(program& program);

    static
    result op_equal(program& program);

    static
    result op_equal_verify(program& program);

    static
    result op_add1(program& program);

    static
    result op_sub1(program& program);

    static
    result op_negate(program& program);

    static
    result op_abs(program& program);

    static
    result op_not(program& program);

    static
    result op_nonzero(program& program);

    static
    result op_add(program& program);

    static
    result op_sub(program& program);

    static
    result op_mul(program& program);

    static
    result op_div(program& program);

    static
    result op_mod(program& program);

    static
    result op_bool_and(program& program);

    static
    result op_bool_or(program& program);

    static
    result op_num_equal(program& program);

    static
    result op_num_equal_verify(program& program);

    static
    result op_num_not_equal(program& program);

    static
    result op_less_than(program& program);

    static
    result op_greater_than(program& program);

    static
    result op_less_than_or_equal(program& program);

    static
    result op_greater_than_or_equal(program& program);

    static
    result op_min(program& program);

    static
    result op_max(program& program);

    static
    result op_within(program& program);

    static
    result op_ripemd160(program& program);

    static
    result op_sha1(program& program);

    static
    result op_sha256(program& program);

    static
    result op_hash160(program& program);

    static
    result op_hash256(program& program);

    static
    result op_codeseparator(program& program, operation const& op);

    static
    result op_check_sig(program& program);

    static
    result op_check_sig_verify(program& program);

    static
    result op_check_data_sig(program& program);

    static
    result op_check_data_sig_verify(program& program);

    static
    result op_check_multisig_verify(program& program);

    static
    result op_check_multisig(program& program);

    static
    result op_check_locktime_verify(program& program);

    static
    result op_check_sequence_verify(program& program);

    static
    result op_input_index(program& program);

    static
    result op_active_bytecode(program& program);

    static
    result op_tx_version(program& program);

    static
    result op_tx_input_count(program& program);

    static
    result op_tx_output_count(program& program);

    static
    result op_tx_locktime(program& program);

    static
    result op_utxo_value(program& program);

    static
    result op_utxo_bytecode(program& program);

    static
    result op_outpoint_tx_hash(program& program);

    static
    result op_outpoint_index(program& program);

    static
    result op_input_bytecode(program& program);

    static
    result op_input_sequence_number(program& program);

    static
    result op_output_value(program& program);

    static
    result op_output_bytecode(program& program);

    static
    result op_utxo_token_category(program& program);

    static
    result op_utxo_token_commitment(program& program);

    static
    result op_utxo_token_amount(program& program);

    static
    result op_output_token_category(program& program);

    static
    result op_output_token_commitment(program& program);

    static
    result op_output_token_amount(program& program);

    /// Run program script.
    static
    code run(program& program);

    /// Run individual operations (idependent of the script).
    /// For best performance use script runner for a sequence of operations.
    static
    code run(operation const& op, program& program);


// Debug step by step
// ----------------------------------------------------------------------------
    static
    std::pair<code, size_t> debug_start(program const& program);

    static
    bool debug_steps_available(program const& program, size_t step);

    static
    std::tuple<code, size_t, program> debug_step(program program, size_t step);

    static
    code debug_end(program const& program);

private:
    static
    result run_op(operation const& op, program& program);

    /// Helper function for common Native Introspection validations
    static
    result validate_native_introspection(program const& program);

    /// Helper function for post-processing Native Introspection push operations
    static
    void post_process_introspection_push(program& program, data_chunk const& data);
};

} // namespace kth::domain::machine

#include <kth/domain/impl/machine/interpreter.ipp>

#endif
