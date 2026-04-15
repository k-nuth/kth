// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/ec_private.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/ec_private.hpp>

// Conversion functions
kth::domain::wallet::ec_private& kth_wallet_ec_private_mut_cpp(kth_ec_private_mut_t o) {
    return *static_cast<kth::domain::wallet::ec_private*>(o);
}
kth::domain::wallet::ec_private const& kth_wallet_ec_private_const_cpp(kth_ec_private_const_t o) {
    return *static_cast<kth::domain::wallet::ec_private const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_ec_private_mut_t kth_wallet_ec_private_construct_default(void) {
    return new kth::domain::wallet::ec_private();
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_version(char const* wif, uint8_t version) {
    KTH_PRECONDITION(wif != nullptr);
    auto const wif_cpp = std::string(wif);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(wif_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version(kth_wif_compressed_t wif_compressed, uint8_t version) {
    auto const wif_compressed_cpp = kth::wif_compressed_to_cpp(wif_compressed.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(wif_compressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_compressed_version_unsafe(uint8_t const* wif_compressed, uint8_t version) {
    KTH_PRECONDITION(wif_compressed != nullptr);
    auto const wif_compressed_cpp = kth::wif_compressed_to_cpp(wif_compressed);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(wif_compressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version(kth_wif_uncompressed_t wif_uncompressed, uint8_t version) {
    auto const wif_uncompressed_cpp = kth::wif_uncompressed_to_cpp(wif_uncompressed.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(wif_uncompressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_wif_uncompressed_version_unsafe(uint8_t const* wif_uncompressed, uint8_t version) {
    KTH_PRECONDITION(wif_uncompressed != nullptr);
    auto const wif_uncompressed_cpp = kth::wif_uncompressed_to_cpp(wif_uncompressed);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(wif_uncompressed_cpp, version));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress(kth_hash_t secret, uint16_t version, kth_bool_t compress) {
    auto const secret_cpp = kth::hash_to_cpp(secret.hash);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(secret_cpp, version, compress_cpp));
}

kth_ec_private_mut_t kth_wallet_ec_private_construct_from_secret_version_compress_unsafe(uint8_t const* secret, uint16_t version, kth_bool_t compress) {
    KTH_PRECONDITION(secret != nullptr);
    auto const secret_cpp = kth::hash_to_cpp(secret);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::make_leaked_if_valid(kth::domain::wallet::ec_private(secret_cpp, version, compress_cpp));
}


// Destructor

void kth_wallet_ec_private_destruct(kth_ec_private_mut_t self) {
    if (self == nullptr) return;
    delete &kth_wallet_ec_private_mut_cpp(self);
}


// Copy

kth_ec_private_mut_t kth_wallet_ec_private_copy(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::ec_private(kth_wallet_ec_private_const_cpp(self));
}


// Equality

kth_bool_t kth_wallet_ec_private_equals(kth_ec_private_const_t self, kth_ec_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_wallet_ec_private_const_cpp(self) == kth_wallet_ec_private_const_cpp(other));
}


// Getters

kth_bool_t kth_wallet_ec_private_valid(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_wallet_ec_private_const_cpp(self).operator bool());
}

char* kth_wallet_ec_private_encoded(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_wallet_ec_private_const_cpp(self).encoded();
    return kth::create_c_str(s);
}

kth_hash_t kth_wallet_ec_private_secret(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth_wallet_ec_private_const_cpp(self).secret());
}

uint16_t kth_wallet_ec_private_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_private_const_cpp(self).version();
}

uint8_t kth_wallet_ec_private_payment_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_private_const_cpp(self).payment_version();
}

uint8_t kth_wallet_ec_private_wif_version(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_private_const_cpp(self).wif_version();
}

kth_bool_t kth_wallet_ec_private_compressed(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_wallet_ec_private_const_cpp(self).compressed());
}

kth_ec_public_mut_t kth_wallet_ec_private_to_public(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth_wallet_ec_private_const_cpp(self).to_public());
}

kth_payment_address_mut_t kth_wallet_ec_private_to_payment_address(kth_ec_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth_wallet_ec_private_const_cpp(self).to_payment_address());
}


// Operations

kth_bool_t kth_wallet_ec_private_less(kth_ec_private_const_t self, kth_ec_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth_wallet_ec_private_const_cpp(x);
    return kth::bool_to_int(kth_wallet_ec_private_const_cpp(self).operator<(x_cpp));
}


// Static utilities

uint8_t kth_wallet_ec_private_to_address_prefix(uint16_t version) {
    return kth::domain::wallet::ec_private::to_address_prefix(version);
}

uint8_t kth_wallet_ec_private_to_wif_prefix(uint16_t version) {
    return kth::domain::wallet::ec_private::to_wif_prefix(version);
}

uint16_t kth_wallet_ec_private_to_version(uint8_t address, uint8_t wif) {
    return kth::domain::wallet::ec_private::to_version(address, wif);
}

} // extern "C"
