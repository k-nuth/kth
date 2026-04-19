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
    return script_->is_valid_operations() && !script_->is_unspendable();
}

inline
script_flags_t program::flags() const {
    return flags_;
}

inline
size_t program::max_script_element_size() const {
    return is_chip_vm_limits_enabled() ? ::kth::may2025::max_push_data_size : max_push_data_size_legacy;
}

inline
size_t program::max_integer_size_legacy() const {
    auto const integers_64_enabled = chain::script::is_enabled(flags(), script_flags::bch_64bit_integers);
    return integers_64_enabled ? max_number_size_64_bits : max_number_size_32_bits;
}

inline
size_t program::max_integer_size() const {
    if (is_bigint_enabled()) return ::kth::may2025::max_push_data_size;
    return max_integer_size_legacy();
}

inline
bool program::is_chip_vm_limits_enabled() const {
    return chain::script::is_enabled(flags(), script_flags::bch_vm_limits);
}

inline
bool program::is_bigint_enabled() const {
    return chain::script::is_enabled(flags(), script_flags::bch_bigint);
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
    return *transaction_;
}

inline
std::optional<script_execution_context> const& program::context() const {
    return context_;
}

// Program registers.
//-----------------------------------------------------------------------------

inline
program::op_iterator program::begin() const {
    return active_frames_.empty()
        ? script_->begin()
        : active_frames_.back().script->begin();
}

inline
program::op_iterator program::jump() const {
    return active_frames_.empty() ? jump_ : active_frames_.back().jump;
}

inline
program::op_iterator program::end() const {
    return active_frames_.empty()
        ? script_->end()
        : active_frames_.back().script->end();
}

inline
chain::script const& program::get_script() const {
    return active_frames_.empty() ? *script_ : *active_frames_.back().script;
}

inline
void program::set_active_script(chain::script const* s) {
    // Semantically "push an active-script frame". OP_INVOKE calls
    // this with a non-null `s`; the only null-arg caller is legacy
    // test / hand-written code that wants to drop the override, so
    // treat that as a pop.
    if (s == nullptr) {
        reset_active_script();
        return;
    }
    active_frames_.push_back({s, s->begin()});
}

inline
void program::reset_active_script() {
    // Pop the most recent frame. No-op on the outermost frame, so
    // double-pops can't corrupt the state.
    if ( ! active_frames_.empty()) {
        active_frames_.pop_back();
    }
}

inline
size_t program::operation_count() const {
    return operation_count_;
}

// Instructions.
//-----------------------------------------------------------------------------

inline
bool exceeds_op_count_limit(size_t count) {
    return count > max_counted_ops;
}

inline
bool program::increment_operation_count(operation const& op) {
    // Addition is safe due to script size validation.
    if (operation::is_counted(op.code())) {
        ++operation_count_;
    }

    // May 2025 (VM limits): the legacy 201-op limit is replaced by op_cost.
    if (is_chip_vm_limits_enabled()) return true;

    return ! exceeds_op_count_limit(operation_count_);
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

    // May 2025 (VM limits): the legacy 201-op limit is replaced by op_cost.
    if (is_chip_vm_limits_enabled()) return true;

    return ! exceeds_op_count_limit(operation_count_);
}

