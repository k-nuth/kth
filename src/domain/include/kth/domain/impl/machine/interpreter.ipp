// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_INTERPRETER_IPP_
#define KTH_DOMAIN_MACHINE_INTERPRETER_IPP_

#include <bit>
#include <cstdint>
#include <utility>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/script_flags.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/machine/sighash_algorithm.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>


// TODO: move to a concepts file
template <typename Op>
concept bitwise_op = requires(Op op, uint8_t a, uint8_t b) {
    { op(a, b) } -> std::same_as<uint8_t>;
};


namespace kth::domain::machine {

// FindAndDelete: remove all occurrences of an endorsement (as a data push) from a script.
// Matches BCHN's FindAndDelete(scriptCode, CScript(vchSig)).
// The endorsement is serialized as a push operation: [push_opcode][data].
// Only applies to pre-forkid scripts (legacy sighash).
inline
chain::script find_and_delete_endorsement(chain::script const& script_code, data_chunk const& endorsement) {
    if (endorsement.empty()) {
        return script_code;
    }

    // Build the serialized push pattern for the endorsement.
    // Matches CScript::operator<<(vector<uint8_t>) serialization.
    data_chunk pattern;
    auto const size = endorsement.size();
    if (size <= static_cast<size_t>(opcode::push_size_75)) {
        pattern.reserve(1 + size);
        pattern.push_back(static_cast<uint8_t>(size));
    } else if (size <= 0xff) {
        pattern.reserve(2 + size);
        pattern.push_back(static_cast<uint8_t>(opcode::push_one_size));
        pattern.push_back(static_cast<uint8_t>(size));
    } else {
        pattern.reserve(3 + size);
        pattern.push_back(static_cast<uint8_t>(opcode::push_two_size));
        pattern.push_back(static_cast<uint8_t>(size & 0xff));
        pattern.push_back(static_cast<uint8_t>(size >> 8));
    }
    pattern.insert(pattern.end(), endorsement.begin(), endorsement.end());

    // Serialize the script_code (without length prefix).
    auto serialized = script_code.to_data(false);

    // Search and remove all occurrences of the pattern (matching BCHN's byte-level FindAndDelete).
    bool found = false;
    data_chunk result;
    result.reserve(serialized.size());

    size_t i = 0;
    while (i < serialized.size()) {
        if (i + pattern.size() <= serialized.size() &&
            std::equal(pattern.begin(), pattern.end(), serialized.begin() + i)) {
            // Skip over the matched pattern.
            i += pattern.size();
            found = true;
        } else {
            result.push_back(serialized[i]);
            ++i;
        }
    }

    if ( ! found) {
        return script_code;
    }

    // Re-parse the modified serialized script.
    byte_reader reader(result);
    auto parsed = chain::script::from_data(reader, false);
    if (parsed) {
        return std::move(*parsed);
    }
    // Fallback: return original if re-parse fails (shouldn't happen).
    return script_code;
}

// Determine if FindAndDelete should be applied for this endorsement.
// BCHN: "Drop the signature in scripts when SIGHASH_FORKID is not used."
inline
bool should_find_and_delete(data_chunk const& endorsement, script_flags_t flags) {
    if (endorsement.empty()) {
        return false;
    }
    // If forkid is not enabled, always do FindAndDelete (legacy behavior).
    if ( ! chain::script::is_enabled(flags, script_flags::bch_sighash_forkid)) {
        return true;
    }
    // If forkid is enabled but this signature doesn't use forkid, do FindAndDelete.
    auto const sighash_byte = endorsement.back();
    using kth::infrastructure::machine::sighash_algorithm;
    return (sighash_byte & static_cast<uint8_t>(sighash_algorithm::forkid)) == 0;
}

// Returns true if the result is a signature verification outcome
// that should be converted to a boolean on the stack.
// All other errors are fatal and must be propagated as script errors.
inline
bool is_signature_result(op_result const& r) {
    return r == error::success
        || r == error::incorrect_signature
        || r == error::invalid_signature_encoding;
}

static constexpr
auto op_75 = uint8_t(opcode::push_size_75);

// Operations (shared).
//-----------------------------------------------------------------------------

inline
interpreter::result interpreter::op_nop(opcode /*unused*/) {
    return {};
}

inline
interpreter::result interpreter::op_disabled(opcode code) {
    return error::op_disabled;
}

inline
interpreter::result interpreter::op_reserved(opcode /*unused*/) {
    return error::op_reserved;
}

inline
interpreter::result interpreter::op_push_number(program& program, uint8_t value) {
    program.push_move({value});
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_push_size(program& program, operation const& op) {
    if (op.data().size() > op_75) {
        return error::invalid_push_data_size;
    }

    program.push_copy(op.data());
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

// TODO: std::move the data chunk to the program
inline
interpreter::result interpreter::op_push_data(program& program, data_chunk const& data, uint32_t size_limit) {
    if (data.size() > size_limit) {
        return error::invalid_push_data_size;
    }

    program.push_copy(data);
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

// Operations (not shared).
//-----------------------------------------------------------------------------
// All index parameters are zero-based and relative to stack top.

inline
interpreter::result interpreter::op_if(program& program) {
    auto value = false;

    if (program.succeeded()) {
        if (program.empty()) {
            return {error::unbalanced_conditional, opcode::if_};  // BCHN: UNBALANCED_CONDITIONAL
        }

        // MINIMALIF: stack top must be empty [] or exactly [0x01] (BCHN behavior).
        if (chain::script::is_enabled(program.flags(), script_flags::bch_minimalif)) {
            auto const& top = program.item(0);
            if (top.size() > 1 || (top.size() == 1 && top[0] != 1)) {
                return {error::minimalif, opcode::if_};
            }
        }

        value = program.stack_true(false);
        program.drop();
    }

    program.open(value);
    return {};
}

inline
interpreter::result interpreter::op_notif(program& program) {
    auto value = false;

    if (program.succeeded()) {
        if (program.empty()) {
            return {error::unbalanced_conditional, opcode::notif};  // BCHN: UNBALANCED_CONDITIONAL
        }

        // MINIMALIF: stack top must be empty [] or exactly [0x01] (BCHN behavior).
        if (chain::script::is_enabled(program.flags(), script_flags::bch_minimalif)) {
            auto const& top = program.item(0);
            if (top.size() > 1 || (top.size() == 1 && top[0] != 1)) {
                return {error::minimalif, opcode::notif};
            }
        }

        value = !program.stack_true(false);
        program.drop();
    }

    program.open(value);
    return {};
}

inline
interpreter::result interpreter::op_else(program& program) {
    if (program.closed()) {
        return {error::unbalanced_conditional, opcode::else_};
    }

    program.negate();
    return {};
}

inline
interpreter::result interpreter::op_endif(program& program) {
    if (program.closed()) {
        return {error::unbalanced_conditional, opcode::endif};
    }

    program.close();
    return {};
}

inline
interpreter::result interpreter::op_verify(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::verify};
    }

    if ( ! program.stack_true(false)) {
        return {error::verify_failed, opcode::verify};
    }

    program.drop();
    return {};
}

inline
interpreter::result interpreter::op_return(program& /*unused*/) {
    return error::op_return;
}

inline
interpreter::result interpreter::op_to_alt_stack(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::toaltstack};
    }

    program.push_alternate(program.pop());
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return {};
}

inline
interpreter::result interpreter::op_from_alt_stack(program& program) {
    if (program.empty_alternate()) {
        return {error::insufficient_alt_stack, opcode::fromaltstack};
    }

    program.push_move(program.pop_alternate());
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_drop2(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::drop2};
    }

    program.drop();
    program.drop();
    return {};
}

inline
interpreter::result interpreter::op_dup2(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::dup2};
    }

    auto item1 = program.item(1);
    auto item0 = program.item(0);

    program.push_move(std::move(item1));
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());

    program.push_move(std::move(item0));
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_dup3(program& program) {
    if (program.size() < 3) {
        return {error::insufficient_main_stack, opcode::dup3};
    }

    auto item2 = program.item(2);
    auto item1 = program.item(1);
    auto item0 = program.item(0);

    program.push_move(std::move(item2));
    program.get_metrics().add_op_cost(program.top().size());
    program.push_move(std::move(item1));
    program.get_metrics().add_op_cost(program.top().size());
    program.push_move(std::move(item0));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_over2(program& program) {
    if (program.size() < 4) {
        return {error::insufficient_main_stack, opcode::over2};
    }

    auto item3 = program.item(3);
    auto item2 = program.item(2);

    program.push_move(std::move(item3));
    program.get_metrics().add_op_cost(program.top().size());
    program.push_move(std::move(item2));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_rot2(program& program) {
    if (program.size() < 6) {
        return {error::insufficient_main_stack, opcode::rot2};
    }

    auto const position_5 = program.position(5);
    auto const position_4 = program.position(4);

    auto copy_5 = *position_5;
    auto copy_4 = *position_4;

    program.erase(position_5, position_4 + 1);
    program.push_move(std::move(copy_5));
    program.get_metrics().add_op_cost(program.top().size());
    program.push_move(std::move(copy_4));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_swap2(program& program) {
    if (program.size() < 4) {
        return {error::insufficient_main_stack, opcode::swap2};
    }

    program.swap(3, 1);
    program.swap(2, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return {};
}

inline
interpreter::result interpreter::op_if_dup(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::ifdup};
    }

    if (program.stack_true(false)) {
        program.duplicate(0);
        program.get_metrics().add_op_cost(program.top().size());
    }

    return {};
}

inline
interpreter::result interpreter::op_depth(program& program) {
    auto num_exp = number::from_int(program.size());
    if ( ! num_exp) {
        return {num_exp.error(), opcode::depth};
    }
    program.push_move(num_exp->data());
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_drop(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::drop};
    }

    program.drop();
    // No metrics
    return {};
}

inline
interpreter::result interpreter::op_dup(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::dup};
    }

    program.duplicate(0);
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_nip(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::nip};
    }

    program.erase(program.position(1));
    // No metrics
    return {};
}

inline
interpreter::result interpreter::op_over(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::over};
    }

    program.duplicate(1);
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_pick(program& program) {
    // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
    auto const index = program.pop_index();
    if ( ! index) {
        return {index.error(), opcode::pick};
    }

    program.push_copy(program.item(*index));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_roll(program& program) {
    // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
    auto const index = program.pop_index();
    if ( ! index) {
        return {index.error(), opcode::roll};
    }

    auto copy = program.item(*index);
    program.erase(program.position(*index));
    program.get_metrics().add_op_cost(*index);
    program.push_move(std::move(copy));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_rot(program& program) {
    // (x1 x2 x3 -- x2 x3 x1)
    //  x2 x1 x3  after first swap
    //  x2 x3 x1  after second swap
    if (program.size() < 3) {
        return {error::insufficient_main_stack, opcode::rot};
    }

    program.swap(2, 1);
    program.swap(1, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return {};
}

inline
interpreter::result interpreter::op_swap(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::swap};
    }

    program.swap(1, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return {};
}

inline
interpreter::result interpreter::op_tuck(program& program) {
    // (x1 x2 -- x2 x1 x2)
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::tuck};
    }

    auto first = program.pop();
    auto second = program.pop();
    program.get_metrics().add_op_cost(first.size());
    program.push_copy(first);
    program.push_move(std::move(second));
    program.push_move(std::move(first));
    return {};
}


// case OP_CAT: {
//     // (x1 x2 -- out)
//     if (stack.size() < 2) {
//         return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
//     }
//     valtype &vch1 = stacktop(-2);
//     const valtype &vch2 = stacktop(-1);
//     if (vch1.size() + vch2.size() > maxScriptElementSize) {
//         return set_error(serror, ScriptError::PUSH_SIZE);
//     }
//     vch1.insert(vch1.end(), vch2.begin(), vch2.end());
//     popstack(stack);
//     metrics.TallyPushOp(stack.back().size());
// } break;

