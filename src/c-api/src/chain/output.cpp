// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/output.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/output.hpp>

// Conversion functions
kth::domain::chain::output& kth_chain_output_mut_cpp(kth_output_mut_t o) {
    return *static_cast<kth::domain::chain::output*>(o);
}
kth::domain::chain::output const& kth_chain_output_const_cpp(kth_output_const_t o) {
    return *static_cast<kth::domain::chain::output const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_output_mut_t kth_chain_output_construct_default(void) {
    return new kth::domain::chain::output();
}

kth_error_code_t kth_chain_output_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_output_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, n));
    auto wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::output::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = new kth::domain::chain::output(std::move(*result));
    return kth_ec_success;
}

kth_output_mut_t kth_chain_output_construct(uint64_t value, kth_script_const_t script, kth_token_data_const_t token_data) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    auto token_data_cpp = (token_data == nullptr ? std::nullopt : std::optional<kth::domain::chain::token_data_t>(kth_chain_token_data_const_cpp(token_data)));
    return new kth::domain::chain::output(value, script_cpp, token_data_cpp);
}


// Destructor

void kth_chain_output_destruct(kth_output_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_output_mut_cpp(self);
}


// Copy

kth_output_mut_t kth_chain_output_copy(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::output(kth_chain_output_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_output_equals(kth_output_const_t self, kth_output_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_output_const_cpp(self) == kth_chain_output_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_output_to_data(kth_output_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto wire_cpp = kth::int_to_bool(wire);
    auto data = kth_chain_output_const_cpp(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_output_serialized_size(kth_output_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto wire_cpp = kth::int_to_bool(wire);
    return kth_chain_output_const_cpp(self).serialized_size(wire_cpp);
}


// Getters

uint64_t kth_chain_output_value(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_output_const_cpp(self).value();
}

kth_script_const_t kth_chain_output_script(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_output_const_cpp(self).script());
}

kth_token_data_const_t kth_chain_output_token_data(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const& opt = kth_chain_output_const_cpp(self).token_data();
    return opt.has_value() ? &(*opt) : nullptr;
}


// Setters

void kth_chain_output_set_script(kth_output_mut_t self, kth_script_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_chain_script_const_cpp(value);
    kth_chain_output_mut_cpp(self).set_script(value_cpp);
}

void kth_chain_output_set_value(kth_output_mut_t self, uint64_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_output_mut_cpp(self).set_value(value);
}

void kth_chain_output_set_token_data(kth_output_mut_t self, kth_token_data_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto value_cpp = (value == nullptr ? std::nullopt : std::optional<kth::domain::chain::token_data_t>(kth_chain_token_data_const_cpp(value)));
    kth_chain_output_mut_cpp(self).set_token_data(value_cpp);
}


// Predicates

kth_bool_t kth_chain_output_is_valid(kth_output_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_output_const_cpp(self).is_valid());
}

kth_bool_t kth_chain_output_is_dust(kth_output_const_t self, uint64_t minimum_output_value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_output_const_cpp(self).is_dust(minimum_output_value));
}


// Operations

kth_payment_address_mut_t kth_chain_output_address_simple(kth_output_const_t self, kth_bool_t testnet) {
    KTH_PRECONDITION(self != nullptr);
    auto testnet_cpp = kth::int_to_bool(testnet);
    return new kth::domain::wallet::payment_address(kth_chain_output_const_cpp(self).address(testnet_cpp));
}

kth_payment_address_mut_t kth_chain_output_address(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::payment_address(kth_chain_output_const_cpp(self).address(p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_chain_output_addresses(kth_output_const_t self, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<kth::domain::wallet::payment_address>(kth_chain_output_const_cpp(self).addresses(p2kh_version, p2sh_version));
}

kth_size_t kth_chain_output_signature_operations(kth_output_const_t self, kth_bool_t bip141) {
    KTH_PRECONDITION(self != nullptr);
    auto bip141_cpp = kth::int_to_bool(bip141);
    return kth_chain_output_const_cpp(self).signature_operations(bip141_cpp);
}

void kth_chain_output_reset(kth_output_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_output_mut_cpp(self).reset();
}

} // extern "C"
