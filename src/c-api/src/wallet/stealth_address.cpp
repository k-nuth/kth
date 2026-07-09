// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/stealth_address.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/wallet/stealth_address.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::stealth_address;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_stealth_address_construct_from_data(uint8_t const* decoded, kth_size_t n, KTH_OUT_OWNED kth_stealth_address_mut_t* out) {
    KTH_PRECONDITION(decoded != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const decoded_cpp = n != 0 ? kth::data_chunk(decoded, decoded + n) : kth::data_chunk{};
    auto result = cpp_t::from_data(decoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_address_parse_from(char const* encoded, KTH_OUT_OWNED kth_stealth_address_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from(encoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_address_from_components(kth_binary_const_t filter, kth_ec_compressed_t const* scan_key, kth_ec_compressed_list_const_t spend_keys, uint8_t signatures, uint8_t version, KTH_OUT_OWNED kth_stealth_address_mut_t* out) {
    KTH_PRECONDITION(filter != nullptr);
    KTH_PRECONDITION(scan_key != nullptr);
    KTH_PRECONDITION(spend_keys != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    auto const scan_key_cpp = kth::ec_compressed_to_cpp(scan_key->data);
    auto const& spend_keys_cpp = kth::cpp_ref<kth::point_list>(spend_keys);
    auto result = cpp_t::from_components(filter_cpp, scan_key_cpp, spend_keys_cpp, signatures, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_address_from_components_unsafe(kth_binary_const_t filter, uint8_t const* scan_key, kth_ec_compressed_list_const_t spend_keys, uint8_t signatures, uint8_t version, KTH_OUT_OWNED kth_stealth_address_mut_t* out) {
    KTH_PRECONDITION(filter != nullptr);
    KTH_PRECONDITION(scan_key != nullptr);
    KTH_PRECONDITION(spend_keys != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    auto const scan_key_cpp = kth::ec_compressed_to_cpp(scan_key);
    auto const& spend_keys_cpp = kth::cpp_ref<kth::point_list>(spend_keys);
    auto result = cpp_t::from_components(filter_cpp, scan_key_cpp, spend_keys_cpp, signatures, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_wallet_stealth_address_destruct(kth_stealth_address_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_stealth_address_mut_t kth_wallet_stealth_address_copy(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_stealth_address_equals(kth_stealth_address_const_t self, kth_stealth_address_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_stealth_address_not_equal(kth_stealth_address_const_t self, kth_stealth_address_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_stealth_address_less(kth_stealth_address_const_t self, kth_stealth_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_stealth_address_greater(kth_stealth_address_const_t self, kth_stealth_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_stealth_address_less_or_equal(kth_stealth_address_const_t self, kth_stealth_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_stealth_address_greater_or_equal(kth_stealth_address_const_t self, kth_stealth_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

uint8_t kth_wallet_stealth_address_version(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).version();
}

kth_ec_compressed_t kth_wallet_stealth_address_scan_key(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).scan_key());
}

kth_ec_compressed_list_const_t kth_wallet_stealth_address_spend_keys(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).spend_keys());
}

uint8_t kth_wallet_stealth_address_signatures(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).signatures();
}

kth_binary_const_t kth_wallet_stealth_address_filter(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).filter());
}

uint8_t* kth_wallet_stealth_address_to_chunk(kth_stealth_address_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).to_chunk();
    return kth::create_c_array(data, *out_size);
}

char* kth_wallet_stealth_address_to_string(kth_stealth_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}

} // extern "C"
