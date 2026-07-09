// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/ec_private.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/ec_private.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::ec_private;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_ec_private_parse_from(char const* wif, uint8_t version, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(wif != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const wif_cpp = std::string_view(wif);
    auto result = cpp_t::parse_from(wif_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_compressed(kth_wif_compressed_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(wif != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto wif_cpp = kth::wif_compressed_to_cpp(wif->data);
    kth::secure_scrub wif_cpp_scrub{&wif_cpp, sizeof(wif_cpp)};
    auto result = cpp_t::from_compressed(wif_cpp, address_version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_compressed_unsafe(uint8_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(wif != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto wif_cpp = kth::wif_compressed_to_cpp(wif);
    kth::secure_scrub wif_cpp_scrub{&wif_cpp, sizeof(wif_cpp)};
    auto result = cpp_t::from_compressed(wif_cpp, address_version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_uncompressed(kth_wif_uncompressed_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(wif != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto wif_cpp = kth::wif_uncompressed_to_cpp(wif->data);
    kth::secure_scrub wif_cpp_scrub{&wif_cpp, sizeof(wif_cpp)};
    auto result = cpp_t::from_uncompressed(wif_cpp, address_version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_uncompressed_unsafe(uint8_t const* wif, uint8_t address_version, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(wif != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto wif_cpp = kth::wif_uncompressed_to_cpp(wif);
    kth::secure_scrub wif_cpp_scrub{&wif_cpp, sizeof(wif_cpp)};
    auto result = cpp_t::from_uncompressed(wif_cpp, address_version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_secret(kth_hash_t const* secret, uint16_t version, kth_bool_t compress, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(secret != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret->hash);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    auto result = cpp_t::from_secret(secret_cpp, version, compress_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_private_from_secret_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress, KTH_OUT_OWNED kth_ec_private_mut_t* out) {
    KTH_PRECONDITION(secret != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    auto result = cpp_t::from_secret(secret_cpp, version, compress_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_ec_private_mut_t kth_wallet_ec_private_from_verified_secret(kth_hash_t const* secret, uint16_t version, kth_bool_t compress) {
    KTH_PRECONDITION(secret != nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret->hash);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak(cpp_t::from_verified_secret(secret_cpp, version, compress_cpp));
}

kth_ec_private_mut_t kth_wallet_ec_private_from_verified_secret_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress) {
    KTH_PRECONDITION(secret != nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak(cpp_t::from_verified_secret(secret_cpp, version, compress_cpp));
}


// Destructor

void kth_wallet_ec_private_destruct(kth_ec_private_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_ec_private_mut_t kth_wallet_ec_private_copy(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_ec_private_equals(kth_ec_private_const_t self, kth_ec_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_ec_private_not_equal(kth_ec_private_const_t self, kth_ec_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_ec_private_less(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_private_greater(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_private_less_or_equal(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_private_greater_or_equal(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

kth_hash_t kth_wallet_ec_private_secret(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).secret());
}

uint16_t kth_wallet_ec_private_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).version();
}

uint8_t kth_wallet_ec_private_payment_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).payment_version();
}

uint8_t kth_wallet_ec_private_wif_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).wif_version();
}

kth_bool_t kth_wallet_ec_private_compressed(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).compressed());
}

kth_ec_public_mut_t kth_wallet_ec_private_to_public(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak(kth::cpp_ref<cpp_t>(self).to_public());
}

kth_error_code_t kth_wallet_ec_private_to_payment_address(kth_ec_private_const_t self, KTH_OUT_OWNED kth_payment_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).to_payment_address();
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_hash_t kth_wallet_ec_private_value(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).value());
}

char* kth_wallet_ec_private_to_string(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}


// Static utilities

uint8_t kth_wallet_ec_private_to_address_prefix(uint16_t version) {
    return cpp_t::to_address_prefix(version);
}

uint8_t kth_wallet_ec_private_to_wif_prefix(uint16_t version) {
    return cpp_t::to_wif_prefix(version);
}

uint16_t kth_wallet_ec_private_to_version(uint8_t address, uint8_t wif) {
    return cpp_t::to_version(address, wif);
}

} // extern "C"
