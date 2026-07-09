// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/ec_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/wallet/ec_public.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::ec_public;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_ec_public_construct_from_data(uint8_t const* decoded, kth_size_t n, KTH_OUT_OWNED kth_ec_public_mut_t* out) {
    KTH_PRECONDITION(decoded != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const decoded_cpp = n != 0 ? kth::data_chunk(decoded, decoded + n) : kth::data_chunk{};
    auto result = cpp_t::from_data(decoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_public_parse_from(char const* base16, KTH_OUT_OWNED kth_ec_public_mut_t* out) {
    KTH_PRECONDITION(base16 != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const base16_cpp = std::string_view(base16);
    auto result = cpp_t::parse_from(base16_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_ec_public_mut_t kth_wallet_ec_public_from_private(kth_ec_private_const_t secret) {
    KTH_PRECONDITION(secret != nullptr);
    auto const& secret_cpp = kth::cpp_ref<kth::domain::wallet::ec_private>(secret);
    return kth::leak(cpp_t::from_private(secret_cpp));
}

kth_error_code_t kth_wallet_ec_public_from_point(kth_ec_uncompressed_t const* point, kth_bool_t compress, KTH_OUT_OWNED kth_ec_public_mut_t* out) {
    KTH_PRECONDITION(point != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const point_cpp = kth::ec_uncompressed_to_cpp(point->data);
    auto const compress_cpp = kth::int_to_bool(compress);
    auto result = cpp_t::from_point(point_cpp, compress_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_ec_public_from_point_unsafe(uint8_t const* point, kth_bool_t compress, KTH_OUT_OWNED kth_ec_public_mut_t* out) {
    KTH_PRECONDITION(point != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const point_cpp = kth::ec_uncompressed_to_cpp(point);
    auto const compress_cpp = kth::int_to_bool(compress);
    auto result = cpp_t::from_point(point_cpp, compress_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_ec_public_mut_t kth_wallet_ec_public_from_verified_point(kth_ec_compressed_t const* point, kth_bool_t compress) {
    KTH_PRECONDITION(point != nullptr);
    auto const point_cpp = kth::ec_compressed_to_cpp(point->data);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak(cpp_t::from_verified_point(point_cpp, compress_cpp));
}

kth_ec_public_mut_t kth_wallet_ec_public_from_verified_point_unsafe(uint8_t const* point, kth_bool_t compress) {
    KTH_PRECONDITION(point != nullptr);
    auto const point_cpp = kth::ec_compressed_to_cpp(point);
    auto const compress_cpp = kth::int_to_bool(compress);
    return kth::leak(cpp_t::from_verified_point(point_cpp, compress_cpp));
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

kth_bool_t kth_wallet_ec_public_not_equal(kth_ec_public_const_t self, kth_ec_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_ec_public_less(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_public_greater(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_public_less_or_equal(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ec_public_greater_or_equal(kth_ec_public_const_t self, kth_ec_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Serialization

uint8_t* kth_wallet_ec_public_to_data(kth_ec_public_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data();
    return kth::create_c_array(data, *out_size);
}


// Getters

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

kth_ec_uncompressed_t kth_wallet_ec_public_to_uncompressed(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_uncompressed_t(kth::cpp_ref<cpp_t>(self).to_uncompressed());
}

kth_ec_compressed_t kth_wallet_ec_public_value(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).value());
}

char* kth_wallet_ec_public_to_string(kth_ec_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}


// Operations

kth_error_code_t kth_wallet_ec_public_to_payment_address(kth_ec_public_const_t self, uint8_t version, KTH_OUT_OWNED kth_payment_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).to_payment_address(version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

} // extern "C"
