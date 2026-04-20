// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/program.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/machine/program.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::program;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_program_mut_t kth_vm_program_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_program_mut_t kth_vm_program_construct_from_script(kth_script_const_t script) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak<cpp_t>(script_cpp);
}

kth_program_mut_t kth_vm_program_construct_from_script_transaction_input_index_flags_value(kth_script_const_t script, kth_transaction_const_t transaction, uint32_t input_index, kth_script_flags_t flags, uint64_t value) {
    KTH_PRECONDITION(script != nullptr);
    KTH_PRECONDITION(transaction != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    auto const& transaction_cpp = kth::cpp_ref<kth::domain::chain::transaction>(transaction);
    return kth::leak<cpp_t>(script_cpp, transaction_cpp, input_index, flags, value);
}

kth_program_mut_t kth_vm_program_construct_from_script_transaction_input_index_flags_stack_value(kth_script_const_t script, kth_transaction_const_t transaction, uint32_t input_index, kth_script_flags_t flags, kth_data_stack_const_t stack, uint64_t value) {
    KTH_PRECONDITION(script != nullptr);
    KTH_PRECONDITION(transaction != nullptr);
    KTH_PRECONDITION(stack != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    auto const& transaction_cpp = kth::cpp_ref<kth::domain::chain::transaction>(transaction);
    auto stack_cpp = kth::cpp_ref<kth::data_stack>(stack);
    return kth::leak<cpp_t>(script_cpp, transaction_cpp, input_index, flags, std::move(stack_cpp), value);
}

kth_program_mut_t kth_vm_program_construct_from_script_program(kth_script_const_t script, kth_program_const_t x) {
    KTH_PRECONDITION(script != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    return kth::leak<cpp_t>(script_cpp, x_cpp);
}

kth_program_mut_t kth_vm_program_construct_from_script_program_move(kth_script_const_t script, kth_program_const_t x, kth_bool_t move) {
    KTH_PRECONDITION(script != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    auto x_cpp = kth::cpp_ref<cpp_t>(x);
    auto const move_cpp = kth::int_to_bool(move);
    return kth::leak<cpp_t>(script_cpp, std::move(x_cpp), move_cpp);
}


// Destructor

void kth_vm_program_destruct(kth_program_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_program_mut_t kth_vm_program_copy(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_metrics_const_t kth_vm_program_get_metrics(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).get_metrics());
}

kth_script_flags_t kth_vm_program_flags(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).flags();
}

kth_size_t kth_vm_program_max_script_element_size(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).max_script_element_size();
}

kth_size_t kth_vm_program_max_integer_size_legacy(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).max_integer_size_legacy();
}

kth_size_t kth_vm_program_max_integer_size(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).max_integer_size();
}

uint32_t kth_vm_program_input_index(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).input_index();
}

uint64_t kth_vm_program_value(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).value();
}

kth_transaction_const_t kth_vm_program_transaction(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).transaction());
}

kth_script_const_t kth_vm_program_get_script(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).get_script());
}

kth_size_t kth_vm_program_operation_count(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).operation_count();
}

kth_bool_t kth_vm_program_empty(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).empty());
}

uint8_t* kth_vm_program_top(kth_program_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& data = kth::cpp_ref<cpp_t>(self).top();
    return kth::create_c_array(data, *out_size);
}

kth_operation_list_mut_t kth_vm_program_subscript(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_list<kth::domain::machine::operation>(kth::cpp_ref<cpp_t>(self).subscript());
}

kth_size_t kth_vm_program_size(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).size();
}

kth_size_t kth_vm_program_conditional_stack_size(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).conditional_stack_size();
}

kth_bool_t kth_vm_program_empty_alternate(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).empty_alternate());
}

kth_bool_t kth_vm_program_closed(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).closed());
}

kth_bool_t kth_vm_program_succeeded(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).succeeded());
}


// Predicates

kth_bool_t kth_vm_program_is_valid(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}

kth_bool_t kth_vm_program_is_chip_vm_limits_enabled(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_chip_vm_limits_enabled());
}

kth_bool_t kth_vm_program_is_bigint_enabled(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_bigint_enabled());
}

kth_bool_t kth_vm_program_is_stack_overflow_simple(kth_program_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_stack_overflow());
}

kth_bool_t kth_vm_program_is_stack_overflow(kth_program_const_t self, kth_size_t extra) {
    KTH_PRECONDITION(self != nullptr);
    auto const extra_cpp = kth::sz(extra);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_stack_overflow(extra_cpp));
}


// Operations

void kth_vm_program_reset_active_script(kth_program_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset_active_script();
}

kth_error_code_t kth_vm_program_evaluate_simple(kth_program_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).evaluate().error);
}

kth_error_code_t kth_vm_program_evaluate(kth_program_mut_t self, kth_operation_const_t op) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(op != nullptr);
    auto const& op_cpp = kth::cpp_ref<kth::domain::machine::operation>(op);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).evaluate(op_cpp).error);
}

