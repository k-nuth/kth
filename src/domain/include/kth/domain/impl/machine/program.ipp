// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_PROGRAM_IPP
#define KTH_DOMAIN_MACHINE_PROGRAM_IPP

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <kth/domain/chain/script.hpp>
#include <kth/domain/chain/transaction.hpp>
#include <kth/domain/constants.hpp>
#include <kth/domain/machine/operation.hpp>
#include <kth/infrastructure/machine/number.hpp>
#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth::domain::machine {

#if ! defined(KTH_CURRENCY_BCH)
using script_version = ::kth::infrastructure::machine::script_version;
#endif // ! KTH_CURRENCY_BCH

// Metrics
//-----------------------------------------------------------------------------
inline
metrics& program::get_metrics() {
    return metrics_;
}

inline
metrics const& program::get_metrics() const {
    return metrics_;
}

// Constant registers.
//-----------------------------------------------------------------------------

inline
bool program::is_valid() const {
    // Invalid operations indicates a failure deserializing individual ops.
    return script_.is_valid_operations() && !script_.is_unspendable();
}

inline
uint32_t program::forks() const {
    return forks_;
}

inline
size_t program::max_script_element_size() const {
    auto const galois_enabled = chain::script::is_enabled(forks(), rule_fork::bch_galois);
    return galois_enabled ? ::kth::may2025::max_push_data_size : max_push_data_size_legacy;
}

inline
size_t program::max_integer_size_legacy() const {
    auto const gauss_enabled = chain::script::is_enabled(forks(), rule_fork::bch_gauss);
    return gauss_enabled ? max_number_size_64_bits : max_number_size_32_bits;
}

inline
bool program::is_chip_vm_limits_enabled() const {
    return chain::script::is_enabled(forks(), rule_fork::bch_galois);
}

inline
uint32_t program::input_index() const {
    return input_index_;
}

inline
uint64_t program::value() const {
    return value_;
}

#if ! defined(KTH_CURRENCY_BCH)
inline
script_version program::version() const {
    return version_;
}
#endif // ! KTH_CURRENCY_BCH

inline
chain::transaction const& program::transaction() const {
    return transaction_;
}

inline
std::optional<script_execution_context> const& program::context() const {
    return context_;
}

// Program registers.
//-----------------------------------------------------------------------------

inline
program::op_iterator program::begin() const {
    return script_.begin();
}

inline
program::op_iterator program::jump() const {
    return jump_;
}

inline
program::op_iterator program::end() const {
    return script_.end();
}

inline
size_t program::operation_count() const {
    return operation_count_;
}

// Instructions.
//-----------------------------------------------------------------------------

inline
bool operation_overflow(size_t count) {
    return count > max_counted_ops;
}

inline
bool program::increment_operation_count(operation const& op) {
    // Addition is safe due to script size validation.
    if (operation::is_counted(op.code())) {
        ++operation_count_;
    }

    return !operation_overflow(operation_count_);
}

inline
bool program::increment_operation_count(int32_t public_keys) {
    static auto const max_keys = int32_t(max_script_public_keys);

    // bit.ly/2d1bsdB
    if (public_keys < 0 || public_keys > max_keys) {
        return false;
    }

    // Addition is safe due to script size validation.
    operation_count_ += public_keys;
    return !operation_overflow(operation_count_);
}

inline
bool program::set_jump_register(operation const& op, int32_t offset) {
    if (script_.empty()) {
        return false;
    }

    auto const finder = [&op](operation const& operation) {
        return &operation == &op;
    };

    // This is not efficient but is simplifying and subscript is rarely used.
    // Otherwise we must track the program counter through each evaluation.
    jump_ = std::find_if(script_.begin(), script_.end(), finder);

    if (jump_ == script_.end()) {
        return false;
    }

    // This does not require guard because op_codeseparator can only increment.
    // Even if the opcode is last in the sequnce the increment is valid (end).
    KTH_ASSERT_MSG(offset == 1, "unguarded jump offset");

    jump_ += offset;
    return true;
}