inline
interpreter::result interpreter::op_cat(program& program) {
    // (x1 x2 -- out)
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::cat};
    }

    auto last = program.pop();
    auto& but_last = program.top();

    if (but_last.size() + last.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }

    but_last.insert(but_last.end(), last.begin(), last.end());
    program.get_metrics().add_op_cost(but_last.size());

    return {};
}


// case OP_SPLIT: {
//     // (in position -- x1 x2)
//     if (stack.size() < 2) {
//         return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
//     }

//     const valtype &data = stacktop(-2);

//     // Make sure the split point is appropriate.
//     int64_t const position = CScriptNum(stacktop(-1), fRequireMinimal, maxIntegerSizeLegacy).getint64();
//     if (position < 0 || uint64_t(position) > data.size()) {
//         return set_error(serror, ScriptError::INVALID_SPLIT_RANGE);
//     }

//     // Prepare the results in their own buffer as `data` will be invalidated.
//     valtype n1(data.begin(), data.begin() + position);
//     valtype n2(data.begin() + position, data.end());

//     // Replace existing stack values by the new values.
//     const size_t totalSize = n1.size() + n2.size();
//     stacktop(-2) = std::move(n1);
//     stacktop(-1) = std::move(n2);
//     metrics.TallyPushOp(totalSize);
// } break;


inline
interpreter::result interpreter::op_split(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::split};
    }

    auto& pos = program.item(0);  // last item
    auto& data = program.item(1); // but last item

    // MINIMALNUM: position must be minimally encoded
    if ((program.flags() & script_flags::bch_minimaldata) != 0
        && ! number::is_minimally_encoded(pos, program.max_integer_size_legacy())) {
        return {error::minimal_number, opcode::split};
    }

    number position;
    if ( ! position.set_data(pos, program.max_integer_size_legacy())) {
        return {error::invalid_operand_size, opcode::split};
    }
    auto const pos64 = position.int64();

    if (pos64 < 0 || size_t(pos64) > data.size()) {
        return {error::invalid_split_range, opcode::split};
    }

    auto n1 = data_chunk(data.begin(), data.begin() + pos64);
    auto n2 = data_chunk(data.begin() + pos64, data.end());
    size_t const total_size = n1.size() + n2.size();

    data = std::move(n1);
    pos = std::move(n2);

    program.get_metrics().add_op_cost(total_size);

    return {};
}

inline
interpreter::result interpreter::op_reverse_bytes(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::reverse_bytes};
    }

    // Operate in-place on the top stack element (like BCHN)
    auto& data = program.top();
    std::reverse(data.begin(), data.end());
    program.get_metrics().add_op_cost(data.size());

    return {};
}


// //
// // Conversion operations
// //
// case OP_NUM2BIN: {
//     // (in size -- out)
//     if (stack.size() < 2) {
//         return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
//     }

//     uint64_t const size = CScriptNum(stacktop(-1), fRequireMinimal, maxIntegerSizeLegacy).getint64();
//     if (size > maxScriptElementSize) {
//         return set_error(serror, ScriptError::PUSH_SIZE);
//     }

//     popstack(stack);
//     valtype &rawnum = stacktop(-1);

//     // Try to see if we can fit that number in the number of byte requested.
//     ScriptNumType::MinimallyEncode(rawnum);
//     if (rawnum.size() > size) {
//         // We definitively cannot.
//         return set_error(serror, ScriptError::IMPOSSIBLE_ENCODING);
//     }

//     // We already have an element of the right size, we don't need to do anything.
//     if (rawnum.size() == size) {
//         metrics.TallyPushOp(rawnum.size());
//         break;
//     }

//     uint8_t signbit = 0x00;
//     if (rawnum.size() > 0) {
//         signbit = rawnum.back() & 0x80;
//         rawnum[rawnum.size() - 1] &= 0x7f;
//     }

//     rawnum.reserve(size);
//     while (rawnum.size() < size - 1) {
//         rawnum.push_back(0x00);
//     }

//     rawnum.push_back(signbit);
//     metrics.TallyPushOp(rawnum.size());
// } break;

inline
interpreter::result interpreter::op_num2bin(program& program) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::num2bin};
    }

    // Size parameter is always a small number (max element size), use legacy number
    auto const max_int_sz = program.is_bigint_enabled() ? program.max_integer_size() : program.max_integer_size_legacy();
    auto const size_exp = program.top_number(max_int_sz);
    if ( ! size_exp) {
        return {size_exp.error(), opcode::num2bin};
    }
    auto const size64 = size_exp->int64();
    if (size64 < 0 || size_t(size64) > program.max_script_element_size()) {
        return {error::invalid_operand_size, opcode::num2bin};
    }

    // Drop the size parameter from stack
    program.drop();

    auto& rawnum = program.top(); // now top item after drop
    
    number::minimally_encode(rawnum);

    // Check if the number can be adjusted to the desired size.
    if (rawnum.size() > size64) {
        return {error::impossible_encoding, opcode::num2bin};
    }

    // If the size is already correct, no more is needed.
    if (rawnum.size() == size64) {
        program.get_metrics().add_op_cost(rawnum.size());
        return {};
    }

    // Adjust the size of `rawnum` by adding padding zeros.
    uint8_t signbit = 0x00;
    if ( ! rawnum.empty() && (rawnum.back() & 0x80)) {
        signbit = 0x80;
        rawnum.back() &= 0x7f;
    }

    rawnum.reserve(size64);
    while (rawnum.size() < size64 - 1) {
        rawnum.push_back(0x00);
    }

    rawnum.push_back(signbit);

    program.get_metrics().add_op_cost(rawnum.size());
    return {};
}


// case OP_BIN2NUM: {
//     // (in -- out)
//     if (stack.size() < 1) {
//         return set_error(serror, ScriptError::INVALID_STACK_OPERATION);
//     }

//     valtype &n = stacktop(-1);
//     ScriptNumType::MinimallyEncode(n);
//     metrics.TallyPushOp(n.size());

//     // The resulting number must be a valid number.
//     // Note: IsMinimallyEncoded() here is really just checking if the number is in range.
//     if ( ! ScriptNumType::IsMinimallyEncoded(n, maxIntegerSize)) {
//         return set_error(serror, invalidNumberRangeError);
//     }
// } break;

inline
interpreter::result interpreter::op_bin2num(program& program) {
    // (in -- out)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::bin2num};
    }

    auto& n = program.top();
    number::minimally_encode(n);
    program.get_metrics().add_op_cost(n.size());

    auto const max_int_sz = program.is_bigint_enabled() ? program.max_integer_size() : program.max_integer_size_legacy();
    if ( ! number::is_minimally_encoded(n, max_int_sz)) {
        return {error::invalid_number_encoding, opcode::bin2num};
    }

    return {};
}

inline
interpreter::result interpreter::op_size(program& program) {
    // (in -- in size)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::size};
    }

    auto top = program.pop();
    auto const size = top.size();
    program.push_move(std::move(top));
    auto num_exp = number::from_int(size);
    if ( ! num_exp) {
        return {num_exp.error(), opcode::size};
    }
    auto result_data = num_exp->data();
    auto const result_size = result_data.size();
    program.push_move(std::move(result_data));
    program.get_metrics().add_op_cost(result_size);
    return {};
}

// Disabled
// inline
// interpreter::result interpreter::op_invert(program& program) {
// }


template <bitwise_op Op>
inline
interpreter::result bitwise_operation_generic(program& program, opcode opcode_id, Op op) {
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode_id};
    }

    auto& vch1 = program.item(1);
    auto& vch2 = program.item(0);

    if (vch1.size() != vch2.size()) {
        return {error::operand_size_mismatch, opcode_id};
    }

    for (size_t i = 0; i < vch1.size(); ++i) {
        vch1[i] = op(vch1[i], vch2[i]);
    }

    program.drop();
    program.get_metrics().add_op_cost(vch1.size());

    return {};
}

inline
interpreter::result interpreter::op_invert(program& program) {
    // Bitwise invert all bytes (non-numeric operand and result).
    // (x1 -- ~x1)
    if (program.empty()) {
        return error::insufficient_main_stack;
    }
    auto& data = program.top();
    if (data.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }
    for (auto& ch : data) {
        ch = ~ch;
    }
    program.get_metrics().add_op_cost(data.size());
    return {};
}

inline
interpreter::result interpreter::op_shiftnum(program& program, opcode code) {
    // Arithmetic left/right shift: (num nbits -- out)
    // LSHIFTNUM: out = num * 2^nbits
    // RSHIFTNUM: out = num / 2^nbits (rounded towards negative infinity)
    if ( ! program.is_bigint_enabled()) {
        return error::op_disabled;
    }
    if (program.size() < 2) {
        return error::insufficient_main_stack;
    }
    auto nbits_exp = program.pop_big_number(program.max_integer_size());
    if ( ! nbits_exp) return {nbits_exp.error(), code};
    auto const nbits_i32 = nbits_exp->to_int32_saturating();
    if (nbits_i32 < 0) {
        return {error::invalid_operand_size, code};
    }
    auto const nbits = static_cast<uint64_t>(nbits_i32);
    if (nbits == 0) {
        // Validate top but no-op — number stays on stack.
        auto validate = program.top_big_number(program.max_integer_size());
        if ( ! validate) return {validate.error(), code};
        program.get_metrics().add_op_cost(program.top().size());
    } else {
        auto num_exp = program.pop_big_number(program.max_integer_size());
        if ( ! num_exp) return {num_exp.error(), code};
        auto& num = *num_exp;
        if (code == opcode::op_lshiftnum) {
            num <<= static_cast<int>(nbits);
        } else {
            num >>= static_cast<int>(nbits);
        }
        auto result_data = num.data();
        if (result_data.size() > program.max_script_element_size()) {
            return {error::invalid_push_data_size, code};
        }
        program.get_metrics().add_op_cost(result_data.size());
        program.push_move(std::move(result_data));
    }
    return {};
}

inline
interpreter::result interpreter::op_shiftbin(program& program, opcode code) {
    // Binary left/right shift of a byte blob: (in nbits -- out)
    if (program.size() < 2) {
        return error::insufficient_main_stack;
    }
    auto nbits_exp = program.is_bigint_enabled()
        ? program.pop_big_number(program.max_integer_size())
        : program.pop_big_number(program.max_integer_size_legacy());
    if ( ! nbits_exp) return {nbits_exp.error(), code};
    auto const nbits_i32 = nbits_exp->to_int32_saturating();
    if (nbits_i32 < 0) {
        return {error::invalid_operand_size, code};
    }
    auto data = program.pop();
    auto const nbits = static_cast<size_t>(nbits_i32);
    auto const total_bits = data.size() * 8;

    // BCHN treats the blob as big-endian (byte 0 = MSB).
    if (nbits >= total_bits) {
        std::fill(data.begin(), data.end(), uint8_t{0});
    } else if (nbits > 0) {
        auto const byte_shift = nbits / 8;
        auto const bit_shift = nbits % 8;
        bool const right = (code == opcode::op_rshiftbin);
        if (right) {
            // Right shift (big-endian): move bytes toward higher indices, zero front.
            if (byte_shift > 0) {
                std::memmove(data.data() + byte_shift, data.data(), data.size() - byte_shift);
                std::memset(data.data(), 0, byte_shift);
            }
            if (bit_shift > 0) {
                uint8_t carry = 0;
                for (auto& byte : data) {
                    uint8_t new_carry = byte << (8 - bit_shift);
                    byte = (byte >> bit_shift) | carry;
                    carry = new_carry;
                }
            }
        } else {
            // Left shift (big-endian): move bytes toward lower indices, zero end.
            if (byte_shift > 0) {
                std::memmove(data.data(), data.data() + byte_shift, data.size() - byte_shift);
                std::memset(data.data() + data.size() - byte_shift, 0, byte_shift);
            }
            if (bit_shift > 0) {
                uint8_t carry = 0;
                for (auto it = data.rbegin(); it != data.rend(); ++it) {
                    uint8_t new_carry = *it >> (8 - bit_shift);
                    *it = (*it << bit_shift) | carry;
                    carry = new_carry;
                }
            }
        }
    }

    if (data.size() > program.max_script_element_size()) {
        return {error::invalid_push_data_size, code};
    }
    program.get_metrics().add_op_cost(data.size());
    program.push_move(std::move(data));
    return {};
}

