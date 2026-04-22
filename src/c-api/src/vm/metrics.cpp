// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/vm/metrics.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/machine/metrics.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::metrics;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_vm_metrics_destruct(kth_metrics_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_metrics_mut_t kth_vm_metrics_copy(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

uint32_t kth_vm_metrics_sig_checks(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).sig_checks();
}

uint64_t kth_vm_metrics_op_cost(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).op_cost();
}

uint64_t kth_vm_metrics_hash_digest_iterations(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).hash_digest_iterations();
}


// Setters

void kth_vm_metrics_set_script_limits(kth_metrics_mut_t self, kth_script_flags_t flags, uint64_t script_sig_size) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_script_limits(flags, script_sig_size);
}

void kth_vm_metrics_set_native_script_limits(kth_metrics_mut_t self, kth_bool_t standard, uint64_t script_sig_size) {
    KTH_PRECONDITION(self != nullptr);
    auto const standard_cpp = kth::int_to_bool(standard);
    kth::cpp_ref<cpp_t>(self).set_native_script_limits(standard_cpp, script_sig_size);
}


// Predicates

kth_bool_t kth_vm_metrics_is_over_op_cost_limit(kth_metrics_const_t self, kth_script_flags_t flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_over_op_cost_limit(flags));
}

kth_bool_t kth_vm_metrics_is_over_op_cost_limit_simple(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_over_op_cost_limit());
}

kth_bool_t kth_vm_metrics_is_over_hash_iters_limit(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_over_hash_iters_limit());
}

kth_bool_t kth_vm_metrics_has_valid_script_limits(kth_metrics_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).has_valid_script_limits());
}


// Operations

void kth_vm_metrics_add_op_cost(kth_metrics_mut_t self, uint32_t cost) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).add_op_cost(cost);
}

void kth_vm_metrics_add_push_op(kth_metrics_mut_t self, uint32_t stack_item_length) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).add_push_op(stack_item_length);
}

void kth_vm_metrics_add_hash_iterations(kth_metrics_mut_t self, uint32_t message_length, kth_bool_t is_two_round_hash) {
    KTH_PRECONDITION(self != nullptr);
    auto const is_two_round_hash_cpp = kth::int_to_bool(is_two_round_hash);
    kth::cpp_ref<cpp_t>(self).add_hash_iterations(message_length, is_two_round_hash_cpp);
}

void kth_vm_metrics_add_sig_checks(kth_metrics_mut_t self, uint32_t n_checks) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).add_sig_checks(n_checks);
}

uint64_t kth_vm_metrics_composite_op_cost_script_flags(kth_metrics_const_t self, kth_script_flags_t flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).composite_op_cost(flags);
}

uint64_t kth_vm_metrics_composite_op_cost_bool(kth_metrics_const_t self, kth_bool_t standard) {
    KTH_PRECONDITION(self != nullptr);
    auto const standard_cpp = kth::int_to_bool(standard);
    return kth::cpp_ref<cpp_t>(self).composite_op_cost(standard_cpp);
}

} // extern "C"
