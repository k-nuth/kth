// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>

#include <kth/capi/wallet/ec_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/ec_public.hpp>

// Conversion functions
kth::domain::wallet::ec_public& kth_wallet_ec_public_mut_cpp(kth_ec_public_mut_t o) {
    return *static_cast<kth::domain::wallet::ec_public*>(o);
}
kth::domain::wallet::ec_public const& kth_wallet_ec_public_const_cpp(kth_ec_public_const_t o) {
    return *static_cast<kth::domain::wallet::ec_public const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_ec_public_mut_t kth_wallet_ec_public_construct_default(void) {
    return new kth::domain::wallet::ec_public();
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_ec_private(kth_ec_private_const_t secret) {
    KTH_PRECONDITION(secret != nullptr);
    auto const& secret_cpp = kth_wallet_ec_private_const_cpp(secret);
    return new kth::domain::wallet::ec_public(secret_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_decoded(uint8_t const* decoded, kth_size_t n) {
    KTH_PRECONDITION(decoded != nullptr || n == 0);
    auto const decoded_cpp = n != 0 ? kth::data_chunk(decoded, decoded + n) : kth::data_chunk{};
    return new kth::domain::wallet::ec_public(decoded_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_base16(char const* base16) {
    KTH_PRECONDITION(base16 != nullptr);
    auto const base16_cpp = std::string(base16);
    return new kth::domain::wallet::ec_public(base16_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress(kth_ec_compressed_t compressed_point, kth_bool_t compress) {
    auto const compressed_point_cpp = kth::ec_compressed_to_cpp(compressed_point.data);
    auto const compress_cpp = kth::int_to_bool(compress);
    return new kth::domain::wallet::ec_public(compressed_point_cpp, compress_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress_unsafe(uint8_t const* compressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(compressed_point != nullptr);
    auto const compressed_point_cpp = kth::ec_compressed_to_cpp(compressed_point);
    auto const compress_cpp = kth::int_to_bool(compress);
    return new kth::domain::wallet::ec_public(compressed_point_cpp, compress_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress(kth_ec_uncompressed_t uncompressed_point, kth_bool_t compress) {
    auto const uncompressed_point_cpp = kth::ec_uncompressed_to_cpp(uncompressed_point.data);
    auto const compress_cpp = kth::int_to_bool(compress);
    return new kth::domain::wallet::ec_public(uncompressed_point_cpp, compress_cpp);
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress_unsafe(uint8_t const* uncompressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(uncompressed_point != nullptr);
    auto const uncompressed_point_cpp = kth::ec_uncompressed_to_cpp(uncompressed_point);
    auto const compress_cpp = kth::int_to_bool(compress);
    return new kth::domain::wallet::ec_public(uncompressed_point_cpp, compress_cpp);
}


// Destructor

void kth_wallet_ec_public_destruct(kth_ec_public_mut_t self) {
    if (self == nullptr) return;
    delete &kth_wallet_ec_public_mut_cpp(self);
}


// Copy

kth_ec_public_mut_t kth_wallet_ec_public_copy(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::ec_public(kth_wallet_ec_public_const_cpp(self));
}


// Equality

kth_bool_t kth_wallet_ec_public_equals(kth_ec_public_const_t self, kth_ec_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_wallet_ec_public_const_cpp(self) == kth_wallet_ec_public_const_cpp(other));
}


// Serialization

uint8_t* kth_wallet_ec_public_to_data(kth_ec_public_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    kth::data_chunk data;
    if ( ! kth_wallet_ec_public_const_cpp(self).to_data(data)) {
        *out_size = 0;
        return nullptr;
    }
    return kth::create_c_array(data, *out_size);
}


// Getters

char* kth_wallet_ec_public_encoded(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_wallet_ec_public_const_cpp(self).encoded();
    return kth::create_c_str(s);
}

kth_ec_compressed_t kth_wallet_ec_public_point(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_ec_public_const_cpp(self).point();
    return kth::to_ec_compressed_t(value_cpp);
}

uint16_t kth_wallet_ec_public_version(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_public_const_cpp(self).version();
}

uint8_t kth_wallet_ec_public_payment_version(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_public_const_cpp(self).payment_version();
}

uint8_t kth_wallet_ec_public_wif_version(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_ec_public_const_cpp(self).wif_version();
}

kth_bool_t kth_wallet_ec_public_compressed(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_wallet_ec_public_const_cpp(self).compressed());
}


// Operations

kth_bool_t kth_wallet_ec_public_less(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth_wallet_ec_public_const_cpp(x);
    return kth::bool_to_int(kth_wallet_ec_public_const_cpp(self).operator<(x_cpp));
}

kth_bool_t kth_wallet_ec_public_to_uncompressed(kth_ec_public_const_t self, uint8_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    std::array<uint8_t, 65> out_cpp;
    auto const cpp_result = kth_wallet_ec_public_const_cpp(self).to_uncompressed(out_cpp);
    if (cpp_result) {
        std::memcpy(out, out_cpp.data(), out_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_payment_address_mut_t kth_wallet_ec_public_to_payment_address(kth_ec_public_const_t self, uint8_t version) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::payment_address(kth_wallet_ec_public_const_cpp(self).to_payment_address(version));
}

} // extern "C"