inline
interpreter::result interpreter::op_and(program& program) {
    return bitwise_operation_generic(program,
        opcode::and_,
        [](uint8_t a, uint8_t b) { return uint8_t(a & b); });
}

inline
interpreter::result interpreter::op_or(program& program) {
    return bitwise_operation_generic(program,
        opcode::or_,
        [](uint8_t a, uint8_t b) { return uint8_t(a | b); });
}

inline
interpreter::result interpreter::op_xor(program& program) {
    return bitwise_operation_generic(program,
        opcode::xor_,
        [](uint8_t a, uint8_t b) { return uint8_t(a ^ b); });
}

inline
interpreter::result interpreter::op_equal(program& program) {
    // (x1 x2 - bool)
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::equal};
    }

    auto val1 = program.pop();
    auto val2 = program.pop();
    bool equal_result = (val1 == val2);

    program.push(equal_result);

    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_equal_verify(program& program) {
    // (x1 x2 - bool)
    if (program.size() < 2) {
        return {error::insufficient_main_stack, opcode::equalverify};
    }
    auto const res = program.pop() == program.pop(); //res is a bool
    program.get_metrics().add_op_cost(res ? 1 : 0);

    if ( ! res) {
        // BCHN: pushes false, then VERIFY fails — false stays on stack.
        program.push(false);
        return {error::verify_failed, opcode::equalverify};
    }
    return {};
}

inline
interpreter::result interpreter::op_add1(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        // BCHN reads stacktop(-1) without popping; only pops on success.
        auto bn_exp = program.top_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::add1};
        auto& bn = *bn_exp;
        ++bn;
        auto result_data = bn.data();
        if (result_data.size() > program.max_integer_size()) {
            return {error::overflow, opcode::add1};
        }
        program.drop();
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::add1};
        auto& number = *number_exp;
        bool const res = number.safe_add(1);
        if ( ! res) {
            return {error::overflow, opcode::add1};
        }
        program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
        program.push_move(number.data());
    }
    return {};
}

inline
interpreter::result interpreter::op_sub1(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        // BCHN reads stacktop(-1) without popping; only pops on success.
        auto bn_exp = program.top_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::sub1};
        auto& bn = *bn_exp;
        --bn;
        auto result_data = bn.data();
        if (result_data.size() > program.max_integer_size()) {
            return {error::underflow, opcode::sub1};
        }
        program.drop();
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::sub1};
        auto& number = *number_exp;
        bool const res = number.safe_sub(1);
        if ( ! res) {
            return {error::underflow, opcode::sub1};
        }
        program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
        program.push_move(number.data());
    }
    return {};
}

inline
interpreter::result interpreter::op_negate(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto bn_exp = program.pop_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::negate};
        auto bn = -(*bn_exp);
        auto result_data = bn.data();
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::negate};
        auto& number = *number_exp;
        number = -number;
        program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
        program.push_move(number.data());
    }
    return {};
}

inline
interpreter::result interpreter::op_abs(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto bn_exp = program.pop_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::abs};
        auto bn = bn_exp->abs();
        auto result_data = bn.data();
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::abs};
        auto& number = *number_exp;
        if (number < 0) {
            number = -number;
        }
        program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
        program.push_move(number.data());
    }
    return {};
}

inline
interpreter::result interpreter::op_not(program& program) {
    if (program.is_bigint_enabled()) {
        auto bn_exp = program.pop_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::not_};
        program.push(bn_exp->is_false());
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::not_};
        program.push(number_exp->is_false());
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_nonzero(program& program) {
    if (program.is_bigint_enabled()) {
        auto bn_exp = program.pop_big_number(program.max_integer_size());
        if ( ! bn_exp) return {bn_exp.error(), opcode::nonzero};
        program.push(bn_exp->is_true());
    } else {
        auto number_exp = program.pop_number(program.max_integer_size_legacy());
        if ( ! number_exp) return {number_exp.error(), opcode::nonzero};
        program.push(number_exp->is_true());
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_add(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::add};
        auto& [first, second] = *operands;
        auto result = first + second;
        auto result_data = result.data();
        if (result_data.size() > program.max_integer_size()) {
            return {error::overflow, opcode::add};
        }
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::add};
        auto& [first, second] = *operands;
        auto result = number::safe_add(first, second);
        if ( ! result) {
            return {error::overflow, opcode::add};
        }
        program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
        program.push_move(result->data());
    }
    return {};
}

inline
interpreter::result interpreter::op_sub(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::sub};
        auto& [first, second] = *operands;
        auto result = second - first;
        auto result_data = result.data();
        if (result_data.size() > program.max_integer_size()) {
            return {error::underflow, opcode::sub};
        }
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::sub};
        auto& [first, second] = *operands;
        auto result = number::safe_sub(second, first);
        if ( ! result) {
            return {error::underflow, opcode::sub};
        }
        program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
        program.push_move(result->data());
    }
    return {};
}

inline
interpreter::result interpreter::op_mul(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::mul};
        auto& [first, second] = *operands;
        // TODO(optimize): byte_count() may differ from serialize().size() — verify semantics match BCHN
        uint32_t const quadratic_op_cost = first.byte_count() * second.byte_count();
        auto result = first * second;
        auto result_data = result.data();
        if (result_data.size() > program.max_integer_size()) {
            return {error::overflow, opcode::mul};
        }
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::mul};
        auto& [first, second] = *operands;
        auto result = number::safe_mul(first, second);
        if ( ! result) {
            return {error::overflow, opcode::mul};
        }
        uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
        program.push_move(result->data());
    }
    return {};
}

inline
interpreter::result interpreter::op_div(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::div};
        auto& [first, second] = *operands;
        if (first == 0) {
            return {error::division_by_zero, opcode::div};
        }
        // TODO(optimize): byte_count() may differ from serialize().size() — verify semantics match BCHN
        uint32_t const quadratic_op_cost = first.byte_count() * second.byte_count();
        auto result = second / first;
        auto result_data = result.data();
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::div};
        auto& [first, second] = *operands;
        if (first == 0) {
            return {error::division_by_zero, opcode::div};
        }
        auto result = second / first;
        uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.push_move(result.data());
        program.get_metrics().add_op_cost(result.data().size() * push_cost_factor);
    }
    return {};
}

inline
interpreter::result interpreter::op_mod(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::mod};
        auto& [first, second] = *operands;
        if (first == 0) {
            return {error::division_by_zero, opcode::mod};
        }
        // TODO(optimize): byte_count() may differ from serialize().size() — verify semantics match BCHN
        uint32_t const quadratic_op_cost = first.byte_count() * second.byte_count();
        auto result = second % first;
        auto result_data = result.data();
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.get_metrics().add_op_cost(result_data.size() * push_cost_factor);
        program.push_move(std::move(result_data));
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::mod};
        auto& [first, second] = *operands;
        if (first == 0) {
            return {error::division_by_zero, opcode::mod};
        }
        auto result = second % first;
        uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
        program.get_metrics().add_op_cost(quadratic_op_cost);
        program.push_move(result.data());
        program.get_metrics().add_op_cost(result.data().size() * push_cost_factor);
    }
    return {};
}

inline
interpreter::result interpreter::op_bool_and(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::booland};
        auto& [first, second] = *operands;
        program.push(first.is_true() && second.is_true());
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::booland};
        auto& [first, second] = *operands;
        program.push(first.is_true() && second.is_true());
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_bool_or(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::boolor};
        auto& [first, second] = *operands;
        program.push(first.is_true() || second.is_true());
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::boolor};
        auto& [first, second] = *operands;
        program.push(first.is_true() || second.is_true());
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_num_equal(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::numequal};
        auto& [first, second] = *operands;
        program.push(first == second);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::numequal};
        auto& [first, second] = *operands;
        program.push(first == second);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_num_equal_verify(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::numequalverify};
        auto& [first, second] = *operands;
        auto const res = first == second;
        // BCHN: pushes result, tallies push cost, then VERIFY checks.
        // false = {} (size 0), true = {0x01} (size 1).
        program.push(res);
        program.get_metrics().add_op_cost(program.top().size());
        if ( ! res) {
            return {error::verify_failed, opcode::numequalverify};
        }
        program.drop();  // VERIFY success: remove the true value
        return {};
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::numequalverify};
        auto& [first, second] = *operands;
        auto const res = first == second;
        program.push(res);
        program.get_metrics().add_op_cost(program.top().size());
        if ( ! res) {
            return {error::verify_failed, opcode::numequalverify};
        }
        program.drop();
        return {};
    }
}

inline
interpreter::result interpreter::op_num_not_equal(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::numnotequal};
        auto& [first, second] = *operands;
        program.push(first != second);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::numnotequal};
        auto& [first, second] = *operands;
        program.push(first != second);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_less_than(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::lessthan};
        auto& [first, second] = *operands;
        program.push(second < first);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::lessthan};
        auto& [first, second] = *operands;
        program.push(second < first);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_greater_than(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::greaterthan};
        auto& [first, second] = *operands;
        program.push(second > first);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::greaterthan};
        auto& [first, second] = *operands;
        program.push(second > first);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_less_than_or_equal(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::lessthanorequal};
        auto& [first, second] = *operands;
        program.push(second <= first);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::lessthanorequal};
        auto& [first, second] = *operands;
        program.push(second <= first);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_greater_than_or_equal(program& program) {
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::greaterthanorequal};
        auto& [first, second] = *operands;
        program.push(second >= first);
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::greaterthanorequal};
        auto& [first, second] = *operands;
        program.push(second >= first);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_min(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::min};
        auto& [first, second] = *operands;
        program.push_move(second < first ? second.data() : first.data());
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::min};
        auto& [first, second] = *operands;
        program.push_move(second < first ? second.data() : first.data());
    }
    program.get_metrics().add_op_cost(program.top().size() * push_cost_factor);
    return {};
}

inline
interpreter::result interpreter::op_max(program& program) {
    constexpr auto push_cost_factor = 2;
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_binary();
        if ( ! operands) return {operands.error(), opcode::max};
        auto& [first, second] = *operands;
        program.push_move(second > first ? second.data() : first.data());
    } else {
        auto operands = program.pop_binary();
        if ( ! operands) return {operands.error(), opcode::max};
        auto& [first, second] = *operands;
        program.push_move(second > first ? second.data() : first.data());
    }
    program.get_metrics().add_op_cost(program.top().size() * push_cost_factor);
    return {};
}

inline
interpreter::result interpreter::op_within(program& program) {
    // (x min max -- out)
    if (program.is_bigint_enabled()) {
        auto operands = program.pop_big_ternary();
        if ( ! operands) return {operands.error(), opcode::within};
        auto& [first, second, third] = *operands;
        program.push(second <= third && third < first);
    } else {
        auto operands = program.pop_ternary();
        if ( ! operands) return {operands.error(), opcode::within};
        auto& [first, second, third] = *operands;
        program.push(second <= third && third < first);
    }
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_ripemd160(program& program) {
    // (in -- hash)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::ripemd160};
    }
    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(ripemd160_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_sha1(program& program) {
    // (in -- hash)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::sha1};
    }
    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(sha1_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_sha256(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::sha256};
    }

    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(sha256_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_hash160(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::hash160};
    }

    program.get_metrics().add_hash_iterations(program.top().size(), true);
    program.push_move(ripemd160_hash_chunk(sha256_hash(program.pop())));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_hash256(program& program) {
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::hash256};
    }

    program.get_metrics().add_hash_iterations(program.top().size(), true);
    program.push_move(sha256_hash_chunk(sha256_hash(program.pop())));
    program.get_metrics().add_op_cost(program.top().size());
    return {};
}

