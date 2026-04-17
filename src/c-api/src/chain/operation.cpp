// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/operation.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/machine/operation.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::machine::operation;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_operation_mut_t kth_chain_operation_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_operation_construct_from_data(uint8_t const* data, kth_size_t n, KTH_OUT_OWNED kth_operation_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_operation_mut_t kth_chain_operation_construct_from_uncoded_minimal(uint8_t const* uncoded, kth_size_t n, kth_bool_t minimal) {
    KTH_PRECONDITION(uncoded != nullptr || n == 0);
    auto const uncoded_cpp = n != 0 ? kth::data_chunk(uncoded, uncoded + n) : kth::data_chunk{};
    auto const minimal_cpp = kth::int_to_bool(minimal);
    return kth::leak<cpp_t>(uncoded_cpp, minimal_cpp);
}

kth_operation_mut_t kth_chain_operation_construct_from_code(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::leak<cpp_t>(code_cpp);
}


// Destructor

void kth_chain_operation_destruct(kth_operation_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_operation_mut_t kth_chain_operation_copy(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_operation_equals(kth_operation_const_t self, kth_operation_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_operation_to_data(kth_operation_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data();
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_operation_serialized_size(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size();
}


// Getters

kth_opcode_t kth_chain_operation_code(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::opcode_to_c(kth::cpp_ref<cpp_t>(self).code());
}

uint8_t* kth_chain_operation_data(kth_operation_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& data = kth::cpp_ref<cpp_t>(self).data();
    return kth::create_c_array(data, *out_size);
}


// Predicates

kth_bool_t kth_chain_operation_is_valid(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}

kth_bool_t kth_chain_operation_is_push(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_push(code_cpp));
}

kth_bool_t kth_chain_operation_is_payload(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_payload(code_cpp));
}

kth_bool_t kth_chain_operation_is_counted(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_counted(code_cpp));
}

kth_bool_t kth_chain_operation_is_version(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_version(code_cpp));
}

kth_bool_t kth_chain_operation_is_numeric(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_numeric(code_cpp));
}

kth_bool_t kth_chain_operation_is_positive(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_positive(code_cpp));
}

kth_bool_t kth_chain_operation_is_reserved(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_reserved(code_cpp));
}

kth_bool_t kth_chain_operation_is_disabled(kth_opcode_t code, kth_script_flags_t active_flags) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_disabled(code_cpp, active_flags));
}

kth_bool_t kth_chain_operation_is_conditional(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_conditional(code_cpp));
}

kth_bool_t kth_chain_operation_is_relaxed_push(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return kth::bool_to_int(cpp_t::is_relaxed_push(code_cpp));
}

kth_bool_t kth_chain_operation_is_push_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_push());
}

kth_bool_t kth_chain_operation_is_counted_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_counted());
}

kth_bool_t kth_chain_operation_is_version_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_version());
}

kth_bool_t kth_chain_operation_is_positive_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_positive());
}

kth_bool_t kth_chain_operation_is_disabled_simple(kth_operation_const_t self, kth_script_flags_t active_flags) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_disabled(active_flags));
}

kth_bool_t kth_chain_operation_is_conditional_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_conditional());
}

kth_bool_t kth_chain_operation_is_relaxed_push_simple(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_relaxed_push());
}

kth_bool_t kth_chain_operation_is_oversized(kth_operation_const_t self, kth_size_t max_size) {
    KTH_PRECONDITION(self != nullptr);
    auto const max_size_cpp = kth::sz(max_size);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_oversized(max_size_cpp));
}

kth_bool_t kth_chain_operation_is_minimal_push(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_minimal_push());
}

kth_bool_t kth_chain_operation_is_nominal_push(kth_operation_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_nominal_push());
}


// Operations

kth_bool_t kth_chain_operation_from_string(kth_operation_mut_t self, char const* mnemonic) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(mnemonic != nullptr);
    auto const mnemonic_cpp = std::string(mnemonic);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).from_string(mnemonic_cpp));
}

char* kth_chain_operation_to_string(kth_operation_const_t self, kth_script_flags_t active_flags) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string(active_flags);
    return kth::create_c_str(s);
}


// Static utilities

kth_opcode_t kth_chain_operation_opcode_from_size(kth_size_t size) {
    auto const size_cpp = kth::sz(size);
    return kth::opcode_to_c(cpp_t::opcode_from_size(size_cpp));
}

kth_opcode_t kth_chain_operation_minimal_opcode_from_data(uint8_t const* data, kth_size_t n) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    return kth::opcode_to_c(cpp_t::minimal_opcode_from_data(data_cpp));
}

kth_opcode_t kth_chain_operation_nominal_opcode_from_data(uint8_t const* data, kth_size_t n) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    return kth::opcode_to_c(cpp_t::nominal_opcode_from_data(data_cpp));
}

kth_opcode_t kth_chain_operation_opcode_from_positive(uint8_t value) {
    return kth::opcode_to_c(cpp_t::opcode_from_positive(value));
}

uint8_t kth_chain_operation_opcode_to_positive(kth_opcode_t code) {
    auto const code_cpp = kth::opcode_to_cpp(code);
    return cpp_t::opcode_to_positive(code_cpp);
}

} // extern "C"