kth_bool_t kth_vm_program_increment_operation_count_operation(kth_program_mut_t self, kth_operation_const_t op) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(op != nullptr);
    auto const& op_cpp = kth::cpp_ref<kth::domain::machine::operation>(op);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).increment_operation_count(op_cpp));
}

kth_bool_t kth_vm_program_increment_operation_count_int32(kth_program_mut_t self, int32_t public_keys) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).increment_operation_count(public_keys));
}

kth_bool_t kth_vm_program_mark_code_separator(kth_program_mut_t self, kth_size_t pc) {
    KTH_PRECONDITION(self != nullptr);
    auto const pc_cpp = kth::sz(pc);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).mark_code_separator(pc_cpp));
}

void kth_vm_program_push(kth_program_mut_t self, kth_bool_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::int_to_bool(value);
    kth::cpp_ref<cpp_t>(self).push(value_cpp);
}

void kth_vm_program_push_move(kth_program_mut_t self, uint8_t const* item, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(item != nullptr || n == 0);
    auto item_cpp = n != 0 ? kth::data_chunk(item, item + n) : kth::data_chunk{};
    kth::cpp_ref<cpp_t>(self).push_move(std::move(item_cpp));
}

void kth_vm_program_push_copy(kth_program_mut_t self, uint8_t const* item, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(item != nullptr || n == 0);
    auto const item_cpp = n != 0 ? kth::data_chunk(item, item + n) : kth::data_chunk{};
    kth::cpp_ref<cpp_t>(self).push_copy(item_cpp);
}

void kth_vm_program_drop(kth_program_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).drop();
}

uint8_t* kth_vm_program_pop(kth_program_mut_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).pop();
    return kth::create_c_array(data, *out_size);
}

kth_error_code_t kth_vm_program_pop_int32(kth_program_mut_t self, int32_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const result = kth::cpp_ref<cpp_t>(self).pop_int32();
    if ( ! result) return kth::to_c_err(result.error());
    *out = static_cast<int32_t>(*result);
    return kth_ec_success;
}

kth_error_code_t kth_vm_program_pop_int64(kth_program_mut_t self, int64_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const result = kth::cpp_ref<cpp_t>(self).pop_int64();
    if ( ! result) return kth::to_c_err(result.error());
    *out = static_cast<int64_t>(*result);
    return kth_ec_success;
}

kth_error_code_t kth_vm_program_pop_index(kth_program_mut_t self, uint32_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    auto const result = kth::cpp_ref<cpp_t>(self).pop_index();
    if ( ! result) return kth::to_c_err(result.error());
    *out = static_cast<uint32_t>(*result);
    return kth_ec_success;
}

void kth_vm_program_duplicate(kth_program_mut_t self, kth_size_t index) {
    KTH_PRECONDITION(self != nullptr);
    auto const index_cpp = kth::sz(index);
    kth::cpp_ref<cpp_t>(self).duplicate(index_cpp);
}

void kth_vm_program_swap(kth_program_mut_t self, kth_size_t index_left, kth_size_t index_right) {
    KTH_PRECONDITION(self != nullptr);
    auto const index_left_cpp = kth::sz(index_left);
    auto const index_right_cpp = kth::sz(index_right);
    kth::cpp_ref<cpp_t>(self).swap(index_left_cpp, index_right_cpp);
}

kth_bool_t kth_vm_program_stack_true(kth_program_const_t self, kth_bool_t clean) {
    KTH_PRECONDITION(self != nullptr);
    auto const clean_cpp = kth::int_to_bool(clean);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).stack_true(clean_cpp));
}

kth_bool_t kth_vm_program_stack_result(kth_program_const_t self, kth_bool_t clean) {
    KTH_PRECONDITION(self != nullptr);
    auto const clean_cpp = kth::int_to_bool(clean);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).stack_result(clean_cpp));
}

kth_bool_t kth_vm_program_if_(kth_program_const_t self, kth_operation_const_t op) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(op != nullptr);
    auto const& op_cpp = kth::cpp_ref<kth::domain::machine::operation>(op);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).if_(op_cpp));
}

uint8_t* kth_vm_program_item(kth_program_const_t self, kth_size_t index, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const index_cpp = kth::sz(index);
    auto const& data = kth::cpp_ref<cpp_t>(self).item(index_cpp);
    return kth::create_c_array(data, *out_size);
}

void kth_vm_program_push_alternate(kth_program_mut_t self, uint8_t const* value, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr || n == 0);
    auto value_cpp = n != 0 ? kth::data_chunk(value, value + n) : kth::data_chunk{};
    kth::cpp_ref<cpp_t>(self).push_alternate(std::move(value_cpp));
}

uint8_t* kth_vm_program_pop_alternate(kth_program_mut_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).pop_alternate();
    return kth::create_c_array(data, *out_size);
}

void kth_vm_program_open(kth_program_mut_t self, kth_bool_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::int_to_bool(value);
    kth::cpp_ref<cpp_t>(self).open(value_cpp);
}

void kth_vm_program_negate(kth_program_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).negate();
}

void kth_vm_program_close(kth_program_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).close();
}

} // extern "C"
