// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_INTERPRETER_IPP_
#define KTH_DOMAIN_MACHINE_INTERPRETER_IPP_

#include <cstdint>
#include <utility>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/define.hpp>
#include <kth/domain/machine/opcode.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/domain/machine/program.hpp>
#include <kth/domain/machine/rule_fork.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>


// TODO: move to a concepts file
template <typename Op>
concept bitwise_op = requires(Op op, uint8_t a, uint8_t b) {
    { op(a, b) } -> std::same_as<uint8_t>;
};


namespace kth::domain::machine {

static constexpr
auto op_75 = uint8_t(opcode::push_size_75);

// Operations (shared).
//-----------------------------------------------------------------------------

inline
interpreter::result interpreter::op_nop(opcode /*unused*/) {
    return error::success;
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
    return error::success;
}

inline
interpreter::result interpreter::op_push_size(program& program, operation const& op) {
    if (op.data().size() > op_75) {
        return error::op_push_size;
    }

    program.push_copy(op.data());
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

// TODO: std::move the data chunk to the program
inline
interpreter::result interpreter::op_push_data(program& program, data_chunk const& data, uint32_t size_limit) {
    if (data.size() > size_limit) {
        return error::op_push_data;
    }

    program.push_copy(data);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

// Operations (not shared).
//-----------------------------------------------------------------------------
// All index parameters are zero-based and relative to stack top.

inline
interpreter::result interpreter::op_if(program& program) {
    //TODO: SCRIPT_VERIFY_MINIMALIF
    auto value = false;

    if (program.succeeded()) {
        if (program.empty()) {
            return error::op_if;
        }

        value = program.stack_true(false);
        program.pop();
    }

    program.open(value);
    return error::success;
}

inline
interpreter::result interpreter::op_notif(program& program) {
    //TODO: SCRIPT_VERIFY_MINIMALNOTIF
    auto value = false;

    if (program.succeeded()) {
        if (program.empty()) {
            return error::op_notif;
        }

        value = !program.stack_true(false);
        program.pop();
    }

    program.open(value);
    return error::success;
}

inline
interpreter::result interpreter::op_else(program& program) {
    if (program.closed()) {
        return error::op_else;
    }

    program.negate();
    return error::success;
}

inline
interpreter::result interpreter::op_endif(program& program) {
    if (program.closed()) {
        return error::op_endif;
    }

    program.close();
    return error::success;
}

inline
interpreter::result interpreter::op_verify(program& program) {
    if (program.empty()) {
        return error::op_verify_empty_stack;
    }

    if ( ! program.stack_true(false)) {
        return error::op_verify_failed;
    }

    program.pop();
    return error::success;
}

inline
interpreter::result interpreter::op_return(program& /*unused*/) {
    return error::op_return;
}

inline
interpreter::result interpreter::op_to_alt_stack(program& program) {
    if (program.empty()) {
        return error::op_to_alt_stack;
    }

    program.push_alternate(program.pop());
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return error::success;
}

inline
interpreter::result interpreter::op_from_alt_stack(program& program) {
    if (program.empty_alternate()) {
        return error::op_from_alt_stack;
    }

    program.push_move(program.pop_alternate());
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_drop2(program& program) {
    if (program.size() < 2) {
        return error::op_drop2;
    }

    program.pop();
    program.pop();
    return error::success;
}

inline
interpreter::result interpreter::op_dup2(program& program) {
    if (program.size() < 2) {
        return error::op_dup2;
    }

    auto item1 = program.item(1);
    auto item0 = program.item(0);

    program.push_move(std::move(item1));
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());

    program.push_move(std::move(item0));
    // metrics.TallyPushOp(stack.back().size());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_dup3(program& program) {
    if (program.size() < 3) {
        return error::op_dup3;
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
    return error::success;
}

inline
interpreter::result interpreter::op_over2(program& program) {
    if (program.size() < 4) {
        return error::op_over2;
    }

    auto item3 = program.item(3);
    auto item2 = program.item(2);

    program.push_move(std::move(item3));
    program.get_metrics().add_op_cost(program.top().size());
    program.push_move(std::move(item2));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_rot2(program& program) {
    if (program.size() < 6) {
        return error::op_rot2;
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
    return error::success;
}

inline
interpreter::result interpreter::op_swap2(program& program) {
    if (program.size() < 4) {
        return error::op_swap2;
    }

    program.swap(3, 1);
    program.swap(2, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return error::success;
}

inline
interpreter::result interpreter::op_if_dup(program& program) {
    if (program.empty()) {
        return error::op_if_dup;
    }

    if (program.stack_true(false)) {
        program.duplicate(0);
        program.get_metrics().add_op_cost(program.top().size());
    }

    return error::success;
}

inline
interpreter::result interpreter::op_depth(program& program) {
    auto num_exp = number::from_int(program.size());
    if ( ! num_exp) {
        return num_exp.error();
    }
    program.push_move(num_exp->data());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_drop(program& program) {
    if (program.empty()) {
        return error::op_drop;
    }

    program.pop();
    // No metrics
    return error::success;
}

inline
interpreter::result interpreter::op_dup(program& program) {
    if (program.empty()) {
        return error::op_dup;
    }

    program.duplicate(0);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_nip(program& program) {
    if (program.size() < 2) {
        return error::op_nip;
    }

    program.erase(program.position(1));
    // No metrics
    return error::success;
}

inline
interpreter::result interpreter::op_over(program& program) {
    if (program.size() < 2) {
        return error::op_over;
    }

    program.duplicate(1);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_pick(program& program) {
    // (xn ... x2 x1 x0 n - xn ... x2 x1 x0 xn)
    program::stack_iterator position;
    if ( ! program.pop_position(position)) {
        return error::op_pick;
    }

    program.push_copy(*position);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_roll(program& program) {
    // (xn ... x2 x1 x0 n - ... x2 x1 x0 xn)
    program::stack_iterator position;
    if ( ! program.pop_position(position)) {
        return error::op_roll;
    }

    auto copy = *position;
    program.erase(position);
    // metrics.TallyOp(n); // erasing in the middle is linear with `n`
    program.get_metrics().add_op_cost(program.index(position));
    program.push_move(std::move(copy));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_rot(program& program) {
    // (x1 x2 x3 -- x2 x3 x1)
    //  x2 x1 x3  after first swap
    //  x2 x3 x1  after second swap
    if (program.size() < 3) {
        return error::op_rot;
    }

    program.swap(2, 1);
    program.swap(1, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return error::success;
}

inline
interpreter::result interpreter::op_swap(program& program) {
    if (program.size() < 2) {
        return error::op_swap;
    }

    program.swap(1, 0);
    // Intentional: no tallying is done to get_metrics().add_op_cost()
    return error::success;
}

inline
interpreter::result interpreter::op_tuck(program& program) {
    // (x1 x2 -- x2 x1 x2)
    if (program.size() < 2) {
        return error::op_tuck;
    }

    auto first = program.pop();
    auto second = program.pop();
    program.get_metrics().add_op_cost(first.size());
    program.push_copy(first);
    program.push_move(std::move(second));
    program.push_move(std::move(first));
    return error::success;
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
        return error::op_cat;
    }

    auto last = program.pop();
    auto& but_last = program.top();

    if (but_last.size() + last.size() > program.max_script_element_size()) {
        return error::invalid_push_data_size;
    }

    but_last.insert(but_last.end(), last.begin(), last.end());
    program.get_metrics().add_op_cost(but_last.size());

    return error::success;
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
        return error::op_split;
    }

    auto& pos = program.item(0);  // last item
    auto& data = program.item(1); // but last item

    number position;
    if ( ! position.set_data(pos, program.max_integer_size_legacy())) {
        return error::op_split;
    }
    auto const pos64 = position.int64();

    if (pos64 < 0 || size_t(pos64) > data.size()) {
        return error::op_split;
    }

    auto n1 = data_chunk(data.begin(), data.begin() + pos64);
    auto n2 = data_chunk(data.begin() + pos64, data.end());
    size_t const total_size = n1.size() + n2.size();

    data = std::move(n1);
    pos = std::move(n2);

    program.get_metrics().add_op_cost(total_size);

    return error::success;
}

inline
interpreter::result interpreter::op_reverse_bytes(program& program) {
    if (program.empty()) {
        return error::op_reverse_bytes;
    }
    
    // Operate in-place on the top stack element (like BCHN)
    auto& data = program.top();
    std::reverse(data.begin(), data.end());
    program.get_metrics().add_op_cost(data.size());
    
    return error::success;
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
        return error::op_num2bin;
    }

    number size;
    if ( ! program.top(size, program.max_integer_size_legacy())) {
        return error::op_num2bin_invalid_size;
    }
    auto const size64 = size.int64();    
    if (size64 < 0 || size_t(size64) > program.max_script_element_size()) {
        return error::op_num2bin_size_exceeded;
    }

    // Pop the size parameter from stack
    program.pop();

    auto& rawnum = program.top(); // now top item after pop
    
    number::minimally_encode(rawnum);

    // Check if the number can be adjusted to the desired size.
    if (rawnum.size() > size64) {
        return error::op_num2bin_impossible_encoding;
    }

    // If the size is already correct, no more is needed.
    if (rawnum.size() == size64) {
        program.get_metrics().add_op_cost(rawnum.size());
        return error::success;
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
    return error::success;
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
        return error::op_bin2num;
    }

    auto& n = program.top();
    number::minimally_encode(n);
    program.get_metrics().add_op_cost(n.size());

    if ( ! number::is_minimally_encoded(n, program.max_integer_size_legacy())) {
        return error::op_bin2num_invalid_number_range;
    }

    return error::success;
}

inline
interpreter::result interpreter::op_size(program& program) {
    // (in -- in size)
    if (program.empty()) {
        return error::op_size;
    }

    auto top = program.pop();
    auto const size = top.size();
    program.push_move(std::move(top));
    auto num_exp = number::from_int(size);
    if ( ! num_exp) {
        return num_exp.error();
    }
    program.push_move(num_exp->data());
    program.get_metrics().add_op_cost(size);
    return error::success;
}

// Disabled
// inline
// interpreter::result interpreter::op_invert(program& program) {
// }


template <bitwise_op Op>
inline
interpreter::result bitwise_operation_generic(program& program, error::error_code_t error, Op op) {
    if (program.size() < 2) {
        return error;
    }

    auto& vch1 = program.item(1);
    auto& vch2 = program.item(0);

    if (vch1.size() != vch2.size()) {
        return error;
    }

    for (size_t i = 0; i < vch1.size(); ++i) {
        vch1[i] = op(vch1[i], vch2[i]);
    }

    program.pop();
    program.get_metrics().add_op_cost(vch1.size());

    return error::success;
}

inline
interpreter::result interpreter::op_and(program& program) {
    return bitwise_operation_generic(program,
        error::op_and,
        [](uint8_t a, uint8_t b) { return uint8_t(a & b); });
}

inline
interpreter::result interpreter::op_or(program& program) {
    return bitwise_operation_generic(program,
        error::op_or,
        [](uint8_t a, uint8_t b) { return uint8_t(a | b); });
}

inline
interpreter::result interpreter::op_xor(program& program) {
    return bitwise_operation_generic(program,
        error::op_xor,
        [](uint8_t a, uint8_t b) { return uint8_t(a ^ b); });
}

inline
interpreter::result interpreter::op_equal(program& program) {
    // (x1 x2 - bool)
    if (program.size() < 2) {
        return error::op_equal;
    }

    auto val1 = program.pop();
    auto val2 = program.pop();
    bool equal_result = (val1 == val2);
        
    program.push(equal_result);
        
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_equal_verify(program& program) {
    // (x1 x2 - bool)
    if (program.size() < 2) {
        return error::op_equal_verify_insufficient_stack;
    }
    auto const res = program.pop() == program.pop(); //res is a bool
    // program.get_metrics().add_op_cost(res.size());

    // void program::push(bool value) {
    //     push_move(value ? value_type{number::positive_1} : value_type{});
    // }
    program.get_metrics().add_op_cost(res ? 1 : 0);

    return res ? error::success : error::op_equal_verify_failed;
}

inline
interpreter::result interpreter::op_add1(program& program) {
    constexpr auto push_cost_factor = 2;
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_add1;
    }

    // number += 1;
    bool const res = number.safe_add(1);
    if ( ! res) {
        return error::op_add_overflow;
    }
    // if ( ! number.valid(program.max_integer_size_legacy())) {
    //     return error::op_add_overflow;
    // }

    program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
    program.push_move(number.data());
    return error::success;
}

inline
interpreter::result interpreter::op_sub1(program& program) {
    constexpr auto push_cost_factor = 2;
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_sub1;
    }

    // number -= 1;
    bool const res = number.safe_sub(1);
    if ( ! res) {
        return error::op_sub_underflow;
    }
    // TODO(2025-Jul)
    // if ( ! number.valid(program.max_integer_size_legacy())) {
    //     return error::op_add_overflow;
    // }

    program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
    program.push_move(number.data());
    return error::success;
}

inline
interpreter::result interpreter::op_negate(program& program) {
    constexpr auto push_cost_factor = 2;
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_negate;
    }

    number = -number;
    program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
    program.push_move(number.data());
    return error::success;
}

inline
interpreter::result interpreter::op_abs(program& program) {
    constexpr auto push_cost_factor = 2;
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_abs;
    }

    if (number < 0) {
        number = -number;
    }

    program.get_metrics().add_op_cost(number.data().size() * push_cost_factor);
    program.push_move(number.data());
    return error::success;
}

inline
interpreter::result interpreter::op_not(program& program) {
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_not;
    }

    program.push(number.is_false());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_nonzero(program& program) {
    number number;
    if ( ! program.pop(number, program.max_integer_size_legacy())) {
        return error::op_nonzero;
    }

    program.push(number.is_true());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_add(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_add;
    }

    // auto const result = first + second;
    auto result = number::safe_add(first, second);
    if ( ! result) {
        return error::op_add_overflow;
    }
    // TODO(2025-Jul)
    // if ( ! result->valid(program.max_integer_size_legacy())) {
    //     return error::op_add_overflow;
    // }

    program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
    program.push_move(result->data());
    return error::success;
}

inline
interpreter::result interpreter::op_sub(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_sub;
    }

    // auto const result = second - first;
    auto result = number::safe_sub(second, first);
    if ( ! result) {
        return error::op_sub_underflow;
    }
    // TODO(2025-Jul)
    // if ( ! result->valid(program.max_integer_size_legacy())) {
    //     return error::op_add_overflow;
    // }

    program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
    program.push_move(result->data());
    return error::success;
}

inline
interpreter::result interpreter::op_mul(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_mul;
    }

    // auto const result = first * second;
    auto result = number::safe_mul(first, second);
    if ( ! result) {
        return error::op_mul_overflow;
    }
    // TODO(2025-Jul)
    // if ( ! result->valid(program.max_integer_size_legacy())) {
    //     return error::op_add_overflow;
    // }

    uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
    program.get_metrics().add_op_cost(quadratic_op_cost);
    program.get_metrics().add_op_cost(result->data().size() * push_cost_factor);
    program.push_move(result->data());
    return error::success;
}

inline
interpreter::result interpreter::op_div(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_div;
    }

    if (first == 0) {
        return error::op_div_by_zero;
    }

    auto result = second / first;
    uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
    program.get_metrics().add_op_cost(quadratic_op_cost);
    program.push_move(result.data());
    program.get_metrics().add_op_cost(result.data().size() * push_cost_factor);
    return error::success;
}

inline
interpreter::result interpreter::op_mod(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_mod;
    }

    if (first == 0) {
        return error::op_mod_by_zero;
    }

    auto result = second % first;
    uint32_t const quadratic_op_cost = first.data().size() * second.data().size();
    program.get_metrics().add_op_cost(quadratic_op_cost);
    program.push_move(result.data());
    program.get_metrics().add_op_cost(result.data().size() * push_cost_factor);
    return error::success;
}

inline
interpreter::result interpreter::op_bool_and(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_bool_and;
    }

    program.push(first.is_true() && second.is_true());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_bool_or(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_bool_or;
    }

    program.push(first.is_true() || second.is_true());
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_num_equal(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_num_equal;
    }

    program.push(first == second);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_num_equal_verify(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_num_equal_verify_insufficient_stack;
    }
    auto const res = first == second;
    program.get_metrics().add_op_cost(1);
    return res ? error::success : error::op_num_equal_verify_failed;
}

inline
interpreter::result interpreter::op_num_not_equal(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_num_not_equal;
    }

    program.push(first != second);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_less_than(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_less_than;
    }

    program.push(second < first);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_greater_than(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_greater_than;
    }

    program.push(second > first);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_less_than_or_equal(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_less_than_or_equal;
    }

    program.push(second <= first);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_greater_than_or_equal(program& program) {
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_greater_than_or_equal;
    }

    program.push(second >= first);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_min(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_min;
    }

    program.push_move(second < first ? second.data() : first.data());
    program.get_metrics().add_op_cost(program.top().size() * push_cost_factor);
    return error::success;
}

inline
interpreter::result interpreter::op_max(program& program) {
    constexpr auto push_cost_factor = 2;
    number first;
    number second;
    if ( ! program.pop_binary(first, second)) {
        return error::op_max;
    }

    program.push_move(second > first ? second.data() : first.data());
    program.get_metrics().add_op_cost(program.top().size() * push_cost_factor);
    return error::success;
}

inline
interpreter::result interpreter::op_within(program& program) {
    // (x min max -- out)
    number first;
    number second;
    number third;
    if ( ! program.pop_ternary(first, second, third)) {
        return error::op_within;
    }

    program.push(second <= third && third < first);
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_ripemd160(program& program) {
    // (in -- hash)
    if (program.empty()) {
        return error::op_ripemd160;
    }
    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(ripemd160_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_sha1(program& program) {
    // (in -- hash)
    if (program.empty()) {
        return error::op_sha1;
    }
    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(sha1_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_sha256(program& program) {
    if (program.empty()) {
        return error::op_sha256;
    }

    program.get_metrics().add_hash_iterations(program.top().size(), false);
    program.push_move(sha256_hash_chunk(program.pop()));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_hash160(program& program) {
    if (program.empty()) {
        return error::op_hash160;
    }

    program.get_metrics().add_hash_iterations(program.top().size(), true);
    program.push_move(ripemd160_hash_chunk(sha256_hash(program.pop())));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_hash256(program& program) {
    if (program.empty()) {
        return error::op_hash256;
    }

    program.get_metrics().add_hash_iterations(program.top().size(), true);
    program.push_move(sha256_hash_chunk(sha256_hash(program.pop())));
    program.get_metrics().add_op_cost(program.top().size());
    return error::success;
}

inline
interpreter::result interpreter::op_codeseparator(program& program, operation const& op) {
    return program.set_jump_register(op, +1) ? error::success : error::op_code_seperator;
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
    // Check if STRICTENC (BIP66) is enabled
    auto const bip66_enabled = chain::script::is_enabled(program.forks(), rule_fork::bip66_rule);
    auto const is_valid_pubkey = is_compressed_or_uncompressed_pubkey(public_key);
    
    if (bip66_enabled && !is_valid_pubkey) {
        return error::pubkey_type;  // PUBKEYTYPE equivalent
    }
    return error::success;
}

inline
std::pair<interpreter::result, size_t> op_check_sig_common(program& program, interpreter::result err) {
    //TODO: SCRIPT_VERIFY_NULLFAIL
    if (program.size() < 2) {
        return {err, 0};
    }

    uint8_t sighash;
    ec_signature signature;
    der_signature distinguished;
    auto const bip66 = chain::script::is_enabled(program.forks(), rule_fork::bip66_rule);
    auto const bip143 = false;

    auto const public_key = program.pop();
    auto endorsement = program.pop();

    // Validate public key encoding according to STRICTENC rules
    auto const pubkey_result = check_pubkey_encoding(public_key, program);
    if (pubkey_result != error::success) {
        return {pubkey_result, 0};
    }

    // Create a subscript with endorsements stripped (sort of).
    chain::script const script_code(program.subscript());

    // BIP62: An empty endorsement is not considered lax encoding.
    if ( ! parse_endorsement(sighash, distinguished, std::move(endorsement))) {
        return {error::invalid_signature_encoding, 0};
    }

    // Parse DER signature into an EC signature.
    if ( ! parse_signature(signature, distinguished, bip66)) {
        return {bip66 ? error::invalid_signature_lax_encoding : error::invalid_signature_encoding, 0};
    }

#if ! defined(KTH_CURRENCY_BCH)
    // Version condition preserves independence of bip141 and bip143.
    auto const version = bip143 ? program.version() : script_version::unversioned;
#endif // ! KTH_CURRENCY_BCH

    auto const [res, size] = chain::script::check_signature(
        signature,
        sighash,
        public_key,
        script_code,
        program.transaction(),
        program.input_index(),
        program.forks(),
#if ! defined(KTH_CURRENCY_BCH)
        version,
#endif // ! KTH_CURRENCY_BCH
        program.value()
    );

    return {res ? error::success : error::incorrect_signature, size};
}

inline
interpreter::result interpreter::op_check_sig(program& program) {
    auto const [res, size] = op_check_sig_common(program, error::op_check_sig);

    // BIP62: only lax encoding fails the operation.
    if (res == error::invalid_signature_lax_encoding) {
        return error::op_check_sig;
    }
    if (res == error::pubkey_type) {
        return res;
    }
    program.push(res == error::success);
    if (res == error::success) {
        program.get_metrics().add_op_cost(program.top().size());
        program.get_metrics().add_sig_checks(1);
        program.get_metrics().add_hash_iterations(size, true);
    }
    return error::success;
}

inline
interpreter::result interpreter::op_check_sig_verify(program& program) {
    auto const [verified, size] = op_check_sig_common(program, error::op_check_sig_verify_failed);
    program.get_metrics().add_op_cost(program.top().size());
    program.get_metrics().add_sig_checks(1);
    program.get_metrics().add_hash_iterations(size, true);
    return verified;
}

inline
interpreter::result op_check_data_common(program& program, bool verify, interpreter::result err) {
    // TODO: implement it
    return err;
}

inline
interpreter::result interpreter::op_check_data_sig(program& program) {
    return op_check_data_common(program, false, error::op_check_data_sig);
}

inline
interpreter::result interpreter::op_check_data_sig_verify(program& program) {
    return op_check_data_common(program, true, error::op_check_data_sig_verify);
}

inline
interpreter::result op_check_multisig_internal(program& program) {
    // Combine Knuth structure with BCHN behavior
    
    // Step 1: Pop key_count (Knuth style)
    int32_t key_count;
    if ( ! program.pop(key_count)) {
        return error::multisig_missing_key_count;
    }

    if (key_count < 0 || key_count > 20) { // MAX_PUBKEYS_PER_MULTISIG
        return error::multisig_invalid_key_count;
    }

    // Account for operation count (pre-May 2025 behavior)
    if ( ! program.increment_operation_count(key_count)) {
        return error::multisig_invalid_key_count;
    }

    // Step 2: Pop public keys (Knuth style)
    data_stack public_keys;
    if ( ! program.pop(public_keys, key_count)) {
        return error::multisig_missing_pubkeys;
    }

    // Step 3: Pop signature_count (Knuth style)
    int32_t signature_count;
    if ( ! program.pop(signature_count)) {
        return error::multisig_missing_signature_count;
    }

    if (signature_count < 0 || signature_count > key_count) {
        return error::multisig_invalid_signature_count;
    }

    // Step 4: Pop signatures (Knuth style)
    data_stack endorsements;
    if ( ! program.pop(endorsements, signature_count)) {
        return error::multisig_missing_endorsements;
    }

    // Step 5: Handle Satoshi bug (Knuth style)
    if (program.empty()) {
        return error::multisig_empty_stack;
    }

    //*************************************************************************
    // CONSENSUS: Satoshi bug, discard stack element, malleable until bip147
    //            in BTC. bip147 is disabled in BCH.
    //*************************************************************************
    if ( ! program.pop().empty()
#if ! defined(KTH_CURRENCY_BCH)
        && chain::script::is_enabled(program.forks(), rule_fork::bip147_rule)
#endif
    ) {
        return error::multisig_satoshi_bug;
    }

    // Step 6: Early return for zero signatures (BCHN behavior)
    if (signature_count == 0) {
        return error::success;  // No signatures to verify
    }

    // Step 7: Signature verification algorithm (BCHN style)
    uint8_t sighash;
    ec_signature signature;
    der_signature distinguished;
    auto public_key = public_keys.begin();
    auto bip66 = chain::script::is_enabled(program.forks(), rule_fork::bip66_rule);
    auto bip143 = false;

    // Create subscript with endorsements stripped (sort of).  
    chain::script script_code(program.subscript());

    // BCHN-style algorithm: try each signature with remaining keys until match or exhaustion
    int sigs_remaining = signature_count;
    int keys_remaining = key_count;
    auto sig_iter = endorsements.begin();
    
    bool success = true;
    while (success && sigs_remaining > 0) {
        auto& endorsement = *sig_iter;
        
        // Parse endorsement (like Knuth)
        
        // BIP62: An empty endorsement is not considered lax encoding (following BCHN comment)
        bool is_empty_signature = endorsement.empty();
        if (!is_empty_signature && ! parse_endorsement(sighash, distinguished, std::move(endorsement))) {
            return error::invalid_signature_encoding;
        }

        // Parse DER signature into an EC signature (like Knuth) - skip for empty sigs
        if (!is_empty_signature) {
            if ( ! parse_signature(signature, distinguished, bip66)) {
                return bip66 ? error::invalid_signature_lax_encoding : error::invalid_signature_encoding;
            }
        }

        // Validate pubkey encoding (BCHN: only for keys actually tested)
        auto const pubkey_result = check_pubkey_encoding(*public_key, program);
        if (pubkey_result != error::success) {
            return pubkey_result;
        }

#if ! defined(KTH_CURRENCY_BCH)
        // Version condition preserves independence of bip141 and bip143.
        auto version = bip143 ? program.version() : script_version::unversioned;
#endif // ! KTH_CURRENCY_BCH

        // Try signature with current key (empty signatures always fail verification)
        bool sig_verified = false;
        if ( ! is_empty_signature) {
            auto const [res, size] = chain::script::check_signature(
                signature,
                sighash,
                *public_key,
                script_code,
                program.transaction(),
                program.input_index(),
                program.forks(),
#if ! defined(KTH_CURRENCY_BCH)
                version,
#endif // ! KTH_CURRENCY_BCH
                program.value()
            );
            sig_verified = res;
        } else {
            sig_verified = false;
        }

        if (sig_verified) {
            // Signature matched, advance to next signature
            ++sig_iter;
            --sigs_remaining;
        }
        
        // Always advance to next key (BCHN behavior)
        ++public_key;
        --keys_remaining;

        // If there are more signatures left than keys left, then too many signatures have failed
        if (sigs_remaining > keys_remaining) {
            success = false;
        }
    }

    return success ? error::success : error::incorrect_signature;
}

inline
interpreter::result interpreter::op_check_multisig(program& program) {
    
    auto const res = op_check_multisig_internal(program);

    // BIP62: only lax encoding fails the operation.
    if (res == error::invalid_signature_lax_encoding) {
        return error::op_check_multisig;
    }
    if (res == error::pubkey_type) {
        return res;
    }

    // Push the result (true for success, false for failure) onto the stack
    auto const success = (res == error::success);
    program.push(success);
    
    program.get_metrics().add_op_cost(program.top().size());
    
    // Only account for signature checks and hash operations if successful
    if (success) {
        // These metrics would normally be tracked during signature verification
        // but for now we'll keep it simple
        program.get_metrics().add_sig_checks(1);
    }

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
        return error::op_verify_empty_stack;
    }
    
    bool const stack_result = program.stack_true(false);
    
    if ( ! stack_result) {
        return error::op_verify_failed;
    }
    
    program.pop();
    return error::success;
}

inline
interpreter::result interpreter::op_check_locktime_verify(program& program) {
    // BIP65: nop2 subsumed by checklocktimeverify when bip65 fork is active.

    auto enabled = chain::script::is_enabled(program.forks(), rule_fork::bip65_rule);

    if ( ! chain::script::is_enabled(program.forks(), rule_fork::bip65_rule)) {
        return op_nop(opcode::nop2);
    }

    auto const& tx = program.transaction();
    auto const input_index = program.input_index();

    if (input_index >= tx.inputs().size()) {
        return error::invalid_script;
    }

    // BIP65: the tx sequence is 0xffffffff.
    if (tx.inputs()[input_index].is_final()) {
        return error::unsatisfied_locktime;
    }

    // BIP65: the stack is empty.
    // BIP65: extend the (signed) script number range to 5 bytes.
    number stack;
    if ( ! program.top(stack, max_check_locktime_verify_number_size)) {
        return error::invalid_script;
    }

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
    if ( ! chain::script::is_enabled(program.forks(), rule_fork::bip112_rule)) {
        return op_nop(opcode::nop3);
    }

    auto const& tx = program.transaction();
    auto const input_index = program.input_index();

    if (input_index >= tx.inputs().size()) {
        return error::invalid_script;
    }

    // BIP112: the stack is empty.
    // BIP112: extend the (signed) script number range to 5 bytes.
    number stack;
    if ( ! program.top(stack, max_check_sequence_verify_number_size)) {
        return error::invalid_script;
    }

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
    if ( ! chain::script::is_enabled(program.forks(), rule_fork::bch_gauss)) {
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
    
    // Subset of script starting at the most recent code separator (if any)
    // or the entire script if no code separators are present.
    auto const begin_code_hash = program.jump();
    auto const script_end = program.end();
    
    // Calculate the size of the active bytecode
    auto const script_size = static_cast<size_t>(script_end - begin_code_hash);
    
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
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_utxo_value;
}

inline
interpreter::result interpreter::op_utxo_bytecode(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_utxo_bytecode;
}

inline
interpreter::result interpreter::op_outpoint_tx_hash(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_outpoint_tx_hash;
}

inline
interpreter::result interpreter::op_outpoint_index(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_outpoint_index;
}

inline
interpreter::result interpreter::op_input_bytecode(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_input_bytecode;
}

inline
interpreter::result interpreter::op_input_sequence_number(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_input_sequence_number;
}

inline
interpreter::result interpreter::op_output_value(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_output_value;
}

inline
interpreter::result interpreter::op_output_bytecode(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_output_bytecode;
}

inline
interpreter::result interpreter::op_utxo_token_category(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_utxo_token_category;
}

inline
interpreter::result interpreter::op_utxo_token_commitment(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_utxo_token_commitment;
}

inline
interpreter::result interpreter::op_utxo_token_amount(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_utxo_token_amount;
}

inline
interpreter::result interpreter::op_output_token_category(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_output_token_category;
}

inline
interpreter::result interpreter::op_output_token_commitment(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_output_token_commitment;
}

inline
interpreter::result interpreter::op_output_token_amount(program& program) {
    // TODO(2025-Jul): Implement this operation.
    return error::op_reserved;
    return error::op_output_token_amount;
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
        case opcode::disabled_verif:
            return op_disabled(code);
        case opcode::disabled_vernotif:
            return op_disabled(code);
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
        case opcode::disabled_invert:
            return op_disabled(code);
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
        case opcode::reserved_137:
            return op_reserved(code);
        case opcode::reserved_138:
            return op_reserved(code);

    // numeric
        case opcode::add1:
            return op_add1(program);
        case opcode::sub1:
            return op_sub1(program);
        case opcode::disabled_mul2:
            return op_disabled(code);
        case opcode::disabled_div2:
            return op_disabled(code);
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
        case opcode::disabled_lshift:
            return op_disabled(code);
        case opcode::disabled_rshift:
            return op_disabled(code);
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
            return op_nop(code);
        case opcode::checklocktimeverify:
            return op_check_locktime_verify(program);
        case opcode::checksequenceverify:
            return op_check_sequence_verify(program);
        case opcode::nop4:
        case opcode::nop5:
        case opcode::nop6:
        case opcode::nop7:
        case opcode::nop8:
        case opcode::nop9:
        case opcode::nop10:
            //TODO: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS
            return op_nop(code);

        //TODO(kth): Implement OP_CHECKDATASIG and OP_CHECKDATASIGVERIFY


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
