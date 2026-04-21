// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

kth_ec_private_mut_t kth_wallet_ec_private_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_version(char const* wif, uint8_t version) {
    KTH_PRECONDITION(wif != nullptr);
    auto const wif_cpp = std::string(wif);
    return kth::leak_if_valid(cpp_t(wif_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version(kth_wif_compressed_t const* wif_compressed, uint8_t version) {
    KTH_PRECONDITION(wif_compressed != nullptr);
    auto wif_compressed_cpp = kth::wif_compressed_to_cpp(wif_compressed->data);
    kth::secure_scrub wif_compressed_cpp_scrub{&wif_compressed_cpp, sizeof(wif_compressed_cpp)};
    return kth::leak_if_valid(cpp_t(wif_compressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version_unsafe(uint8_t const* wif_compressed, uint8_t version) {
    KTH_PRECONDITION(wif_compressed != nullptr);
    auto wif_compressed_cpp = kth::wif_compressed_to_cpp(wif_compressed);
    kth::secure_scrub wif_compressed_cpp_scrub{&wif_compressed_cpp, sizeof(wif_compressed_cpp)};
    return kth::leak_if_valid(cpp_t(wif_compressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version(kth_wif_uncompressed_t const* wif_uncompressed, uint8_t version) {
    KTH_PRECONDITION(wif_uncompressed != nullptr);
    auto wif_uncompressed_cpp = kth::wif_uncompressed_to_cpp(wif_uncompressed->data);
    kth::secure_scrub wif_uncompressed_cpp_scrub{&wif_uncompressed_cpp, sizeof(wif_uncompressed_cpp)};
    return kth::leak_if_valid(cpp_t(wif_uncompressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version_unsafe(uint8_t const* wif_uncompressed, uint8_t version) {
    KTH_PRECONDITION(wif_uncompressed != nullptr);
    auto wif_uncompressed_cpp = kth::wif_uncompressed_to_cpp(wif_uncompressed);
    kth::secure_scrub wif_uncompressed_cpp_scrub{&wif_uncompressed_cpp, sizeof(wif_uncompressed_cpp)};
    return kth::leak_if_valid(cpp_t(wif_uncompressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress(kth_hash_t const* secret, uint16_t version, kth_bool_t compress) {
    KTH_PRECONDITION(secret != nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret->hash);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(secret_cpp, version, compress_cpp));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress) {
    KTH_PRECONDITION(secret != nullptr);
    auto secret_cpp = kth::hash_to_cpp(secret);
    kth::secure_scrub secret_cpp_scrub{&secret_cpp, sizeof(secret_cpp)};
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(secret_cpp, version, compress_cpp));
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


// Getters

kth_bool_t kth_wallet_ec_private_valid(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

char* kth_wallet_ec_private_encoded(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded();
    return kth::create_c_str(s);
}

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
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).to_public());
}

kth_payment_address_mut_t kth_wallet_ec_private_to_payment_address(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).to_payment_address());
}


// Operations

kth_bool_t kth_wallet_ec_private_less(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
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