inline
interpreter::result interpreter::op_codeseparator(program& program, operation const& op) {
    return program.set_jump_register(op, +1) ? error::success : error::invalid_script;
}

// Helper function to validate public key encoding
inline
bool is_compressed_or_uncompressed_pubkey(data_chunk const& public_key) {
    switch (public_key.size()) {
        case kth::ec_compressed_size:
            // Compressed public key: must start with 0x02 or 0x03.
            return public_key[0] == 0x02 || public_key[0] == 0x03;
        case kth::ec_uncompressed_size:
            // Non-compressed public key: must start with 0x04.
            return public_key[0] == 0x04;
        default:
            // Non-canonical public key: invalid size.
            return false;
    }
}

// Helper function to check public key encoding according to STRICTENC rules
inline
interpreter::result check_pubkey_encoding(data_chunk const& public_key, program const& program) {
    // Check if STRICTENC is enabled
    auto const strictenc_enabled = chain::script::is_enabled(program.flags(), script_flags::bch_strictenc);
    auto const is_valid_pubkey = is_compressed_or_uncompressed_pubkey(public_key);

    if (strictenc_enabled && !is_valid_pubkey) {
        return error::pubkey_type;  // PUBKEYTYPE equivalent
    }
    return error::success;
}

// Pure DER format validation matching BCHN's IsValidSignatureEncoding.
// When has_sighash_byte=true: validates transaction signatures (includes trailing sighash byte).
// When has_sighash_byte=false: validates data signatures (pure DER, no sighash byte).
inline
bool is_valid_signature_encoding(data_chunk const& sig, bool has_sighash_byte) {
    // Minimum: 0x30 + len + 0x02 + rlen + R(1) + 0x02 + slen + S(1) [+ sighash]
    auto const min_size = has_sighash_byte ? 9u : 8u;
    auto const max_size = has_sighash_byte ? 73u : 72u;

    if (sig.size() < min_size || sig.size() > max_size) {
        return false;
    }

    // A signature is of type 0x30 (compound).
    if (sig[0] != 0x30) {
        return false;
    }

    // Make sure the length covers the entire signature.
    // total-length = everything after [0x30, total-length] and before [sighash].
    auto const overhead = has_sighash_byte ? 3u : 2u;
    if (sig[1] != sig.size() - overhead) {
        return false;
    }

    // Extract the length of the R element.
    unsigned int const lenR = sig[3];

    // Make sure the length of the S element is still inside the signature.
    if (5u + lenR >= sig.size()) {
        return false;
    }

    // Extract the length of the S element.
    unsigned int const lenS = sig[lenR + 5];

    // Verify that the length of the signature matches the sum of the length
    // of the elements. total = 0x30 + len + 0x02 + R_len + R + 0x02 + S_len + S [+ sighash]
    auto const total_overhead = has_sighash_byte ? 7u : 6u;
    if ((lenR + lenS + total_overhead) != sig.size()) {
        return false;
    }

    // Check whether the R element is an integer.
    if (sig[2] != 0x02) {
        return false;
    }

    // Zero-length integers are not allowed for R.
    if (lenR == 0) {
        return false;
    }

    // Negative numbers are not allowed for R.
    if (sig[4] & 0x80) {
        return false;
    }

    // Null bytes at the start of R are not allowed, unless R would otherwise
    // be interpreted as a negative number.
    if (lenR > 1 && (sig[4] == 0x00) && !(sig[5] & 0x80)) {
        return false;
    }

    // Check whether the S element is an integer.
    if (sig[lenR + 4] != 0x02) {
        return false;
    }

    // Zero-length integers are not allowed for S.
    if (lenS == 0) {
        return false;
    }

    // Negative numbers are not allowed for S.
    if (sig[lenR + 6] & 0x80) {
        return false;
    }

    // Null bytes at the start of S are not allowed, unless S would otherwise
    // be interpreted as a negative number.
    if (lenS > 1 && (sig[lenR + 6] == 0x00) && !(sig[lenR + 7] & 0x80)) {
        return false;
    }

    return true;
}

// Check transaction signature encoding (DER, low-S, hashtype, forkid).
// Mirrors BCHN's CheckTransactionSignatureEncoding.
inline
interpreter::result check_transaction_signature_encoding(data_chunk const& endorsement, program const& program) {
    using namespace kth::infrastructure::machine;

    // Empty signature is always allowed (compact way to provide invalid sig).
    if (endorsement.empty()) {
        return error::success;
    }

    auto const flags = program.flags();
    auto const [dersig, strictenc, low_s] = chain::script::are_enabled(flags,
        script_flags::bip66_rule, script_flags::bch_strictenc, script_flags::bch_low_s);

    // Schnorr endorsements (64 sig + 1 sighash = 65 bytes) skip DER/LOW_S checks.
    // BCHN: CheckTransactionSignatureEncoding calls CheckRawSignatureEncoding
    // which skips DER validation for 64-byte (Schnorr-length) signatures.
    bool const is_schnorr = (endorsement.size() == schnorr_signature_size + 1);

    if ( ! is_schnorr) {
        // DER format validation (BCHN: IsValidSignatureEncoding).
        // Triggered by DERSIG, LOW_S, or STRICTENC flags.
        if ((dersig || low_s || strictenc) && !is_valid_signature_encoding(endorsement, true)) {
            return error::invalid_signature_lax_encoding;
        }

        // LOW_S check (BCHN: CPubKey::CheckLowS).
        if (low_s) {
            // Extract DER signature (without sighash byte) for low-S check.
            der_signature der_sig(endorsement.begin(), endorsement.end() - 1);
            if ( ! check_low_s(der_sig)) {
                return error::sig_high_s;
            }
        }
    }

    // Sighash encoding check (BCHN: CheckSighashEncoding)
    if (strictenc) {
        auto const sighash_byte = endorsement.back();
        // Match BCHN isDefined(): strip forkid, anyonecanpay, AND utxos before checking base type.
        auto const base_type = sighash_byte & ~(sighash_algorithm::forkid | sighash_algorithm::anyone_can_pay | sighash_algorithm::utxos);
        auto const has_forkid = (sighash_byte & sighash_algorithm::forkid) != 0;
        auto const has_utxos = (sighash_byte & sighash_algorithm::utxos) != 0;
        auto const has_anyone_can_pay = (sighash_byte & sighash_algorithm::anyone_can_pay) != 0;
        auto const forkid_enabled = chain::script::is_enabled(flags, script_flags::bch_sighash_forkid);

        // Check base sighash type is defined (all=1, none=2, single=3)
        if (base_type < sighash_algorithm::all || base_type > sighash_algorithm::single) {
            return error::sig_hashtype;
        }

        // Forkid enforcement
        if ( ! forkid_enabled && has_forkid) {
            return error::illegal_forkid;
        }
        if (forkid_enabled && ! has_forkid) {
            return error::must_use_forkid;
        }

        // UTXOS validation (BCHN: CheckSighashEncoding, post-isDefined block)
        // SIGHASH_UTXOS (0x20) requires: tokens enabled, forkid used, forkid enabled, no anyonecanpay.
        if (has_utxos) {
            auto const tokens_enabled = chain::script::is_enabled(flags, script_flags::bch_tokens);
            if ( ! tokens_enabled || ! has_forkid || ! forkid_enabled || has_anyone_can_pay) {
                return error::sig_hashtype;
            }
        }
    }

    return error::success;
}

// Check data signature encoding (DER, low-S) for OP_CHECKDATASIG.
// Mirrors BCHN's CheckDataSignatureEncoding.
inline
interpreter::result check_data_signature_encoding(data_chunk const& sig, program const& program) {
    // Empty signature is always allowed.
    if (sig.empty()) {
        return error::success;
    }

    auto const flags = program.flags();
    auto const [dersig, strictenc, low_s] = chain::script::are_enabled(flags,
        script_flags::bip66_rule, script_flags::bch_strictenc, script_flags::bch_low_s);

    // DER format validation (no sighash byte for data signatures).
    if ((dersig || low_s || strictenc) && !is_valid_signature_encoding(sig, false)) {
        return error::invalid_signature_lax_encoding;
    }

    // LOW_S check.
    if (low_s) {
        der_signature der_sig(sig.begin(), sig.end());
        if ( ! check_low_s(der_sig)) {
            return error::sig_high_s;
        }
    }

    return error::success;
}

inline
std::pair<interpreter::result, size_t> op_check_sig_common(program& program, opcode op_code) {
    if (program.size() < 2) {
        return {{error::insufficient_main_stack, op_code}, 0};
    }

    auto const public_key = program.pop();
    auto endorsement = program.pop();

    // Check signature encoding (DER, hashtype, forkid, low-S) before any verification.
    // BCHN: CheckTransactionSignatureEncoding
    auto const sig_enc_result = check_transaction_signature_encoding(endorsement, program);
    if (sig_enc_result != error::success) {
        return {sig_enc_result, 0};
    }

    // Validate public key encoding (STRICTENC).
    auto const pubkey_result = check_pubkey_encoding(public_key, program);
    if (pubkey_result != error::success) {
        return {pubkey_result, 0};
    }

    // Create a subscript, then apply FindAndDelete for pre-forkid scripts (BCHN: CleanupScriptCode).
    chain::script script_code(program.subscript());
    if (should_find_and_delete(endorsement, program.flags())) {
        script_code = find_and_delete_endorsement(script_code, endorsement);
    }

    // Empty signature: not an encoding error, just a failed verification.
    if (endorsement.empty()) {
        return {error::incorrect_signature, 0};
    }

    // Parse endorsement (sighash byte + signature).
    uint8_t sighash;
    der_signature distinguished;
    if ( ! parse_endorsement(sighash, distinguished, std::move(endorsement))) {
        return {error::invalid_signature_encoding, 0};
    }

    bool const is_schnorr_sig = (distinguished.size() == schnorr_signature_size);

    if (is_schnorr_sig) {
        // Schnorr verification path
        auto const [sighash_digest, size] = chain::script::generate_signature_hash(
            program.transaction(),
            program.input_index(),
            script_code,
            sighash,
            program.flags(),
            program.value());

        bool const verified = verify_schnorr(
            byte_span{public_key.data(), public_key.size()},
            sighash_digest,
            byte_span{distinguished.data(), distinguished.size()});

        // BCHN: TallySigChecks(1) and TallyHashOp are called whenever sig is non-empty,
        // regardless of verification success/failure, and before the NULLFAIL check.
        program.get_metrics().add_sig_checks(1);
        program.get_metrics().add_hash_iterations(size, true);

        if ( ! verified) {
            if (chain::script::is_enabled(program.flags(), script_flags::bch_nullfail)) {
                return {error::sig_nullfail, size};
            }
            return {error::incorrect_signature, size};
        }
        return {error::success, size};
    }

    // ECDSA verification path
    ec_signature signature;
    auto const strict = chain::script::is_enabled(program.flags(), script_flags::bip66_rule | script_flags::bch_strictenc);
    if ( ! parse_signature(signature, distinguished, strict)) {
        return {strict ? error::invalid_signature_lax_encoding : error::invalid_signature_encoding, 0};
    }

#if ! defined(KTH_CURRENCY_BCH)
    auto const version = chain::script::is_enabled(program.flags(), script_flags::bip143_rule)
        ? program.version() : script_version::unversioned;
#endif

    auto const [res, size] = chain::script::check_signature(
        signature,
        sighash,
        public_key,
        script_code,
        program.transaction(),
        program.input_index(),
        program.flags(),
#if ! defined(KTH_CURRENCY_BCH)
        version,
#endif
        program.value()
    );

    // BCHN: TallySigChecks(1) and TallyHashOp are called whenever sig is non-empty,
    // regardless of verification success/failure, and before the NULLFAIL check.
    program.get_metrics().add_sig_checks(1);
    if (size > 0) {
        program.get_metrics().add_hash_iterations(size, true);
    }

    if ( ! res) {
        if (chain::script::is_enabled(program.flags(), script_flags::bch_nullfail)) {
            return {error::sig_nullfail, size};
        }
        return {error::incorrect_signature, size};
    }
    return {error::success, size};
}

