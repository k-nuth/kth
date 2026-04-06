// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/input.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Conversion functions
kth::domain::chain::input& kth_chain_input_cpp(kth_input_mut_t o) {
    return *static_cast<kth::domain::chain::input*>(o);
}
kth::domain::chain::input const& kth_chain_input_const_cpp(kth_input_const_t o) {
    return *static_cast<kth::domain::chain::input const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_input_mut_t kth_chain_input_construct_default() {
    return new kth::domain::chain::input();
}

kth_input_mut_t kth_chain_input_construct(kth_output_point_const_t previous_output, kth_script_const_t script, uint32_t sequence) {
    return new kth::domain::chain::input(
        kth_chain_output_point_const_cpp(previous_output),
        kth_chain_script_const_cpp(script),
        sequence);
}

void kth_chain_input_destruct(kth_input_mut_t input) {
    if (input == nullptr) return;
    delete &kth_chain_input_cpp(input);
}

kth_input_mut_t kth_chain_input_copy(kth_input_const_t other) {
    return new kth::domain::chain::input(kth_chain_input_const_cpp(other));
}

kth_bool_t kth_chain_input_is_valid(kth_input_const_t input) {
    return kth::bool_to_int(kth_chain_input_const_cpp(input).is_valid());
}

uint8_t const* kth_chain_input_to_data(kth_input_const_t input, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_input_const_cpp(input).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_input_serialized_size(kth_input_const_t input, kth_bool_t wire) {
    return kth_chain_input_const_cpp(input).serialized_size(kth::int_to_bool(wire));
}

kth_output_point_const_t kth_chain_input_previous_output(kth_input_const_t input) {
    return &kth_chain_input_const_cpp(input).previous_output();
}

void kth_chain_input_set_previous_output(kth_input_mut_t input, kth_output_point_const_t value) {
    kth_chain_input_cpp(input).set_previous_output(kth_chain_output_point_const_cpp(value));
}

kth_script_const_t kth_chain_input_script(kth_input_const_t input) {
    return &kth_chain_input_const_cpp(input).script();
}

uint32_t kth_chain_input_sequence(kth_input_const_t input) {
    return kth_chain_input_const_cpp(input).sequence();
}

void kth_chain_input_set_sequence(kth_input_mut_t input, uint32_t value) {
    kth_chain_input_cpp(input).set_sequence(value);
}

kth_bool_t kth_chain_input_is_final(kth_input_const_t input) {
    return kth::bool_to_int(kth_chain_input_const_cpp(input).is_final());
}

kth_bool_t kth_chain_input_is_locked(kth_input_const_t input, kth_size_t block_height, uint32_t median_time_past) {
    return kth::bool_to_int(kth_chain_input_const_cpp(input).is_locked(block_height, median_time_past));
}

kth_size_t kth_chain_input_signature_operations(kth_input_const_t input, kth_bool_t bip16, kth_bool_t bip141) {
    return kth_chain_input_const_cpp(input).signature_operations(kth::int_to_bool(bip16), kth::int_to_bool(bip141));
}

kth_error_code_t kth_chain_input_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_input_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::input::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

void kth_chain_input_set_script(kth_input_mut_t input, kth_script_const_t value) {
    kth_chain_input_cpp(input).set_script(kth_chain_script_const_cpp(value));
}

void kth_chain_input_reset(kth_input_mut_t input) {
    kth_chain_input_cpp(input).reset();
}

} // extern "C"