// Primary stack (push).
//-----------------------------------------------------------------------------

// push
inline
void program::push(bool value) {
    push_move(value ? value_type{number::positive_1} : value_type{});
}

// Be explicit about the intent to move or copy, to get compiler help.
inline
void program::push_move(value_type&& item) {
    primary_.push_back(std::move(item));
}

// Be explicit about the intent to move or copy, to get compiler help.
inline
void program::push_copy(value_type const& item) {
    primary_.push_back(item);
}

// Primary stack (pop).
//-----------------------------------------------------------------------------

// This must be guarded.
inline
data_chunk program::pop() {
    KTH_ASSERT( ! empty());
    auto value = std::move(primary_.back());
    primary_.pop_back();
    return value;
}

inline
bool program::pop(int32_t& out_value) {
    number value;
    if ( ! pop(value, max_integer_size_legacy())) {
        return false;
    }
    out_value = value.int32();
    return true;
}

inline
bool program::pop(int64_t& out_value) {
    number value;
    if ( ! pop(value, max_integer_size_legacy())) {
        return false;
    }
    out_value = value.int64();
    return true;
}

inline
bool program::pop(number& out_number, size_t maximum_size) {
    return !empty() && out_number.set_data(pop(), maximum_size);
}

inline
bool program::pop_binary(number& first, number& second) {
    // The right hand side number is at the top of the stack.
    return pop(first, max_integer_size_legacy()) && 
           pop(second, max_integer_size_legacy());
}

inline
bool program::pop_ternary(number& first, number& second, number& third) {
    // The upper bound is at stack top, lower bound next, value next.
    return pop(first, max_integer_size_legacy()) && pop(second, max_integer_size_legacy()) && pop(third, max_integer_size_legacy());
}

// Determines if popped value is valid post-pop stack index and returns index.
inline
bool program::pop_position(stack_iterator& out_position) {
    int32_t signed_index;
    if ( ! pop(signed_index)) {
        return false;
    }

    // Ensure the index is within bounds.

    if (signed_index < 0) {
        return false;
    }

    auto const index = uint32_t(signed_index);

    if (index >= size()) {
        return false;
    }

    out_position = position(index);
    return true;
}

// pop1/pop2/.../pop[count]
inline
bool program::pop(data_stack& section, size_t count) {
    if (size() < count) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        section.push_back(pop());
    }

    return true;
}

// Primary push/pop optimizations (active).
//-----------------------------------------------------------------------------

// pop1/pop2/.../pop[index]/push[index]/.../push2/push1/push[index]
inline
void program::duplicate(size_t index) {
    push_copy(item(index));
}

// pop1/pop2/push1/push2
inline
void program::swap(size_t index_left, size_t index_right) {
    using std::swap;
    swap(item(index_left), item(index_right));

    // // TODO(legacy): refactor to allow DRY without const_cast here.
    // std::swap(
    //     const_cast<data_stack::value_type&>(item(index_left)),
    //     const_cast<data_stack::value_type&>(item(index_right)));
}

// pop1/pop2/.../pop[pos-1]/pop[pos]/push[pos-1]/.../push2/push1
inline
void program::erase(const stack_iterator& position) {
    primary_.erase(position);
}

// pop1/pop2/.../pop[i]/pop[first]/.../pop[last]/push[i]/.../push2/push1
inline
void program::erase(const stack_iterator& first, const stack_iterator& last) {
    primary_.erase(first, last);
}

// Primary push/pop optimizations (passive).
//-----------------------------------------------------------------------------

// private
inline
bool program::stack_to_bool(bool clean) const {
    if (clean && primary_.size() != 1) {
        return false;
    }

    auto const& back = primary_.back();

    // It's not non-zero it's the terminating negative sentinel.
    for (auto it = back.begin(); it != back.end(); ++it) {
        if (*it != 0) {
            return !(it == back.end() - 1 && *it == number::negative_0);
        }
    }

    return false;
}

