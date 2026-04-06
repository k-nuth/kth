// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <kth/capi/chain/script.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>

// Conversion functions
kth::domain::chain::script& kth_chain_script_cpp(kth_script_mut_t o) {
    return *static_cast<kth::domain::chain::script*>(o);
}
kth::domain::chain::script const& kth_chain_script_const_cpp(kth_script_const_t o) {
    return *static_cast<kth::domain::chain::script const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

kth_script_mut_t kth_chain_script_construct_default() {
    return new kth::domain::chain::script();
}

void kth_chain_script_destruct(kth_script_mut_t script) {
    if (script == nullptr) return;
    delete &kth_chain_script_cpp(script);
}

kth_script_mut_t kth_chain_script_copy(kth_script_const_t other) {
    return new kth::domain::chain::script(kth_chain_script_const_cpp(other));
}

kth_bool_t kth_chain_script_equal(kth_script_const_t a, kth_script_const_t b) {
    return kth::bool_to_int(kth_chain_script_const_cpp(a) == kth_chain_script_const_cpp(b));
}

kth_bool_t kth_chain_script_is_valid(kth_script_const_t script) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).is_valid());
}

uint8_t const* kth_chain_script_to_data(kth_script_const_t script, kth_bool_t prefix, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto data = kth_chain_script_const_cpp(script).to_data(kth::int_to_bool(prefix));
    *out_size = data.size();
    return kth::create_c_array(data);
}

kth_size_t kth_chain_script_serialized_size(kth_script_const_t script, kth_bool_t prefix) {
    return kth_chain_script_const_cpp(script).serialized_size(kth::int_to_bool(prefix));
}

uint8_t const* kth_chain_script_bytes(kth_script_const_t script, kth_size_t* out_size) {
    KTH_PRECONDITION(out_size != nullptr);
    auto const& bytes = kth_chain_script_const_cpp(script).bytes();
    *out_size = bytes.size();
    return kth::create_c_array(bytes);
}

kth_error_code_t kth_chain_script_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, kth_script_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::script::from_data(reader, kth::int_to_bool(wire));
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

kth_error_code_t kth_chain_script_from_data_with_size(uint8_t const* data, kth_size_t n, kth_size_t size, kth_script_mut_t* out_result) {
    KTH_PRECONDITION(data != nullptr);
    KTH_PRECONDITION(out_result != nullptr);
    kth::byte_reader reader({data, n});
    auto res = kth::domain::chain::script::from_data_with_size(reader, size);
    if ( ! res) {
        *out_result = nullptr;
        return kth::to_c_err(res.error());
    }
    *out_result = kth::move_or_copy_and_leak(std::move(*res));
    return kth_ec_success;
}

kth_bool_t kth_chain_script_is_valid_operations(kth_script_const_t script) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).is_valid_operations());
}

char const* kth_chain_script_to_string(kth_script_const_t script, kth_script_flags_t active_flags) {
    auto str = kth_chain_script_const_cpp(script).to_string(active_flags);
    return kth::create_c_str(str);
}

void kth_chain_script_clear(kth_script_mut_t script) {
    kth_chain_script_cpp(script).clear();
}

kth_bool_t kth_chain_script_empty(kth_script_const_t script) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).empty());
}

kth_size_t kth_chain_script_size(kth_script_const_t script) {
    return kth_chain_script_const_cpp(script).size();
}

kth_operation_list_const_t kth_chain_script_operations(kth_script_const_t script) {
    return &kth_chain_script_const_cpp(script).operations();
}

kth_operation_mut_t kth_chain_script_first_operation(kth_script_const_t script) {
    return kth::move_or_copy_and_leak(kth_chain_script_const_cpp(script).first_operation());
}

kth_bool_t kth_chain_script_is_push_only(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_push_only(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_relaxed_push(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_relaxed_push(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_coinbase_pattern(kth_operation_list_const_t ops, kth_size_t height) {
    return kth::bool_to_int(kth::domain::chain::script::is_coinbase_pattern(kth_chain_operation_list_const_cpp(ops), height));
}

kth_bool_t kth_chain_script_is_null_data_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_null_data_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_pay_multisig_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_pay_multisig_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_pay_public_key_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_pay_public_key_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_pay_public_key_hash_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_pay_public_key_hash_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_pay_script_hash_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_pay_script_hash_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_pay_script_hash_32_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_pay_script_hash_32_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_sign_multisig_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_sign_multisig_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_sign_public_key_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_sign_public_key_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_sign_public_key_hash_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_sign_public_key_hash_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_bool_t kth_chain_script_is_sign_script_hash_pattern(kth_operation_list_const_t ops) {
    return kth::bool_to_int(kth::domain::chain::script::is_sign_script_hash_pattern(kth_chain_operation_list_const_cpp(ops)));
}

kth_script_pattern_t kth_chain_script_pattern(kth_script_const_t script) {
    return kth::script_pattern_to_c(kth_chain_script_const_cpp(script).pattern());
}

kth_script_pattern_t kth_chain_script_output_pattern(kth_script_const_t script) {
    return kth::script_pattern_to_c(kth_chain_script_const_cpp(script).output_pattern());
}

kth_script_pattern_t kth_chain_script_input_pattern(kth_script_const_t script) {
    return kth::script_pattern_to_c(kth_chain_script_const_cpp(script).input_pattern());
}

kth_size_t kth_chain_script_sigops(kth_script_const_t script, kth_bool_t accurate) {
    return kth_chain_script_const_cpp(script).sigops(kth::int_to_bool(accurate));
}

kth_bool_t kth_chain_script_is_unspendable(kth_script_const_t script) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).is_unspendable());
}

void kth_chain_script_reset(kth_script_mut_t script) {
    kth_chain_script_cpp(script).reset();
}

kth_bool_t kth_chain_script_is_pay_to_script_hash(kth_script_const_t script, kth_script_flags_t flags) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).is_pay_to_script_hash(flags));
}

kth_bool_t kth_chain_script_is_pay_to_script_hash_32(kth_script_const_t script, kth_script_flags_t flags) {
    return kth::bool_to_int(kth_chain_script_const_cpp(script).is_pay_to_script_hash_32(flags));
}

} // extern "C"
