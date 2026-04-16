// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/input.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/input.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_input_mut_t kth_chain_input_construct_default(void) {
    return new kth::domain::chain::input();
}

kth_error_code_t kth_chain_input_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_input_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::input::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_input_mut_t kth_chain_input_construct(kth_output_point_const_t previous_output, kth_script_const_t script, uint32_t sequence) {
    KTH_PRECONDITION(previous_output != nullptr);
    KTH_PRECONDITION(script != nullptr);
    auto const& previous_output_cpp = kth::cpp_ref<kth::domain::chain::output_point>(previous_output);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::make_leaked<kth::domain::chain::input>(previous_output_cpp, script_cpp, sequence);
}


// Destructor

void kth_chain_input_destruct(kth_input_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::chain::input>(self);
}


// Copy

kth_input_mut_t kth_chain_input_copy(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::input(kth::cpp_ref<kth::domain::chain::input>(self));
}


// Equality

kth_bool_t kth_chain_input_equals(kth_input_const_t self, kth_input_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::input>(self) == kth::cpp_ref<kth::domain::chain::input>(other));
}


// Serialization

uint8_t* kth_chain_input_to_data(kth_input_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    auto const data = kth::cpp_ref<kth::domain::chain::input>(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_input_serialized_size(kth_input_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    return kth::cpp_ref<kth::domain::chain::input>(self).serialized_size(wire_cpp);
}


// Getters

kth_payment_address_mut_t kth_chain_input_address(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::domain::chain::input>(self).address());
}

kth_payment_address_list_mut_t kth_chain_input_addresses(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new std::vector<kth::domain::wallet::payment_address>(kth::cpp_ref<kth::domain::chain::input>(self).addresses());
}

kth_output_point_const_t kth_chain_input_previous_output(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::chain::input>(self).previous_output());
}

kth_script_const_t kth_chain_input_script(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<kth::domain::chain::input>(self).script());
}

uint32_t kth_chain_input_sequence(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::chain::input>(self).sequence();
}

kth_error_code_t kth_chain_input_extract_embedded_script(kth_input_const_t self, KTH_OUT_OWNED kth_script_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<kth::domain::chain::input>(self).extract_embedded_script();
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}


// Setters

void kth_chain_input_set_script(kth_input_mut_t self, kth_script_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::script>(value);
    kth::cpp_ref<kth::domain::chain::input>(self).set_script(value_cpp);
}

void kth_chain_input_set_previous_output(kth_input_mut_t self, kth_output_point_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::chain::output_point>(value);
    kth::cpp_ref<kth::domain::chain::input>(self).set_previous_output(value_cpp);
}

void kth_chain_input_set_sequence(kth_input_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::chain::input>(self).set_sequence(value);
}


// Predicates

kth_bool_t kth_chain_input_is_valid(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::input>(self).is_valid());
}

kth_bool_t kth_chain_input_is_final(kth_input_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::input>(self).is_final());
}

kth_bool_t kth_chain_input_is_locked(kth_input_const_t self, kth_size_t block_height, uint32_t median_time_past) {
    KTH_PRECONDITION(self != nullptr);
    auto const block_height_cpp = static_cast<size_t>(block_height);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::chain::input>(self).is_locked(block_height_cpp, median_time_past));
}


// Operations

void kth_chain_input_reset(kth_input_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::chain::input>(self).reset();
}

kth_size_t kth_chain_input_signature_operations(kth_input_const_t self, kth_bool_t bip16, kth_bool_t bip141) {
    KTH_PRECONDITION(self != nullptr);
    auto const bip16_cpp = kth::int_to_bool(bip16);
    auto const bip141_cpp = kth::int_to_bool(bip141);
    return kth::cpp_ref<kth::domain::chain::input>(self).signature_operations(bip16_cpp, bip141_cpp);
}

} // extern "C"