inline
interpreter::result interpreter::op_check_sig(program& program) {
    auto const [res, size] = op_check_sig_common(program, opcode::checksig);

    if ( ! is_signature_result(res)) {
        return res;
    }

    program.push(res == error::success);
    // TallyPushOp: account for the pushed bool.
    // sig_checks and hash_iterations are already tallied inside op_check_sig_common.
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_check_sig_verify(program& program) {
    auto const [res, size] = op_check_sig_common(program, opcode::checksigverify);

    // Hard errors (encoding violations, nullfail, etc.) propagate directly.
    if ( ! is_signature_result(res)) {
        return res;
    }

    // TallyPushOp: BCHN pushes bool then TallyPushOp(1) before the VERIFY pop;
    // we skip the push/pop but must account for the same cost.
    // sig_checks and hash_iterations are already tallied inside op_check_sig_common.
    program.get_metrics().add_op_cost(1);

    if (res != error::success) {
        // CHECKSIGVERIFY failure: BCHN returns SCRIPT_ERR_CHECKSIGVERIFY.
        return {error::invalid_script, opcode::checksigverify};
    }

    return error::success;
}

inline
// Returns {result, message_size} — message_size is needed for hash iteration metrics.
std::pair<interpreter::result, size_t> op_check_data_common(program& program, opcode op_code) {
    // (sig message pubkey -- bool)
    if (program.size() < 3) {
        return {{error::insufficient_main_stack, op_code}, 0};
    }

    auto const public_key = program.pop();
    auto const message = program.pop();
    auto const sig = program.pop();

    // Validate signature encoding (DER format + low-S) — BCHN: CheckDataSignatureEncoding.
    // Schnorr signatures (64 bytes) skip DER/low-S checks.
    if ( ! sig.empty() && sig.size() != schnorr_signature_size) {
        auto const sig_enc_result = check_data_signature_encoding(sig, program);
        if (sig_enc_result != error::success) {
            return {sig_enc_result, 0};
        }
    }

    // Validate public key encoding
    auto const pubkey_result = check_pubkey_encoding(public_key, program);
    if (pubkey_result != error::success) {
        return {pubkey_result, 0};
    }

    auto const message_size = message.size();

    bool success = false;
    if ( ! sig.empty()) {
        // Hash the message with SHA256
        auto const msg_hash = sha256_hash(byte_span{message.data(), message.size()});

        if (sig.size() == schnorr_signature_size) {
            // Schnorr verification
            success = verify_schnorr(
                byte_span{public_key.data(), public_key.size()},
                msg_hash,
                byte_span{sig.data(), sig.size()});
        } else {
            // ECDSA verification - parse DER signature
            ec_signature ec_sig;
            der_signature der_sig(sig.begin(), sig.end());
            auto const bip66 = chain::script::is_enabled(program.flags(), script_flags::bip66_rule);
            if (parse_signature(ec_sig, der_sig, bip66)) {
                success = verify_signature(
                    byte_span{public_key.data(), public_key.size()},
                    msg_hash,
                    ec_sig);
            }
        }

        // BCHN: TallySigChecks(1) and TallyHashOp are called whenever sig is non-empty,
        // regardless of verification success/failure, and before the NULLFAIL check.
        program.get_metrics().add_sig_checks(1);
        program.get_metrics().add_hash_iterations(message_size, false);

        // NULLFAIL: non-empty signature that fails verification must error
        if ( ! success && chain::script::is_enabled(program.flags(), script_flags::bch_nullfail)) {
            return {error::sig_nullfail, message_size};
        }
    }

    if (success) {
        return {error::success, message_size};
    }
    return {error::incorrect_signature, message_size};
}

inline
interpreter::result interpreter::op_check_data_sig(program& program) {
    auto const [res, message_size] = op_check_data_common(program, opcode::checkdatasig);

    if ( ! is_signature_result(res)) {
        return res;
    }

    bool const success = (res == error::success);
    program.push(success);

    // TallyPushOp: account for the pushed bool.
    // sig_checks and hash_iterations are already tallied inside op_check_data_common.
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_check_data_sig_verify(program& program) {
    auto const [res, message_size] = op_check_data_common(program, opcode::checkdatasigverify);

    if ( ! is_signature_result(res)) {
        return res;
    }

    // TallyPushOp: BCHN pushes bool then TallyPushOp(1) before the VERIFY pop.
    // sig_checks and hash_iterations are already tallied inside op_check_data_common.
    program.get_metrics().add_op_cost(1);

    if (res != error::success) {
        return {error::invalid_script, opcode::checkdatasigverify};
    }

    return error::success;
}

inline
interpreter::result op_check_multisig_internal(program& program) {
    // Combine Knuth structure with BCHN behavior
    
    // Step 1: Pop key_count (Knuth style)
    auto const key_count_exp = program.pop_int32();
    if ( ! key_count_exp) {
        return {key_count_exp.error(), opcode::checkmultisig};
    }
    auto const key_count = *key_count_exp;

    if (key_count < 0 || key_count > 20) { // MAX_PUBKEYS_PER_MULTISIG
        return {error::invalid_operand_size, opcode::checkmultisig};
    }

    // Account for operation count (pre-May 2025 behavior)
    if ( ! program.increment_operation_count(key_count)) {
        return {error::invalid_operation_count, opcode::checkmultisig};
    }

    // Step 2: Pop public keys (Knuth style)
    auto public_keys = program.pop(key_count);
    if ( ! public_keys) {
        return {public_keys.error(), opcode::checkmultisig};
    }

    // Step 3: Pop signature_count (Knuth style)
    auto const sig_count_exp = program.pop_int32();
    if ( ! sig_count_exp) {
        return {sig_count_exp.error(), opcode::checkmultisig};
    }
    auto const signature_count = *sig_count_exp;

    if (signature_count < 0 || signature_count > key_count) {
        return {error::invalid_operand_size, opcode::checkmultisig};
    }

    // Step 4: Pop signatures (Knuth style)
    auto endorsements = program.pop(signature_count);
    if ( ! endorsements) {
        return {endorsements.error(), opcode::checkmultisig};
    }

    // Step 5: Handle Satoshi bug (Knuth style)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::checkmultisig};
    }

    //*************************************************************************
    // CONSENSUS: Satoshi bug, discard stack element, malleable until bip147
    //            in BTC. bip147 is disabled in BCH.
    //*************************************************************************
#if defined(KTH_CURRENCY_BCH)
    // BCH: pop the dummy element (used as checkbits in Schnorr multisig path)
    auto const dummy = program.pop();
#else
    if ( ! program.pop().empty()
        && chain::script::is_enabled(program.flags(), script_flags::bip147_rule)
    ) {
        return error::multisig_satoshi_bug;
    }
#endif

#if defined(KTH_CURRENCY_BCH)
    // BCHN: if SCHNORR_MULTISIG is enabled and dummy is non-empty, use Schnorr path
    bool const schnorr_multisig_enabled = chain::script::is_enabled(program.flags(), script_flags::bch_schnorr_multisig);
    if (schnorr_multisig_enabled && ! dummy.empty()) {
        // --- SCHNORR MULTISIG PATH (BCHN) ---
        static_assert(20 < 32, "Schnorr multisig checkbits implementation assumes < 32 pubkeys.");

        // Decode dummy as a little-endian bitfield
        auto const bitfield_size = (static_cast<size_t>(key_count) + 7) / 8;
        if (dummy.size() != bitfield_size) {
            return error::invalid_script;  // BITFIELD_SIZE
        }

        uint32_t check_bits = 0;
        for (size_t i = 0; i < bitfield_size; ++i) {
            check_bits |= uint32_t(dummy[i]) << (8 * i);
        }

        // Verify no bits set beyond key_count
        uint32_t const mask = (uint64_t(1) << key_count) - 1;
        if ((check_bits & mask) != check_bits) {
            return error::invalid_script;  // BIT_RANGE
        }

        // The bitfield must set exactly sig_count bits
        if (std::popcount(check_bits) != signature_count) {
            return error::invalid_script;  // INVALID_BIT_COUNT
        }

        chain::script const script_code(program.subscript());

        // pop() returns [0]=top-of-stack, but BCHN's bitfield indexes from the bottom (first pushed).
        // bottom_index() maps BCHN-style bottom-up indices to our top-down vector indices.
        auto const& keys = *public_keys;
        auto const& sigs = *endorsements;
        auto const bottom_key = [&](int i) -> data_chunk const& { return keys[key_count - 1 - i]; };
        auto const bottom_sig = [&](int i) -> data_chunk const& { return sigs[signature_count - 1 - i]; };

        int ikey = 0;
        for (int isig = 0; isig < signature_count; ++isig, ++ikey) {
            // Sanity check
            if ((check_bits >> ikey) == 0) {
                return error::invalid_script;  // BIT_RANGE (unreachable)
            }

            // Find the next key indicated by the bitfield
            while (((check_bits >> ikey) & 0x01) == 0) {
                ++ikey;
            }

            if (ikey >= key_count) {
                return error::invalid_script;  // PUBKEY_COUNT (unreachable)
            }

            auto const& endorsement = bottom_sig(isig);
            auto const& pub_key = bottom_key(ikey);

            // Check signature encoding (sighash, forkid) — BCHN: CheckTransactionSignatureEncoding
            auto const sig_enc_result = check_transaction_signature_encoding(endorsement, program);
            if (sig_enc_result != error::success) {
                return sig_enc_result;
            }

            // Check Schnorr signature encoding
            if (endorsement.empty()) {
                // Empty sig in Schnorr multisig means bitfield should have been null
                return error::sig_nullfail;
            }

            // Validate pubkey encoding
            auto const pubkey_result = check_pubkey_encoding(pub_key, program);
            if (pubkey_result != error::success) {
                return pubkey_result;
            }

            // Parse endorsement: last byte is sighash type, first 64 bytes are signature
            uint8_t sighash;
            der_signature distinguished;
            if ( ! parse_endorsement(sighash, distinguished, endorsement)) {
                return error::invalid_signature_encoding;
            }

            // Must be Schnorr (64 bytes after removing sighash byte)
            if (distinguished.size() != schnorr_signature_size) {
                return error::sig_nonschnorr;
            }

            // Verify Schnorr signature
            auto const [sighash_digest, size] = chain::script::generate_signature_hash(
                program.transaction(),
                program.input_index(),
                script_code,
                sighash,
                program.flags(),
                program.value());

            if ( ! verify_schnorr(
                    byte_span{pub_key.data(), pub_key.size()},
                    sighash_digest,
                    byte_span{distinguished.data(), distinguished.size()})) {
                return error::sig_nullfail;
            }

            // BCHN: TallySigChecks(1) per Schnorr signature verified
            program.get_metrics().add_sig_checks(1);
            program.get_metrics().add_hash_iterations(size, true);
        }

        // Verify all bits consumed
        if ((check_bits >> ikey) != 0) {
            return error::invalid_script;  // INVALID_BIT_COUNT (unreachable)
        }

        return error::success;
    }
#endif // KTH_CURRENCY_BCH

    // Early return for zero signatures (no verification needed).
    if (signature_count == 0) {
        return error::success;
    }

    // --- LEGACY MULTISIG PATH (ECDSA) ---
    uint8_t sighash;
    ec_signature signature;
    der_signature distinguished;
    auto public_key = public_keys->begin();
    auto const strict = chain::script::is_enabled(program.flags(), script_flags::bip66_rule | script_flags::bch_strictenc);

    // BCHN: CleanupScriptCode — apply FindAndDelete for ALL endorsements before verification.
    // This must happen upfront (matching BCHN's pre-loop cleanup).
    chain::script script_code(program.subscript());
    for (auto const& endorsement : *endorsements) {
        if (should_find_and_delete(endorsement, program.flags())) {
            script_code = find_and_delete_endorsement(script_code, endorsement);
        }
    }

    int sigs_remaining = signature_count;
    int keys_remaining = key_count;
    auto sig_iter = endorsements->begin();

    bool success = true;
    while (success && sigs_remaining > 0) {
        auto& endorsement = *sig_iter;

        bool is_empty_signature = endorsement.empty();

        // Check signature encoding (sighash, forkid) — BCHN: CheckTransactionSignatureEncoding
        auto const sig_enc_result = check_transaction_signature_encoding(endorsement, program);
        if (sig_enc_result != error::success) {
            return sig_enc_result;
        }

        // In ECDSA-only context (legacy multisig), reject Schnorr-length signatures (64+1 sighash byte = 65)
        if ( ! is_empty_signature && endorsement.size() == schnorr_signature_size + 1) {
            return error::sig_badlength;
        }

        if (!is_empty_signature && ! parse_endorsement(sighash, distinguished, endorsement)) {
            return error::invalid_signature_encoding;
        }

        if (!is_empty_signature) {
            if ( ! parse_signature(signature, distinguished, strict)) {
                return strict ? error::invalid_signature_lax_encoding : error::invalid_signature_encoding;
            }
        }

        auto const pubkey_result = check_pubkey_encoding(*public_key, program);
        if (pubkey_result != error::success) {
            return pubkey_result;
        }

#if ! defined(KTH_CURRENCY_BCH)
        auto version = chain::script::is_enabled(program.flags(), script_flags::bip143_rule)
            ? program.version() : script_version::unversioned;
#endif // ! KTH_CURRENCY_BCH

        bool sig_verified = false;
        if ( ! is_empty_signature) {
            auto const [res, size] = chain::script::check_signature(
                signature,
                sighash,
                *public_key,
                script_code,
                program.transaction(),
                program.input_index(),
                program.flags(),
#if ! defined(KTH_CURRENCY_BCH)
                version,
#endif // ! KTH_CURRENCY_BCH
                program.value()
            );
            sig_verified = res;
        }

        if (sig_verified) {
            ++sig_iter;
            --sigs_remaining;
        }

        ++public_key;
        --keys_remaining;

        if (sigs_remaining > keys_remaining) {
            success = false;
        }
    }

    // BCHN: If failed and NULLFAIL enabled, all signatures must be empty
#if defined(KTH_CURRENCY_BCH)
    if ( ! success && chain::script::is_enabled(program.flags(), script_flags::bch_nullfail)) {
        for (auto const& endorsement : *endorsements) {
            if ( ! endorsement.empty()) {
                return error::sig_nullfail;
            }
        }
    }

    // BCHN: ECDSA CHECKMULTISIG counts nKeysCount sig_checks (not nSigsCount),
    // but only if not all signatures are null.
    {
        bool all_sigs_null = true;
        for (auto const& endorsement : *endorsements) {
            if ( ! endorsement.empty()) {
                all_sigs_null = false;
                break;
            }
        }
        if ( ! all_sigs_null) {
            program.get_metrics().add_sig_checks(key_count);
        }
    }
#endif

    return success ? error::success : error::incorrect_signature;
}

inline
interpreter::result interpreter::op_check_multisig(program& program) {

    auto const res = op_check_multisig_internal(program);

    if ( ! is_signature_result(res)) {
        return res;
    }

    // Push the result (true for success, false for failure) onto the stack
    auto const success = (res == error::success);
    program.push(success);

    program.get_metrics().add_op_cost(program.top().size());

    // sig_checks are tallied inside op_check_multisig_internal
    // (Schnorr: 1 per sig, ECDSA: nKeysCount if any sig is non-null)

    return error::success;
}

inline
interpreter::result interpreter::op_check_multisig_verify(program& program) {
        // First run the multisig operation
    auto const res = op_check_multisig(program);
    if (res != error::success) {
        return res;
    }
    
    // Then verify the result (like OP_VERIFY)
    if (program.empty()) {
        return {error::insufficient_main_stack, opcode::checkmultisigverify};
    }

    bool const stack_result = program.stack_true(false);

    if ( ! stack_result) {
        return {error::verify_failed, opcode::checkmultisigverify};
    }

    program.drop();
    return error::success;
}

inline
interpreter::result interpreter::op_check_locktime_verify(program& program) {
    // BIP65: nop2 subsumed by checklocktimeverify when bip65 fork is active.

    if ( ! chain::script::is_enabled(program.flags(), script_flags::bip65_rule)) {
        return op_nop(opcode::nop2);
    }

    auto const& tx = program.transaction();
    auto const input_index = program.input_index();

    if (input_index >= tx.inputs().size()) {
        return error::invalid_script;
    }

    // MINIMALNUM: check minimal encoding of stack top (before parsing as number)
    if ( ! program.empty() && chain::script::is_enabled(program.flags(), script_flags::bch_minimaldata)) {
        if ( ! number::is_minimally_encoded(program.top(), max_check_locktime_verify_number_size)) {
            return error::minimal_number;
        }
    }

    // BIP65: the tx sequence is 0xffffffff.
    if (tx.inputs()[input_index].is_final()) {
        return error::unsatisfied_locktime;
    }

    // BIP65: the stack is empty.
    // BIP65: extend the (signed) script number range to 5 bytes.

    auto const stack_exp = program.top_number(max_check_locktime_verify_number_size);
    if ( ! stack_exp) {
        return error::invalid_script;
    }
    auto const& stack = *stack_exp;

    // BIP65: the top stack item is negative.
    if (stack < 0) {
        return error::negative_locktime;
    }

    // The top stack item is positive, so cast is safe.
    auto const locktime = uint64_t(stack.int64());

    // BIP65: the stack locktime type differs from that of tx.
    if ((locktime < locktime_threshold) != (tx.locktime() < locktime_threshold)) {
        return error::unsatisfied_locktime;
    }

    // BIP65: the stack locktime is greater than the tx locktime.
    return (locktime > tx.locktime()) ? 
        error::unsatisfied_locktime : 
        error::success;
}

inline
interpreter::result interpreter::op_check_sequence_verify(program& program) {
    // BIP112: nop3 subsumed by checksequenceverify when bip112 fork is active.
    if ( ! chain::script::is_enabled(program.flags(), script_flags::bip112_rule)) {
        return op_nop(opcode::nop3);
    }

    auto const& tx = program.transaction();
    auto const input_index = program.input_index();

    if (input_index >= tx.inputs().size()) {
        return error::invalid_script;
    }

    // MINIMALNUM: check minimal encoding of stack top (before parsing as number)
    if ( ! program.empty() && chain::script::is_enabled(program.flags(), script_flags::bch_minimaldata)) {
        if ( ! number::is_minimally_encoded(program.top(), max_check_sequence_verify_number_size)) {
            return error::minimal_number;
        }
    }

    // BIP112: the stack is empty.
    // BIP112: extend the (signed) script number range to 5 bytes.
    auto const stack_exp = program.top_number(max_check_sequence_verify_number_size);
    if ( ! stack_exp) {
        return error::insufficient_main_stack;  // BCHN: INVALID_STACK_OPERATION
    }
    auto const& stack = *stack_exp;

    // BIP112: the top stack item is negative.
    if (stack < 0) {
        return error::negative_locktime;
    }

    // The top stack item is positive, so cast is safe.
    auto const sequence = uint64_t(stack.int64());

    // BIP112: the stack sequence is disabled, treat as nop3.
    if ((sequence & relative_locktime_disabled) != 0) {
        return op_nop(opcode::nop3);
    }

    // BIP112: the stack sequence is enabled and tx version less than 2.
    if (tx.version() < relative_locktime_min_version) {
        return error::unsatisfied_locktime;
    }

    auto const tx_sequence = tx.inputs()[input_index].sequence();

    // BIP112: the transaction sequence is disabled.
    if ((tx_sequence & relative_locktime_disabled) != 0) {
        return error::unsatisfied_locktime;
    }

    // BIP112: the stack sequence type differs from that of tx input.
    if ((sequence & relative_locktime_time_locked) !=
        (tx_sequence & relative_locktime_time_locked)) {
        return error::unsatisfied_locktime;
    }

    // BIP112: the masked stack sequence is greater than the tx sequence.
    return (sequence & relative_locktime_mask) >
                   (tx_sequence & relative_locktime_mask)
               ? error::unsatisfied_locktime
               : error::success;
}

// Native Introspection helper functions
//-----------------------------------------------------------------------------

inline
interpreter::result interpreter::validate_native_introspection(program const& program) {
    // Check if native introspection is enabled
    if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_native_introspection)) {
        return error::op_reserved;
    }

    // Check if context is available
    auto const& context = program.context();
    if ( ! context) {
        return error::context_not_present;
    }

    return error::success;
}

