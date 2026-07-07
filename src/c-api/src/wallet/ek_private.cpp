// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>
#include <utility>

#include <kth/capi/wallet/ek_private.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/ek_private.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::ek_private;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_ek_private_mut_t kth_wallet_ek_private_construct(kth_encrypted_private_t const* value) {
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::encrypted_private_to_cpp(value->data);
    return kth::leak_if_valid(cpp_t(value_cpp));
}

kth_ek_private_mut_t kth_wallet_ek_private_construct_unsafe(uint8_t const* value) {
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::encrypted_private_to_cpp(value);
    return kth::leak_if_valid(cpp_t(value_cpp));
}

kth_error_code_t kth_wallet_ek_private_parse_from(char const* encoded, KTH_OUT_OWNED kth_ek_private_mut_t* out) {
    KTH_PRECONDITION(encoded != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const encoded_cpp = std::string_view(encoded);
    auto result = cpp_t::parse_from(encoded_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_wallet_ek_private_destruct(kth_ek_private_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_ek_private_mut_t kth_wallet_ek_private_copy(kth_ek_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_ek_private_equals(kth_ek_private_const_t self, kth_ek_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_wallet_ek_private_not_equal(kth_ek_private_const_t self, kth_ek_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_wallet_ek_private_less(kth_ek_private_const_t self, kth_ek_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ek_private_greater(kth_ek_private_const_t self, kth_ek_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ek_private_less_or_equal(kth_ek_private_const_t self, kth_ek_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_wallet_ek_private_greater_or_equal(kth_ek_private_const_t self, kth_ek_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

kth_bool_t kth_wallet_ek_private_valid(kth_ek_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).valid());
}

kth_encrypted_private_t kth_wallet_ek_private_private_key(kth_ek_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_encrypted_private_t(kth::cpp_ref<cpp_t>(self).private_key());
}

kth_encrypted_private_t kth_wallet_ek_private_value(kth_ek_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_encrypted_private_t(kth::cpp_ref<cpp_t>(self).value());
}

char* kth_wallet_ek_private_to_string(kth_ek_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}

} // extern "C"