inline
bool program::empty() const {
    return primary_.empty();
}

// This must be guarded (intended for interpreter internal use).
inline
bool program::stack_true(bool clean) const {
    KTH_ASSERT( ! empty());
    return stack_to_bool(clean);
}

// This is safe to call when empty (intended for completion handlers).
inline
bool program::stack_result(bool clean) const {
    return !empty() && stack_true(clean);
}

inline
bool program::is_stack_overflow() const {
    // bit.ly/2cowHlP
    // Addition is safe due to script size validation.
    return size() + alternate_.size() > max_stack_size;
}

inline
bool program::if_(operation const& op) const {
    // Skip operation if failed and the operator is unconditional.
    return op.is_conditional() || succeeded();
}

inline
data_stack::value_type const& program::item(size_t index) const {
    return *position(index);
}

inline
data_stack::value_type& program::item(size_t index) {
    return *position(index);
}

// This must be guarded.
inline
data_chunk& program::top() {
    KTH_ASSERT( ! empty());
    return primary_.back();
}

inline
data_chunk const& program::top() const {
    KTH_ASSERT( ! empty());
    return primary_.back();
}

inline
bool program::top(number& out_number, size_t maximum_size) const {
    return !empty() && out_number.set_data(item(0), maximum_size);
}


inline
program::stack_iterator program::position(size_t index) const {
    // Subtracting 1 makes the stack indexes zero-based (unlike satoshi).
    KTH_ASSERT(index < size());
    return (primary_.end() - 1) - index;
}

inline
program::stack_mutable_iterator program::position(size_t index) {
    // Subtracting 1 makes the stack indexes zero-based (unlike satoshi).
    KTH_ASSERT(index < size());
    return (primary_.end() - 1) - index;
}

inline
size_t program::index(stack_iterator const& position) const {
    return size() - (position - primary_.begin());
}

// Pop jump-to-end, push all back, use to construct a script.
inline
operation::list program::subscript() const {
    operation::list ops;

    for (auto op = jump(); op != end(); ++op) {
        ops.push_back(*op);
    }

    return ops;
}

inline
size_t program::size() const {
    return primary_.size();
}

inline
size_t program::conditional_stack_size() const {
    return condition_.size();
}


// Alternate stack.
//-----------------------------------------------------------------------------

inline
bool program::empty_alternate() const {
    return alternate_.empty();
}

inline
void program::push_alternate(value_type&& value) {
    alternate_.push_back(std::move(value));
}

// This must be guarded.
inline
program::value_type program::pop_alternate() {
    KTH_ASSERT( ! alternate_.empty());
    auto const value = alternate_.back();
    alternate_.pop_back();
    return value;
}

// Conditional stack.
//-----------------------------------------------------------------------------

inline
void program::open(bool value) {
    negative_count_ += (value ? 0 : 1);
    condition_.push_back(value);
}

// This must be guarded.
inline
void program::negate() {
    KTH_ASSERT( ! closed());

    auto const value = condition_.back();
    negative_count_ += (value ? 1 : -1);
    condition_.back() = !value;

    // Optimized above to avoid succeeded loop.
    ////condition_.back() = !condition_.back();
}

// This must be guarded.
inline
void program::close() {
    KTH_ASSERT( ! closed());

    auto const value = condition_.back();
    negative_count_ += (value ? 0 : -1);
    condition_.pop_back();

    // Optimized above to avoid succeeded loop.
    ////condition_.pop_back();
}

inline
bool program::closed() const {
    return condition_.empty();
}

inline
bool program::succeeded() const {
    return negative_count_ == 0;

    // Optimized above to avoid succeeded loop.
    ////auto const is_true = [](bool value) { return value; };
    ////return std::all_of(condition_.begin(), condition_.end(), true);
}

//TODO: temp:
inline
chain::script const& program::get_script() const {
    return script_;
}

} // namespace kth::domain::machine

#endif