inline
interpreter::result interpreter::validate_token_introspection(program const& program) {
    auto const res = validate_native_introspection(program);
    if (res != error::success) {
        return res;
    }
    // Token introspection opcodes require bch_tokens (Descartes/2023).
    if ( ! chain::script::is_enabled(program.flags(), script_flags::bch_tokens)) {
        return error::op_reserved;
    }
    return error::success;
}

inline
void interpreter::post_process_introspection_push(program& program, data_chunk const& data) {
    // Tally push cost (equivalent to metrics.TallyPushOp(stack.back().size()) in BCHN)
    program.get_metrics().add_op_cost(data.size());
}

inline
interpreter::result interpreter::op_input_index(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    auto const& context = program.context();
    auto const input_index = int64_t(context->input_index());
    auto const bn_exp = number::from_int(input_index);
    if ( ! bn_exp) {
        return error::overflow;
    }
    
    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);

    return error::success;
}

inline
interpreter::result interpreter::op_active_bytecode(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    // Subset of active script starting at the most recent code separator (if any)
    // or the entire script if no code separators are present.
    auto const& active = program.get_script();
    auto const begin_code_hash = program.jump();
    auto const script_end = active.end();
    
    // Calculate the size of the active bytecode
    auto const script_size = size_t(script_end - begin_code_hash);
    
    // Check maximum script element size constraint
    auto const max_script_element_size = program.max_script_element_size();
    if (script_size > max_script_element_size) {
        return error::invalid_push_data_size;
    }
    
    // Convert the script portion to data_chunk
    data_chunk active_bytecode;
    active_bytecode.reserve(script_size);
    
    for (auto it = begin_code_hash; it != script_end; ++it) {
        auto const op_data = it->to_data();
        active_bytecode.insert(active_bytecode.end(), op_data.begin(), op_data.end());
    }
    
    program.push_copy(active_bytecode);
    post_process_introspection_push(program, active_bytecode);
    
    return error::success;
}

inline
interpreter::result interpreter::op_tx_version(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    auto const& context = program.context();
    auto const tx_version = int64_t(context->tx_version());
    auto const bn_exp = number::from_int(tx_version);
    if ( ! bn_exp) {
        return error::overflow;
    }
    
    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);

    return error::success;
}

inline
interpreter::result interpreter::op_tx_input_count(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    auto const& context = program.context();
    auto const input_count = int64_t(context->input_count());
    auto const bn_exp = number::from_int(input_count);
    if ( ! bn_exp) {
        return error::overflow;
    }
    
    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);

    return error::success;
}

