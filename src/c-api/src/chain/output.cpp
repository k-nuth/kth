// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/output.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/token_data.hpp>

// Conversion functions
kth::domain::chain::output& kth_chain_output_cpp(kth_output_mut_t o) {
    return *static_cast<kth::domain::chain::output*>(o);
}
kth::domain::chain::output const& kth_chain_output_const_cpp(kth_output_const_t o) {
    return *static_cast<kth::domain::chain::output const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_output_mut_t kth_chain_output_construct_default() {
    return new kth::domain::chain::output();
}

kth_output_mut_t kth_chain_output_construct(uint64_t value, kth_script_const_t script) {
    return new kth::domain::chain::output(value, kth_chain_script_const_cpp(script), std::nullopt);
}

void kth_chain_output_destruct(kth_output_mut_t output) {
    if (output == nullptr) return;
    delete &kth_chain_output_cpp(output);
}

kth_output_mut_t kth_chain_output_copy(kth_output_const_t other) {
    return new kth::domain::chain::output(kth_chain_output_const_cpp(other));
}

kth_bool_t kth_chain_output_is_valid(kth_output_const_t output) {
    return kth::bool_to_int(kth_chain_output_const_cpp(output).is_valid());
}

uint64_t kth_chain_output_value(kth_output_const_t output) {
    return kth_chain_output_const_cpp(output).value();
}

void kth_chain_output_set_value(kth_output_mut_t output, uint64_t value) {
    kth_chain_output_cpp(output).set_value(value);
}

kth_script_const_t kth_chain_output_script(kth_output_const_t output) {
    return &kth_chain_output_const_cpp(output).script();
}

kth_token_data_const_t kth_chain_output_token_data(kth_output_const_t output) {
    auto const& td = kth_chain_output_const_cpp(output).token_data();
    return td.has_value() ? &td.value() : nullptr;
}

void kth_chain_output_set_token_data(kth_output_mut_t output, kth_token_data_const_t value) {
    if (value != nullptr) {
        kth_chain_output_cpp(output).set_token_data(kth_chain_token_data_const_cpp(value));
    } else {
        kth_chain_output_cpp(output).set_token_data(std::nullopt);
    }
}

kth_size_t kth_chain_output_signature_operations(kth_output_const_t output, kth_bool_t bip141) {
    return kth_chain_output_const_cpp(output).signature_operations(kth::int_to_bool(bip141));
}

kth_bool_t kth_chain_output_is_dust(kth_output_const_t output, uint64_t minimum_output_value) {
    return kth::bool_to_int(kth_chain_output_const_cpp(output).is_dust(minimum_output_value));
}

void kth_chain_output_reset(kth_output_mut_t output) {
    kth_chain_output_cpp(output).reset();
}

kth_error_code_t kth_chain_output_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_output_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::output::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

uint8_t const* kth_chain_output_to_data(kth_output_const_t output, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_output_const_cpp(output).to_data(kth::int_to_bool(wire));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_output_serialized_size(kth_output_const_t output, kth_bool_t wire) {
    return kth_chain_output_const_cpp(output).serialized_size(kth::int_to_bool(wire));
}

void kth_chain_output_set_script(kth_output_mut_t output, kth_script_const_t value) {
    kth_chain_output_cpp(output).set_script(kth_chain_script_const_cpp(value));
}

kth_output_mut_t kth_chain_output_construct_with_token_fungible(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, int64_t token_amount) {
    using kth::domain::chain::amount_t;
    auto token_category_cpp = kth::to_array(token_category->hash);
    kth::domain::chain::token_data_t token_data = {
        token_category_cpp,
        kth::domain::chain::fungible{amount_t{token_amount}}
    };
    return new kth::domain::chain::output(value, kth_chain_script_const_cpp(script), std::move(token_data));
}

kth_output_mut_t kth_chain_output_construct_with_token_non_fungible(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, kth_token_capability_t capability, uint8_t const* commitment_data, kth_size_t commitment_n) {
    auto token_category_cpp = kth::to_array(token_category->hash);
    auto capability_cpp = kth::token_capability_to_cpp(capability);
    kth::data_chunk commitment_cpp(commitment_data, std::next(commitment_data, commitment_n));
    kth::domain::chain::token_data_t token_data = {
        token_category_cpp,
        kth::domain::chain::non_fungible{capability_cpp, std::move(commitment_cpp)}
    };
    return new kth::domain::chain::output(value, kth_chain_script_const_cpp(script), std::move(token_data));
}

kth_output_mut_t kth_chain_output_construct_with_token_both(uint64_t value, kth_script_const_t script, kth_hash_t const* token_category, int64_t token_amount, kth_token_capability_t capability, uint8_t const* commitment_data, kth_size_t commitment_n) {
    using kth::domain::chain::amount_t;
    auto token_category_cpp = kth::to_array(token_category->hash);
    auto capability_cpp = kth::token_capability_to_cpp(capability);
    kth::data_chunk commitment_cpp(commitment_data, std::next(commitment_data, commitment_n));
    kth::domain::chain::token_data_t token_data = {
        token_category_cpp,
        kth::domain::chain::both_kinds{
            kth::domain::chain::fungible{amount_t{token_amount}},
            kth::domain::chain::non_fungible{capability_cpp, std::move(commitment_cpp)}
        }
    };
    return new kth::domain::chain::output(value, kth_chain_script_const_cpp(script), std::move(token_data));
}

kth_output_mut_t kth_chain_output_construct_with_token_data(uint64_t value, kth_script_const_t script, kth_token_data_const_t token_data) {
    return new kth::domain::chain::output(value, kth_chain_script_const_cpp(script), kth_chain_token_data_const_cpp(token_data));
}

} // extern "C"
