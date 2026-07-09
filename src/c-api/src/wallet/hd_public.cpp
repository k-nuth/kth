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

kth_error_code_t kth_wallet_hd_public_from_hd_key(kth_hd_key_t const* key, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const key_cpp = kth::hd_key_to_cpp(key->data);
    auto result = cpp_t::from_hd_key(key_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_from_hd_key_unsafe(uint8_t const* key, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const key_cpp = kth::hd_key_to_cpp(key);
    auto result = cpp_t::from_hd_key(key_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_from_hd_key_with_prefix(kth_hd_key_t const* key, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const key_cpp = kth::hd_key_to_cpp(key->data);
    auto result = cpp_t::from_hd_key_with_prefix(key_cpp, prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_from_hd_key_with_prefix_unsafe(uint8_t const* key, uint32_t prefix, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const key_cpp = kth::hd_key_to_cpp(key);
    auto result = cpp_t::from_hd_key_with_prefix(key_cpp, prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_from_secret(kth_hash_t const* secret, kth_hash_t const* chain_code, kth_hd_lineage_t lineage, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(secret != nullptr);
    KTH_PRECONDITION(chain_code != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret->hash);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const chain_code_cpp = kth::hash_to_cpp(chain_code->hash);
    auto const lineage_cpp = kth::from_c_struct<kth::domain::wallet::hd_lineage>(lineage);
    auto result = cpp_t::from_secret(secret_cpp, chain_code_cpp, lineage_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_public_from_secret_unsafe(uint8_t const* secret, uint8_t const* chain_code, kth_hd_lineage_t lineage, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(secret != nullptr);
    KTH_PRECONDITION(chain_code != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const chain_code_cpp = kth::hash_to_cpp(chain_code);
    auto const lineage_cpp = kth::from_c_struct<kth::domain::wallet::hd_lineage>(lineage);
    auto result = cpp_t::from_secret(secret_cpp, chain_code_cpp, lineage_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_hd_public_mut_t kth_wallet_hd_public_from_verified_components(kth_ec_compressed_t const* point, kth_hash_t const* chain_code, kth_hd_lineage_t lineage) {
    KTH_PRECONDITION(point != nullptr);
    KTH_PRECONDITION(chain_code != nullptr);
    auto const point_cpp = kth::ec_compressed_to_cpp(point->data);
    auto const chain_code_cpp = kth::hash_to_cpp(chain_code->hash);
    auto const lineage_cpp = kth::from_c_struct<kth::domain::wallet::hd_lineage>(lineage);
    return kth::leak(cpp_t::from_verified_components(point_cpp, chain_code_cpp, lineage_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_from_verified_components_unsafe(uint8_t const* point, uint8_t const* chain_code, kth_hd_lineage_t lineage) {
    KTH_PRECONDITION(point != nullptr);
    KTH_PRECONDITION(chain_code != nullptr);
    auto const point_cpp = kth::ec_compressed_to_cpp(point);
    auto const chain_code_cpp = kth::hash_to_cpp(chain_code);
    auto const lineage_cpp = kth::from_c_struct<kth::domain::wallet::hd_lineage>(lineage);
    return kth::leak(cpp_t::from_verified_components(point_cpp, chain_code_cpp, lineage_cpp));
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

uint32_t kth_wallet_hd_public_fingerprint(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).fingerprint();
}


// Operations

kth_error_code_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).derive_public(index);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

void kth_wallet_hd_public_wipe(kth_hd_public_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).wipe();
}


// Static utilities

uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes) {
    return cpp_t::to_prefix(prefixes);
}

} // extern "C"
