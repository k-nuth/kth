// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/bitcoin_uri.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/bitcoin_uri.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::bitcoin_uri;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_bitcoin_uri_parse_from(char const* uri, kth_bool_t strict, KTH_OUT_OWNED kth_bitcoin_uri_mut_t* out) {
    KTH_PRECONDITION(uri != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const uri_cpp = std::string_view(uri);
    auto const strict_cpp = kth::int_to_bool(strict);
    auto result = cpp_t::parse_from(uri_cpp, strict_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_wallet_bitcoin_uri_destruct(kth_bitcoin_uri_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_copy(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_bitcoin_uri_equals(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_bitcoin_uri_not_equal(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Getters

uint64_t kth_wallet_bitcoin_uri_amount(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).amount();
}

char* kth_wallet_bitcoin_uri_label(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).label();
    return kth::create_c_str(s);
}

char* kth_wallet_bitcoin_uri_message(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).message();
    return kth::create_c_str(s);
}

char* kth_wallet_bitcoin_uri_r(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).r();
    return kth::create_c_str(s);
}

char* kth_wallet_bitcoin_uri_address(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).address();
    return kth::create_c_str(s);
}

kth_error_code_t kth_wallet_bitcoin_uri_payment(kth_bitcoin_uri_const_t self, KTH_OUT_OWNED kth_payment_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).payment();
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_bitcoin_uri_stealth(kth_bitcoin_uri_const_t self, KTH_OUT_OWNED kth_stealth_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = kth::cpp_ref<cpp_t>(self).stealth();
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

char* kth_wallet_bitcoin_uri_to_string(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}


// Operations

char* kth_wallet_bitcoin_uri_parameter(kth_bitcoin_uri_const_t self, char const* key) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(key != nullptr);
    auto const key_cpp = std::string(key);
    auto const s = kth::cpp_ref<cpp_t>(self).parameter(key_cpp);
    return kth::create_c_str(s);
}

} // extern "C"