inline
interpreter::result interpreter::op_tx_output_count(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    auto const& context = program.context();
    auto const output_count = int64_t(context->output_count());
    auto const bn_exp = number::from_int(output_count);
    if ( ! bn_exp) {
        return error::overflow;
    }
    
    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);

    return error::success;
}

inline
interpreter::result interpreter::op_tx_locktime(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }
    
    auto const& context = program.context();
    auto const locktime = int64_t(context->tx_locktime());
    auto const bn_exp = number::from_int(locktime);
    if ( ! bn_exp) {
        return error::overflow;
    }
    
    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);

    return error::success;
}

inline
interpreter::result interpreter::op_utxo_value(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& prevout_cache = tx.inputs()[index].previous_output().validation.cache;
    auto const bn_exp = number::from_int(int64_t(prevout_cache.value()));
    if ( ! bn_exp) {
        return error::overflow;
    }

    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);
    return error::success;
}

inline
interpreter::result interpreter::op_utxo_bytecode(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& prevout_cache = tx.inputs()[index].previous_output().validation.cache;
    auto const tokens_active = chain::script::is_enabled(program.flags(), script_flags::bch_tokens);

    data_chunk bytecode;
    if ( ! tokens_active && prevout_cache.token_data().has_value()) {
        // Pre-activation: return full serialized blob (prefix + token_data + script)
        auto const full = prevout_cache.to_data(false);
        auto const token_size = chain::token::encoding::serialized_size(prevout_cache.token_data());
        auto const script_size = prevout_cache.script().serialized_size(false);
        auto const wrapped_size = 1 + token_size + script_size;
        auto const offset = full.size() - wrapped_size;
        bytecode.assign(full.begin() + offset, full.end());
    } else {
        bytecode = prevout_cache.script().bytes();
    }

    if (bytecode.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }

    program.push_move(std::move(bytecode));
    post_process_introspection_push(program, program.top());
    return error::success;
}

inline
interpreter::result interpreter::op_outpoint_tx_hash(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& hash = tx.inputs()[index].previous_output().hash();
    auto data = to_chunk(hash);
    program.push_move(std::move(data));
    post_process_introspection_push(program, program.top());
    return error::success;
}

inline
interpreter::result interpreter::op_outpoint_index(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const outpoint_idx = int64_t(tx.inputs()[index].previous_output().index());
    auto const bn_exp = number::from_int(outpoint_idx);
    if ( ! bn_exp) {
        return error::overflow;
    }

    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);
    return error::success;
}

inline
interpreter::result interpreter::op_input_bytecode(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& script_bytes = tx.inputs()[index].script().bytes();
    if (script_bytes.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }

    program.push_copy(script_bytes);
    post_process_introspection_push(program, script_bytes);
    return error::success;
}

inline
interpreter::result interpreter::op_input_sequence_number(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const seq = int64_t(tx.inputs()[index].sequence());
    auto const bn_exp = number::from_int(seq);
    if ( ! bn_exp) {
        return error::overflow;
    }

    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);
    return error::success;
}

inline
interpreter::result interpreter::op_output_value(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.outputs().size()) {
        return error::invalid_tx_output_index;
    }

    auto const value = int64_t(tx.outputs()[index].value());
    auto const bn_exp = number::from_int(value);
    if ( ! bn_exp) {
        return error::overflow;
    }

    auto const& data = bn_exp->data();
    program.push_copy(data);
    post_process_introspection_push(program, data);
    return error::success;
}

inline
interpreter::result interpreter::op_output_bytecode(program& program) {
    auto const validation_result = validate_native_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.outputs().size()) {
        return error::invalid_tx_output_index;
    }

    auto const& out = tx.outputs()[index];
    auto const tokens_active = chain::script::is_enabled(program.flags(), script_flags::bch_tokens);

    // Pre-activation special case (BCHN): if the output has token data but tokens
    // are not yet active, return the full serialized blob (prefix + token_data + script).
    // Post-activation: return just the scriptPubKey.
    data_chunk bytecode;
    if ( ! tokens_active && out.token_data().has_value()) {
        // Rebuild the wrapped script: PREFIX_BYTE + token_data + script
        // Serialize the full output and strip the value (8 bytes) + varint script_size.
        auto const full = out.to_data(false);
        // full = value(8) + varint(script_size) + prefix + token + script
        auto const token_size = chain::token::encoding::serialized_size(out.token_data());
        auto const script_size = out.script().serialized_size(false);
        auto const wrapped_size = 1 + token_size + script_size; // prefix_byte + token + script
        // The wrapped bytes start after value + varint
        auto const offset = full.size() - wrapped_size;
        bytecode.assign(full.begin() + offset, full.end());
    } else {
        bytecode = out.script().bytes();
    }

    if (bytecode.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }

    program.push_move(std::move(bytecode));
    post_process_introspection_push(program, program.top());
    return error::success;
}

// Token introspection helpers
namespace detail {

inline
data_chunk get_token_category(chain::token_data_t const& token) {
    auto const cap = chain::get_nft_capability(token);
    bool const push_cap_byte = chain::has_nft(token)
        && (cap == chain::capability_t::mut
         || cap == chain::capability_t::minting);
    data_chunk vch;
    vch.reserve(token.id.size() + (push_cap_byte ? 1 : 0));
    vch.insert(vch.end(), token.id.begin(), token.id.end());
    if (push_cap_byte) {
        vch.push_back(static_cast<uint8_t>(cap));
    }
    return vch;
}

inline
data_chunk get_token_commitment(chain::token_data_t const& token) {
    return chain::get_nft_commitment(token);
}

inline
bool has_nft(chain::token_data_t const& token) {
    return chain::has_nft(token);
}

inline
int64_t get_token_amount(chain::token_data_t const& token) {
    return int64_t(chain::get_amount(token));
}

} // namespace detail

inline
interpreter::result interpreter::op_utxo_token_category(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& prevout_cache = tx.inputs()[index].previous_output().validation.cache;
    auto const& token_opt = prevout_cache.token_data();
    if ( ! token_opt.has_value()) {
        // No token data: push empty (CScriptNum 0)
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto data = detail::get_token_category(*token_opt);
        if (data.size() > program.max_script_element_size()) {
            return error::invalid_push_data_size;
        }
        program.push_move(std::move(data));
        post_process_introspection_push(program, program.top());
    }
    return error::success;
}

inline
interpreter::result interpreter::op_utxo_token_commitment(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& prevout_cache = tx.inputs()[index].previous_output().validation.cache;
    auto const& token_opt = prevout_cache.token_data();
    if ( ! token_opt.has_value() || ! detail::has_nft(*token_opt)) {
        // No token data or not an NFT: push empty
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto data = detail::get_token_commitment(*token_opt);
        if (data.size() > program.max_script_element_size()) {
            return error::invalid_push_data_size;
        }
        program.push_move(std::move(data));
        post_process_introspection_push(program, program.top());
    }
    return error::success;
}

inline
interpreter::result interpreter::op_utxo_token_amount(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.inputs().size()) {
        return error::invalid_tx_input_index;
    }

    auto const& prevout_cache = tx.inputs()[index].previous_output().validation.cache;
    auto const& token_opt = prevout_cache.token_data();
    if ( ! token_opt.has_value()) {
        // No token data: push empty (VM number 0)
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto const amount = detail::get_token_amount(*token_opt);
        auto const bn_exp = number::from_int(amount);
        if ( ! bn_exp) {
            return error::overflow;
        }
        auto const& data = bn_exp->data();
        program.push_copy(data);
        post_process_introspection_push(program, data);
    }
    return error::success;
}

inline
interpreter::result interpreter::op_output_token_category(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.outputs().size()) {
        return error::invalid_tx_output_index;
    }

    auto const& token_opt = tx.outputs()[index].token_data();
    if ( ! token_opt.has_value()) {
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto data = detail::get_token_category(*token_opt);
        if (data.size() > program.max_script_element_size()) {
            return error::invalid_push_data_size;
        }
        program.push_move(std::move(data));
        post_process_introspection_push(program, program.top());
    }
    return error::success;
}

inline
interpreter::result interpreter::op_output_token_commitment(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.outputs().size()) {
        return error::invalid_tx_output_index;
    }

    auto const& token_opt = tx.outputs()[index].token_data();
    if ( ! token_opt.has_value() || ! detail::has_nft(*token_opt)) {
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto data = detail::get_token_commitment(*token_opt);
        if (data.size() > program.max_script_element_size()) {
            return error::invalid_push_data_size;
        }
        program.push_move(std::move(data));
        post_process_introspection_push(program, program.top());
    }
    return error::success;
}

inline
interpreter::result interpreter::op_output_token_amount(program& program) {
    auto const validation_result = validate_token_introspection(program);
    if (validation_result != error::success) {
        return validation_result;
    }

    if (program.size() < 1) {
        return error::insufficient_main_stack;
    }

    auto const index_exp = program.pop_number(program.max_integer_size_legacy());
    if ( ! index_exp) {
        return index_exp.error();
    }
    auto const index = index_exp->int64();

    auto const& context = program.context();
    auto const& tx = context->transaction();
    if (index < 0 || uint64_t(index) >= tx.outputs().size()) {
        return error::invalid_tx_output_index;
    }

    auto const& token_opt = tx.outputs()[index].token_data();
    if ( ! token_opt.has_value()) {
        data_chunk empty;
        program.push_move(std::move(empty));
        post_process_introspection_push(program, program.top());
    } else {
        auto const amount = detail::get_token_amount(*token_opt);
        auto const bn_exp = number::from_int(amount);
        if ( ! bn_exp) {
            return error::overflow;
        }
        auto const& data = bn_exp->data();
        program.push_copy(data);
        post_process_introspection_push(program, data);
    }
    return error::success;
}


