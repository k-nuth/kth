// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/hd_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_public.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::hd_public;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_hd_public_mut_t kth_wallet_hd_public_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key(kth_hd_key_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key->data);
    return kth::leak_if_valid(cpp_t(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_unsafe(uint8_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::leak_if_valid(cpp_t(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix(kth_hd_key_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key->data);
    return kth::leak_if_valid(cpp_t(public_key_cpp, prefix));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix_unsafe(uint8_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::leak_if_valid(cpp_t(public_key_cpp, prefix));
}


// Destructor

void kth_wallet_hd_public_destruct(kth_hd_public_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_hd_public_mut_t kth_wallet_hd_public_copy(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_hd_public_equals(kth_hd_public_const_t self, kth_hd_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_hd_public_not_equal(kth_hd_public_const_t self, kth_hd_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_public_greater(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_public_less_or_equal(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_public_greater_or_equal(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

kth_bool_t kth_wallet_hd_public_valid(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).valid());
}

kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).chain_code());
}

kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_hd_lineage_t>(kth::cpp_ref<cpp_t>(self).lineage());
}

kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).point());
}

kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hd_key_t(kth::cpp_ref<cpp_t>(self).to_hd_key());
}

char* kth_wallet_hd_public_to_string(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}


// Operations

kth_hd_public_mut_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).derive_public(index));
}


// Static utilities

uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes) {
    return cpp_t::to_prefix(prefixes);
}

kth_error_code_t kth_wallet_hd_public_parse_from(char const* encoded, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from(encoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_parse_from_with_prefix(char const* encoded, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from_with_prefix(encoded_cpp, prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

} // extern "C"
