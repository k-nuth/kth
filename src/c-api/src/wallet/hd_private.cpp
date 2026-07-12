// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/hd_private.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_private.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::hd_private;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_hd_private_parse_from(char const* encoded, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from(encoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_parse_from_with_public_prefix(char const* encoded, uint32_t public_prefix, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from_with_public_prefix(encoded_cpp, public_prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_parse_from_with_prefixes(char const* encoded, uint64_t prefixes, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from_with_prefixes(encoded_cpp, prefixes);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_seed(uint8_t const* seed, kth_size_t n, uint64_t prefixes, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(seed != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const seed_cpp = n != 0 ? kth::data_chunk(seed, seed + n) : kth::data_chunk{};
    auto result = cpp_t::from_seed(seed_cpp, prefixes);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key(kth_hd_key_t const* private_key, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key->data);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key(private_key_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key_unsafe(uint8_t const* private_key, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key(private_key_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key_with_public_prefix(kth_hd_key_t const* private_key, uint32_t public_prefix, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key->data);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key_with_public_prefix(private_key_cpp, public_prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key_with_public_prefix_unsafe(uint8_t const* private_key, uint32_t public_prefix, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key_with_public_prefix(private_key_cpp, public_prefix);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key_with_prefixes(kth_hd_key_t const* private_key, uint64_t prefixes, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key->data);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key_with_prefixes(private_key_cpp, prefixes);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_from_hd_key_with_prefixes_unsafe(uint8_t const* private_key, uint64_t prefixes, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(private_key != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto private_key_cpp = kth::hd_key_to_cpp(private_key);
    kth::secure_scrub private_key_cpp_scrub{&private_key_cpp, sizeof(private_key_cpp)};
    auto result = cpp_t::from_hd_key_with_prefixes(private_key_cpp, prefixes);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_wallet_hd_private_destruct(kth_hd_private_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_hd_private_mut_t kth_wallet_hd_private_copy(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_hd_private_equals(kth_hd_private_const_t self, kth_hd_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_hd_private_not_equal(kth_hd_private_const_t self, kth_hd_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_hd_private_less(kth_hd_private_const_t self, kth_hd_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_private_greater(kth_hd_private_const_t self, kth_hd_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_private_less_or_equal(kth_hd_private_const_t self, kth_hd_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_hd_private_greater_or_equal(kth_hd_private_const_t self, kth_hd_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

kth_hash_t kth_wallet_hd_private_secret(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).secret());
}

kth_hd_key_t kth_wallet_hd_private_to_hd_key(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hd_key_t(kth::cpp_ref<cpp_t>(self).to_hd_key());
}

kth_hd_public_mut_t kth_wallet_hd_private_to_public(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak(kth::cpp_ref<cpp_t>(self).to_public());
}

kth_hash_t kth_wallet_hd_private_chain_code(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).chain_code());
}

kth_hd_lineage_t kth_wallet_hd_private_lineage(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_hd_lineage_t>(kth::cpp_ref<cpp_t>(self).lineage());
}

kth_ec_compressed_t kth_wallet_hd_private_point(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).point());
}

char* kth_wallet_hd_private_to_string(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}

kth_hd_public_const_t kth_wallet_hd_private_public_key(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).public_key());
}


// Operations

kth_error_code_t kth_wallet_hd_private_derive_private(kth_hd_private_const_t self, uint32_t index, KTH_OUT_OWNED kth_hd_private_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).derive_private(index);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_hd_private_derive_public(kth_hd_private_const_t self, uint32_t index, KTH_OUT_OWNED kth_hd_public_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).derive_public(index);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

void kth_wallet_hd_private_wipe(kth_hd_private_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).wipe();
}


// Static utilities

uint32_t kth_wallet_hd_private_to_prefix(uint64_t prefixes) {
    return cpp_t::to_prefix(prefixes);
}

} // extern "C"