// It is expected that the compiler will produce a very efficient jump table.
inline
interpreter::result interpreter::run_op(operation const& op, program& program) {
    auto const code = op.code();
    KTH_ASSERT(op.data().empty() || op.is_push());

    program.get_metrics().add_op_cost(kth::may2025::opcode_cost);

    switch (op.code()) {
    // push value
        case opcode::push_size_0:
        case opcode::push_size_1:
        case opcode::push_size_2:
        case opcode::push_size_3:
        case opcode::push_size_4:
        case opcode::push_size_5:
        case opcode::push_size_6:
        case opcode::push_size_7:
        case opcode::push_size_8:
        case opcode::push_size_9:
        case opcode::push_size_10:
        case opcode::push_size_11:
        case opcode::push_size_12:
        case opcode::push_size_13:
        case opcode::push_size_14:
        case opcode::push_size_15:
        case opcode::push_size_16:
        case opcode::push_size_17:
        case opcode::push_size_18:
        case opcode::push_size_19:
        case opcode::push_size_20:
        case opcode::push_size_21:
        case opcode::push_size_22:
        case opcode::push_size_23:
        case opcode::push_size_24:
        case opcode::push_size_25:
        case opcode::push_size_26:
        case opcode::push_size_27:
        case opcode::push_size_28:
        case opcode::push_size_29:
        case opcode::push_size_30:
        case opcode::push_size_31:
        case opcode::push_size_32:
        case opcode::push_size_33:
        case opcode::push_size_34:
        case opcode::push_size_35:
        case opcode::push_size_36:
        case opcode::push_size_37:
        case opcode::push_size_38:
        case opcode::push_size_39:
        case opcode::push_size_40:
        case opcode::push_size_41:
        case opcode::push_size_42:
        case opcode::push_size_43:
        case opcode::push_size_44:
        case opcode::push_size_45:
        case opcode::push_size_46:
        case opcode::push_size_47:
        case opcode::push_size_48:
        case opcode::push_size_49:
        case opcode::push_size_50:
        case opcode::push_size_51:
        case opcode::push_size_52:
        case opcode::push_size_53:
        case opcode::push_size_54:
        case opcode::push_size_55:
        case opcode::push_size_56:
        case opcode::push_size_57:
        case opcode::push_size_58:
        case opcode::push_size_59:
        case opcode::push_size_60:
        case opcode::push_size_61:
        case opcode::push_size_62:
        case opcode::push_size_63:
        case opcode::push_size_64:
        case opcode::push_size_65:
        case opcode::push_size_66:
        case opcode::push_size_67:
        case opcode::push_size_68:
        case opcode::push_size_69:
        case opcode::push_size_70:
        case opcode::push_size_71:
        case opcode::push_size_72:
        case opcode::push_size_73:
        case opcode::push_size_74:
        case opcode::push_size_75:
            return op_push_size(program, op);

        case opcode::push_one_size:
            return op_push_data(program, op.data(), max_uint8);
        case opcode::push_two_size:
            return op_push_data(program, op.data(), max_uint16);
        case opcode::push_four_size:
            return op_push_data(program, op.data(), max_uint32);

        case opcode::reserved_80:
            return op_reserved(code);

        case opcode::push_negative_1:
            return op_push_number(program, number::negative_1);
        case opcode::push_positive_1:
            return op_push_number(program, number::positive_1);
        case opcode::push_positive_2:
            return op_push_number(program, number::positive_2);
        case opcode::push_positive_3:
            return op_push_number(program, number::positive_3);
        case opcode::push_positive_4:
            return op_push_number(program, number::positive_4);
        case opcode::push_positive_5:
            return op_push_number(program, number::positive_5);
        case opcode::push_positive_6:
            return op_push_number(program, number::positive_6);
        case opcode::push_positive_7:
            return op_push_number(program, number::positive_7);
        case opcode::push_positive_8:
            return op_push_number(program, number::positive_8);
        case opcode::push_positive_9:
            return op_push_number(program, number::positive_9);
        case opcode::push_positive_10:
            return op_push_number(program, number::positive_10);
        case opcode::push_positive_11:
            return op_push_number(program, number::positive_11);
        case opcode::push_positive_12:
            return op_push_number(program, number::positive_12);
        case opcode::push_positive_13:
            return op_push_number(program, number::positive_13);
        case opcode::push_positive_14:
            return op_push_number(program, number::positive_14);
        case opcode::push_positive_15:
            return op_push_number(program, number::positive_15);
        case opcode::push_positive_16:
            return op_push_number(program, number::positive_16);

    // control
        case opcode::nop:
            return op_nop(code);
        case opcode::reserved_98:
            return op_reserved(code);
        case opcode::if_:
            return op_if(program);
        case opcode::notif:
            return op_notif(program);
        case opcode::op_begin:
        case opcode::op_until:
            // Handled directly in the interpreter loop (not via run_op).
            return {};
        case opcode::else_:
            return op_else(program);
        case opcode::endif:
            return op_endif(program);
        case opcode::verify:
            return op_verify(program);
        case opcode::return_:
            return op_return(program);

    // stack ops
        case opcode::toaltstack:
            return op_to_alt_stack(program);
        case opcode::fromaltstack:
            return op_from_alt_stack(program);
        case opcode::drop2:
            return op_drop2(program);
        case opcode::dup2:
            return op_dup2(program);
        case opcode::dup3:
            return op_dup3(program);
        case opcode::over2:
            return op_over2(program);
        case opcode::rot2:
            return op_rot2(program);
        case opcode::swap2:
            return op_swap2(program);
        case opcode::ifdup:
            return op_if_dup(program);
        case opcode::depth:
            return op_depth(program);
        case opcode::drop:
            return op_drop(program);
        case opcode::dup:
            return op_dup(program);
        case opcode::nip:
            return op_nip(program);
        case opcode::over:
            return op_over(program);
        case opcode::pick:
            return op_pick(program);
        case opcode::roll:
            return op_roll(program);
        case opcode::rot:
            return op_rot(program);
        case opcode::swap:
            return op_swap(program);
        case opcode::tuck:
            return op_tuck(program);

    // splice ops
        case opcode::cat:
            return op_cat(program);
        case opcode::split:                 // after pythagoras/monolith upgrade (May 2018)
            return op_split(program);
        case opcode::reverse_bytes:
            return op_reverse_bytes(program);
        case opcode::num2bin:               // after pythagoras/monolith upgrade (May 2018)
            return op_num2bin(program);
        case opcode::bin2num:               // after pythagoras/monolith upgrade (May 2018)
            return op_bin2num(program);
        case opcode::size:
            return op_size(program);

    // Native Introspection opcodes (Nullary)
        case opcode::input_index:
            return op_input_index(program);
        case opcode::active_bytecode:
            return op_active_bytecode(program);
        case opcode::tx_version:
            return op_tx_version(program);
        case opcode::tx_input_count:
            return op_tx_input_count(program);
        case opcode::tx_output_count:
            return op_tx_output_count(program);
        case opcode::tx_locktime:
            return op_tx_locktime(program);

    // Native Introspection opcodes (Unary)
        // case OP_UTXOTOKENCATEGORY:
        // case OP_UTXOTOKENCOMMITMENT:
        // case OP_UTXOTOKENAMOUNT:
        // case OP_OUTPUTTOKENCATEGORY:
        // case OP_OUTPUTTOKENCOMMITMENT:
        // case OP_OUTPUTTOKENAMOUNT:
        //     // These require native tokens (upgrade9)
        //     if ( ! nativeTokens) {
        //         return set_error(serror, ScriptError::BAD_OPCODE);
        //     }
        //     [[fallthrough]];
        // case OP_UTXOVALUE:
        // case OP_UTXOBYTECODE:
        // case OP_OUTPOINTTXHASH:
        // case OP_OUTPOINTINDEX:
        // case OP_INPUTBYTECODE:
        // case OP_INPUTSEQUENCENUMBER:
        // case OP_OUTPUTVALUE:
        // case OP_OUTPUTBYTECODE: {

        case opcode::utxo_token_category:
            return op_utxo_token_category(program);
        case opcode::utxo_token_commitment:
            return op_utxo_token_commitment(program);
        case opcode::utxo_token_amount:
            return op_utxo_token_amount(program);
        case opcode::output_token_category:
            return op_output_token_category(program);
        case opcode::output_token_commitment:
            return op_output_token_commitment(program);
        case opcode::output_token_amount:
            return op_output_token_amount(program);
        case opcode::utxo_value:
            return op_utxo_value(program);
        case opcode::utxo_bytecode:
            return op_utxo_bytecode(program);
        case opcode::outpoint_tx_hash:
            return op_outpoint_tx_hash(program);
        case opcode::outpoint_index:
            return op_outpoint_index(program);
        case opcode::input_bytecode:
            return op_input_bytecode(program);
        case opcode::input_sequence_number:
            return op_input_sequence_number(program);
        case opcode::output_value:
            return op_output_value(program);
        case opcode::output_bytecode:
            return op_output_bytecode(program);

    // bit logic
        case opcode::op_invert:
            return op_invert(program);
        case opcode::and_:
            return op_and(program);
        case opcode::or_:
            return op_or(program);
        case opcode::xor_:
            return op_xor(program);
        case opcode::equal:
            return op_equal(program);
        case opcode::equalverify:
            return op_equal_verify(program);
        case opcode::op_define:
        case opcode::op_invoke:
            // Handled directly in the interpreter loop (not via run_op).
            return {};

    // numeric
        case opcode::add1:
            return op_add1(program);
        case opcode::sub1:
            return op_sub1(program);
        case opcode::op_lshiftnum:
        case opcode::op_rshiftnum:
            return op_shiftnum(program, code);
        case opcode::negate:
            return op_negate(program);
        case opcode::abs:
            return op_abs(program);
        case opcode::not_:
            return op_not(program);
        case opcode::nonzero:
            return op_nonzero(program);
        case opcode::add:
            return op_add(program);
        case opcode::sub:
            return op_sub(program);
        case opcode::mul:
            return op_mul(program);
        case opcode::div:
            return op_div(program);
        case opcode::mod:
            return op_mod(program);
        case opcode::op_lshiftbin:
        case opcode::op_rshiftbin:
            return op_shiftbin(program, code);
        case opcode::booland:
            return op_bool_and(program);
        case opcode::boolor:
            return op_bool_or(program);
        case opcode::numequal:
            return op_num_equal(program);
        case opcode::numequalverify:
            return op_num_equal_verify(program);
        case opcode::numnotequal:
            return op_num_not_equal(program);
        case opcode::lessthan:
            return op_less_than(program);
        case opcode::greaterthan:
            return op_greater_than(program);
        case opcode::lessthanorequal:
            return op_less_than_or_equal(program);
        case opcode::greaterthanorequal:
            return op_greater_than_or_equal(program);
        case opcode::min:
            return op_min(program);
        case opcode::max:
            return op_max(program);

        case opcode::within:
            return op_within(program);

    // crypto
        case opcode::ripemd160:
            return op_ripemd160(program);
        case opcode::sha1:
            return op_sha1(program);
        case opcode::sha256:
            return op_sha256(program);
        case opcode::hash160:
            return op_hash160(program);
        case opcode::hash256:
            return op_hash256(program);
        case opcode::codeseparator:
            return op_codeseparator(program, op);
        case opcode::checksig:
            return op_check_sig(program);
        case opcode::checksigverify:
            return op_check_sig_verify(program);

        case opcode::checkdatasig:
            return op_check_data_sig(program);
        case opcode::checkdatasigverify:
            return op_check_data_sig_verify(program);

        case opcode::checkmultisig:
            return op_check_multisig(program);
        case opcode::checkmultisigverify:
            return op_check_multisig_verify(program);

    // expansion
        case opcode::nop1:
        case opcode::nop4:
        case opcode::nop5:
        case opcode::nop6:
        case opcode::nop7:
        case opcode::nop8:
        case opcode::nop9:
        case opcode::nop10:
            if (chain::script::is_enabled(program.flags(), script_flags::bch_discourage_upgradable_nops)) {
                return {error::operation_failed, code};
            }
            return op_nop(code);
        case opcode::checklocktimeverify:
            return op_check_locktime_verify(program);
        case opcode::checksequenceverify:
            return op_check_sequence_verify(program);




        case opcode::reserved_212:
        case opcode::reserved_213:
        case opcode::reserved_214:
        case opcode::reserved_215:
        case opcode::reserved_216:
        case opcode::reserved_217:
        case opcode::reserved_218:
        case opcode::reserved_219:
        case opcode::reserved_220:
        case opcode::reserved_221:
        case opcode::reserved_222:
        case opcode::reserved_223:
        case opcode::reserved_224:
        case opcode::reserved_225:
        case opcode::reserved_226:
        case opcode::reserved_227:
        case opcode::reserved_228:
        case opcode::reserved_229:
        case opcode::reserved_230:
        case opcode::reserved_231:
        case opcode::reserved_232:
        case opcode::reserved_233:
        case opcode::reserved_234:
        case opcode::reserved_235:
        case opcode::reserved_236:
        case opcode::reserved_237:
        case opcode::reserved_238:
        case opcode::reserved_239:
        case opcode::reserved_240:
        case opcode::reserved_241:
        case opcode::reserved_242:
        case opcode::reserved_243:
        case opcode::reserved_244:
        case opcode::reserved_245:
        case opcode::reserved_246:
        case opcode::reserved_247:
        case opcode::reserved_248:
        case opcode::reserved_249:
        case opcode::reserved_250:
        case opcode::reserved_251:
        case opcode::reserved_252:
        case opcode::reserved_253:
        case opcode::reserved_254:
        case opcode::reserved_255:
        default:
            return op_reserved(code);
    }
}

} // namespace kth::domain::machine

#endif // KTH_DOMAIN_MACHINE_INTERPRETER_IPP_