inline
bool program::set_jump_register(operation const& op, int32_t offset) {
    auto const& active = get_script();
    if (active.empty()) {
        return false;
    }

    auto const finder = [&op](operation const& operation) {
        return &operation == &op;
    };

    // This is not efficient but is simplifying and subscript is rarely used.
    // Otherwise we must track the program counter through each evaluation.
    auto found = std::find_if(active.begin(), active.end(), finder);

    if (found == active.end()) {
        return false;
    }

    // This does not require guard because op_codeseparator can only increment.
    // Even if the opcode is last in the sequnce the increment is valid (end).
    KTH_ASSERT_MSG(offset == 1, "unguarded jump offset");

    // Write through to the active frame if we're inside an OP_INVOKE,
    // otherwise to the outermost state.
    if (active_frames_.empty()) {
        jump_ = found + offset;
    } else {
        active_frames_.back().jump = found + offset;
    }
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

inline
void program::drop() {
    KTH_ASSERT( ! empty());
    primary_.pop_back();
}

// This must be guarded.
inline
data_chunk program::pop() {
    KTH_ASSERT( ! empty());
    auto value = std::move(primary_.back());
    primary_.pop_back();
    return value;
}

inline
std::expected<int32_t, error::error_code_t> program::pop_int32() {
    auto num = pop_number(max_integer_size_legacy());
    if ( ! num) return std::unexpected(num.error());
    return num->int32();
}

inline
std::expected<int64_t, error::error_code_t> program::pop_int64() {
    auto num = pop_number(max_integer_size_legacy());
    if ( ! num) return std::unexpected(num.error());
    return num->int64();
}

inline
std::expected<number, error::error_code_t> program::pop_number(size_t maximum_size) {
    if (empty()) {
        return std::unexpected(error::insufficient_main_stack);
    }
    auto data = pop();
    // MINIMALNUM: numbers must be minimally encoded
    if ((flags() & script_flags::bch_minimaldata) != 0
        && ! number::is_minimally_encoded(data, maximum_size)) {
        return std::unexpected(error::minimal_number);
    }
    number result;
    if ( ! result.set_data(data, maximum_size)) {
        return std::unexpected(error::invalid_operand_size);
    }
    return result;
}

inline
std::expected<std::pair<number, number>, error::error_code_t> program::pop_binary() {
    // The right hand side number is at the top of the stack.
    auto first = pop_number(max_integer_size_legacy());
    if ( ! first) return std::unexpected(first.error());
    auto second = pop_number(max_integer_size_legacy());
    if ( ! second) return std::unexpected(second.error());
    return std::pair{std::move(*first), std::move(*second)};
}

inline
std::expected<std::tuple<number, number, number>, error::error_code_t> program::pop_ternary() {
    // The upper bound is at stack top, lower bound next, value next.
    auto first = pop_number(max_integer_size_legacy());
    if ( ! first) return std::unexpected(first.error());
    auto second = pop_number(max_integer_size_legacy());
    if ( ! second) return std::unexpected(second.error());
    auto third = pop_number(max_integer_size_legacy());
    if ( ! third) return std::unexpected(third.error());
    return std::tuple{std::move(*first), std::move(*second), std::move(*third)};
}

// ── BigInt pop functions ──────────────────────────────────────────────────────

inline
std::expected<big_number, error::error_code_t> program::pop_big_number(size_t maximum_size) {
    if (empty()) {
        return std::unexpected(error::insufficient_main_stack);
    }
    auto data = pop();
    if ((flags() & script_flags::bch_minimaldata) != 0
        && ! big_number::is_minimally_encoded(data, maximum_size)) {
        return std::unexpected(error::minimal_number);
    }
    big_number result;
    if ( ! result.set_data(data, maximum_size)) {
        return std::unexpected(error::invalid_operand_size);
    }
    return result;
}


inline
std::expected<std::pair<big_number, big_number>, error::error_code_t> program::pop_big_binary() {
    auto first = pop_big_number(max_integer_size());
    if ( ! first) return std::unexpected(first.error());
    auto second = pop_big_number(max_integer_size());
    if ( ! second) return std::unexpected(second.error());
    return std::pair{std::move(*first), std::move(*second)};
}

inline
std::expected<std::tuple<big_number, big_number, big_number>, error::error_code_t> program::pop_big_ternary() {
    auto first = pop_big_number(max_integer_size());
    if ( ! first) return std::unexpected(first.error());
    auto second = pop_big_number(max_integer_size());
    if ( ! second) return std::unexpected(second.error());
    auto third = pop_big_number(max_integer_size());
    if ( ! third) return std::unexpected(third.error());
    return std::tuple{std::move(*first), std::move(*second), std::move(*third)};
}

// Determines if popped value is valid post-pop stack index and returns the index.
inline
std::expected<uint32_t, error::error_code_t> program::pop_index() {
    auto const signed_index = pop_int32();
    if ( ! signed_index) {
        return std::unexpected(signed_index.error());
    }

    if (*signed_index < 0 || uint32_t(*signed_index) >= size()) {
        return std::unexpected(error::insufficient_main_stack);
    }

    return uint32_t(*signed_index);
}

// Determines if popped value is valid post-pop stack index and returns iterator.
inline
std::expected<program::stack_iterator, error::error_code_t> program::pop_position() {
    auto const index = pop_index();
    if ( ! index) {
        return std::unexpected(index.error());
    }
    return position(*index);
}

// pop1/pop2/.../pop[count]
inline
std::expected<data_stack, error::error_code_t> program::pop(size_t count) {
    if (size() < count) {
        return std::unexpected(error::insufficient_main_stack);
    }

    data_stack section;
    section.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        section.push_back(pop());
    }

    return section;
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
bool program::is_stack_overflow(size_t extra) const {
    return size() + alternate_.size() + extra > max_stack_size;
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
std::expected<number, error::error_code_t> program::top_number(size_t maximum_size) const {
    if (empty()) {
        return std::unexpected(error::insufficient_main_stack);
    }
    auto const& data = top();
    // MINIMALNUM: numbers must be minimally encoded
    if ((flags() & script_flags::bch_minimaldata) != 0
        && ! number::is_minimally_encoded(data, maximum_size)) {
        return std::unexpected(error::minimal_number);
    }
    number result;
    if ( ! result.set_data(data, maximum_size)) {
        return std::unexpected(error::invalid_operand_size);
    }
    return result;
}

// Like pop_big_number but reads from the top of the stack without popping.
// Used by ops that need to preserve the original value on failure (e.g. OP_1ADD).
inline
std::expected<big_number, error::error_code_t> program::top_big_number(size_t maximum_size) const {
    if (empty()) {
        return std::unexpected(error::insufficient_main_stack);
    }
    auto const& data = top();
    if ((flags() & script_flags::bch_minimaldata) != 0
        && ! big_number::is_minimally_encoded(data, maximum_size)) {
        return std::unexpected(error::minimal_number);
    }
    big_number result;
    if ( ! result.set_data(data, maximum_size)) {
        return std::unexpected(error::invalid_operand_size);
    }
    return result;
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



} // namespace kth::domain::machine

#endif
