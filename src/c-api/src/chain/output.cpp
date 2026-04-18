// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/output.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/output.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::output;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_output_mut_t kth_chain_output_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_output_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_output_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = cpp_t::from_data(data_cpp, wire_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_output_mut_t kth_chain_output_construct(uint64_t value, kth_script_const_t script, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    auto const token_data_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(token_data);
    return kth::leak<cpp_t>(value, script_cpp, token_data_cpp);
}


// Destructor

void kth_chain_output_destruct(kth_output_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_output_mut_t kth_chain_output_copy(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_output_equals(kth_output_const_t self, kth_output_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_output_to_data(kth_output_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_output_serialized_size(kth_output_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    return kth::cpp_ref<cpp_t>(self).serialized_size(wire_cpp);
}


// Getters

uint64_t kth_chain_output_value(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).value();
}

kth_script_const_t kth_chain_output_script(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).script());
}

kth_token_data_const_t kth_chain_output_token_data(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const& opt = kth::cpp_ref<cpp_t>(self).token_data();
    return opt.has_value() ? &(*opt) : nullptr;
}


// Setters

void kth_chain_output_set_script(kth_output_mut_t self, kth_script_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::script>(value);
    kth::cpp_ref<cpp_t>(self).set_script(value_cpp);
}

void kth_chain_output_set_value(kth_output_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_value(value);
}

void kth_chain_output_set_token_data(kth_output_mut_t self, kth_token_data_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::optional_cpp_ref<kth::domain::chain::token_data_t>(value);
    kth::cpp_ref<cpp_t>(self).set_token_data(value_cpp);
}


// Predicates

kth_bool_t kth_chain_output_is_valid(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}

kth_bool_t kth_chain_output_is_dust(kth_output_const_t self, uint64_t minimum_output_value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_dust(minimum_output_value));
}


// Operations

kth_payment_address_mut_t kth_chain_output_address_simple(kth_output_const_t self, kth_bool_t testnet) {
    KTH_PRECONDITION(self != nullptr);
    auto const testnet_cpp = kth::int_to_bool(testnet);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).address(testnet_cpp));
}

kth_payment_address_mut_t kth_chain_output_address(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).address(p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_chain_output_addresses(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_list<kth::domain::wallet::payment_address>(kth::cpp_ref<cpp_t>(self).addresses(p2kh_version, p2sh_version));
}

kth_size_t kth_chain_output_signature_operations(kth_output_const_t self, kth_bool_t bip141) {
    KTH_PRECONDITION(self != nullptr);
    auto const bip141_cpp = kth::int_to_bool(bip141);
    return kth::cpp_ref<cpp_t>(self).signature_operations(bip141_cpp);
}

void kth_chain_output_reset(kth_output_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
