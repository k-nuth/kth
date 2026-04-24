// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_bitcoin_uri_mut_t kth_wallet_bitcoin_uri_construct(char const* uri, kth_bool_t strict) {
    KTH_PRECONDITION(uri != nullptr);
    auto const uri_cpp = std::string(uri);
    auto const strict_cpp = kth::int_to_bool(strict);
    return kth::leak_if_valid(cpp_t(uri_cpp, strict_cpp));
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


// Getters

kth_bool_t kth_wallet_bitcoin_uri_valid(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

char* kth_wallet_bitcoin_uri_encoded(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded();
    return kth::create_c_str(s);
}

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

kth_payment_address_mut_t kth_wallet_bitcoin_uri_payment(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).payment());
}

kth_stealth_address_mut_t kth_wallet_bitcoin_uri_stealth(kth_bitcoin_uri_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).stealth());
}


// Setters

void kth_wallet_bitcoin_uri_set_amount(kth_bitcoin_uri_mut_t self, uint64_t satoshis) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_amount(satoshis);
}

void kth_wallet_bitcoin_uri_set_label(kth_bitcoin_uri_mut_t self, char const* label) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(label != nullptr);
    auto const label_cpp = std::string(label);
    kth::cpp_ref<cpp_t>(self).set_label(label_cpp);
}

void kth_wallet_bitcoin_uri_set_message(kth_bitcoin_uri_mut_t self, char const* message) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(message != nullptr);
    auto const message_cpp = std::string(message);
    kth::cpp_ref<cpp_t>(self).set_message(message_cpp);
}

void kth_wallet_bitcoin_uri_set_r(kth_bitcoin_uri_mut_t self, char const* r) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(r != nullptr);
    auto const r_cpp = std::string(r);
    kth::cpp_ref<cpp_t>(self).set_r(r_cpp);
}

kth_bool_t kth_wallet_bitcoin_uri_set_address_string(kth_bitcoin_uri_mut_t self, char const* address) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(address != nullptr);
    auto const address_cpp = std::string(address);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_address(address_cpp));
}

void kth_wallet_bitcoin_uri_set_address_payment_address(kth_bitcoin_uri_mut_t self, kth_payment_address_const_t payment) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(payment != nullptr);
    auto const& payment_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(payment);
    kth::cpp_ref<cpp_t>(self).set_address(payment_cpp);
}

void kth_wallet_bitcoin_uri_set_address_stealth_address(kth_bitcoin_uri_mut_t self, kth_stealth_address_const_t stealth) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(stealth != nullptr);
    auto const& stealth_cpp = kth::cpp_ref<kth::domain::wallet::stealth_address>(stealth);
    kth::cpp_ref<cpp_t>(self).set_address(stealth_cpp);
}

void kth_wallet_bitcoin_uri_set_strict(kth_bitcoin_uri_mut_t self, kth_bool_t strict) {
    KTH_PRECONDITION(self != nullptr);
    auto const strict_cpp = kth::int_to_bool(strict);
    kth::cpp_ref<cpp_t>(self).set_strict(strict_cpp);
}

kth_bool_t kth_wallet_bitcoin_uri_set_scheme(kth_bitcoin_uri_mut_t self, char const* scheme) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(scheme != nullptr);
    auto const scheme_cpp = std::string(scheme);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_scheme(scheme_cpp));
}

kth_bool_t kth_wallet_bitcoin_uri_set_authority(kth_bitcoin_uri_mut_t self, char const* authority) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(authority != nullptr);
    auto const authority_cpp = std::string(authority);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_authority(authority_cpp));
}

kth_bool_t kth_wallet_bitcoin_uri_set_path(kth_bitcoin_uri_mut_t self, char const* path) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(path != nullptr);
    auto const path_cpp = std::string(path);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_path(path_cpp));
}

kth_bool_t kth_wallet_bitcoin_uri_set_fragment(kth_bitcoin_uri_mut_t self, char const* fragment) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(fragment != nullptr);
    auto const fragment_cpp = std::string(fragment);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_fragment(fragment_cpp));
}

kth_bool_t kth_wallet_bitcoin_uri_set_parameter(kth_bitcoin_uri_mut_t self, char const* key, char const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(key != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const key_cpp = std::string(key);
    auto const value_cpp = std::string(value);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_parameter(key_cpp, value_cpp));
}


// Operations

kth_bool_t kth_wallet_bitcoin_uri_less(kth_bitcoin_uri_const_t self, kth_bitcoin_uri_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

char* kth_wallet_bitcoin_uri_parameter(kth_bitcoin_uri_const_t self, char const* key) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(key != nullptr);
    auto const key_cpp = std::string(key);
    auto const s = kth::cpp_ref<cpp_t>(self).parameter(key_cpp);
    return kth::create_c_str(s);
}

} // extern "C"
