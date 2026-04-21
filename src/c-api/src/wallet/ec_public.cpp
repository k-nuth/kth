// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>

#include <kth/capi/wallet/ec_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/ec_public.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::ec_public;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_ec_public_mut_t kth_wallet_ec_public_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_ec_private(kth_ec_private_const_t secret) {
    KTH_PRECONDITION(secret != nullptr);
    auto const& secret_cpp = kth::cpp_ref<kth::domain::wallet::ec_private>(secret);
    return kth::leak_if_valid(cpp_t(secret_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_decoded(uint8_t const* decoded, kth_size_t n) {
    KTH_PRECONDITION(decoded != nullptr || n == 0);
    auto const decoded_cpp = n != 0 ? kth::data_chunk(decoded, decoded + n) : kth::data_chunk{};
    return kth::leak_if_valid(cpp_t(decoded_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_base16(char const* base16) {
    KTH_PRECONDITION(base16 != nullptr);
    auto const base16_cpp = std::string(base16);
    return kth::leak_if_valid(cpp_t(base16_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress(kth_ec_compressed_t const* compressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(compressed_point != nullptr);
    auto const compressed_point_cpp = kth::ec_compressed_to_cpp(compressed_point->data);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(compressed_point_cpp, compress_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_compressed_point_compress_unsafe(uint8_t const* compressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(compressed_point != nullptr);
    auto const compressed_point_cpp = kth::ec_compressed_to_cpp(compressed_point);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(compressed_point_cpp, compress_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress(kth_ec_uncompressed_t const* uncompressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(uncompressed_point != nullptr);
    auto const uncompressed_point_cpp = kth::ec_uncompressed_to_cpp(uncompressed_point->data);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(uncompressed_point_cpp, compress_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_construct_from_uncompressed_point_compress_unsafe(uint8_t const* uncompressed_point, kth_bool_t compress) {
    KTH_PRECONDITION(uncompressed_point != nullptr);
    auto const uncompressed_point_cpp = kth::ec_uncompressed_to_cpp(uncompressed_point);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak_if_valid(cpp_t(uncompressed_point_cpp, compress_cpp));
}


// Destructor

void kth_wallet_ec_public_destruct(kth_ec_public_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_ec_public_mut_t kth_wallet_ec_public_copy(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_ec_public_equals(kth_ec_public_const_t self, kth_ec_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

kth_error_code_t kth_wallet_ec_public_to_data(kth_ec_public_const_t self, KTH_OUT_OWNED uint8_t** out, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const result = kth::cpp_ref<cpp_t>(self).to_data();
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::create_c_array(*result, *out_size);
    return kth_ec_success;
}


// Getters

kth_bool_t kth_wallet_ec_public_valid(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

char* kth_wallet_ec_public_encoded(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded();
    return kth::create_c_str(s);
}

kth_ec_compressed_t kth_wallet_ec_public_point(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).point());
}

kth_bool_t kth_wallet_ec_public_compressed(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).compressed());
}

kth_error_code_t kth_wallet_ec_public_to_uncompressed(kth_ec_public_const_t self, uint8_t* out, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(n >= 65);
    auto const result = kth::cpp_ref<cpp_t>(self).to_uncompressed();
    if ( ! result) return kth::to_c_err(result.error());
    std::memcpy(out, result->data(), result->size());
    return kth_ec_success;
}


// Operations

kth_bool_t kth_wallet_ec_public_less(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_payment_address_mut_t kth_wallet_ec_public_to_payment_address(kth_ec_public_const_t self, uint8_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).to_payment_address(version));
}

} // extern "C"
