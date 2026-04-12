// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/hd_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_public.hpp>

// Conversion functions
kth::domain::wallet::hd_public& kth_wallet_hd_public_mut_cpp(kth_hd_public_mut_t o) {
    return *static_cast<kth::domain::wallet::hd_public*>(o);
}
kth::domain::wallet::hd_public const& kth_wallet_hd_public_const_cpp(kth_hd_public_const_t o) {
    return *static_cast<kth::domain::wallet::hd_public const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_hd_public_mut_t kth_wallet_hd_public_construct_default(void) {
    return new kth::domain::wallet::hd_public();
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key(kth_hd_key_t public_key) {
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key.data);
    auto* obj = new kth::domain::wallet::hd_public(public_key_cpp);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_unsafe(uint8_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    auto* obj = new kth::domain::wallet::hd_public(public_key_cpp);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix(kth_hd_key_t public_key, uint32_t prefix) {
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key.data);
    auto* obj = new kth::domain::wallet::hd_public(public_key_cpp, prefix);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix_unsafe(uint8_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    auto* obj = new kth::domain::wallet::hd_public(public_key_cpp, prefix);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded(char const* encoded) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    auto* obj = new kth::domain::wallet::hd_public(encoded_cpp);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded_prefix(char const* encoded, uint32_t prefix) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    auto* obj = new kth::domain::wallet::hd_public(encoded_cpp, prefix);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}


// Destructor

void kth_wallet_hd_public_destruct(kth_hd_public_mut_t self) {
    if (self == nullptr) return;
    delete &kth_wallet_hd_public_mut_cpp(self);
}


// Copy

kth_hd_public_mut_t kth_wallet_hd_public_copy(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::hd_public(kth_wallet_hd_public_const_cpp(self));
}


// Equality

kth_bool_t kth_wallet_hd_public_equals(kth_hd_public_const_t self, kth_hd_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_wallet_hd_public_const_cpp(self) == kth_wallet_hd_public_const_cpp(other));
}


// Getters

kth_bool_t kth_wallet_hd_public_valid(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_wallet_hd_public_const_cpp(self).operator const bool());
}

char* kth_wallet_hd_public_encoded(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_wallet_hd_public_const_cpp(self).encoded();
    return kth::create_c_str(s);
}

kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_hd_public_const_cpp(self).chain_code();
    return kth::to_hash_t(value_cpp);
}

kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_hd_public_const_cpp(self).lineage();
    return kth::to_c_struct<kth_hd_lineage_t>(value_cpp);
}

kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_hd_public_const_cpp(self).point();
    return kth::to_ec_compressed_t(value_cpp);
}

kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_hd_public_const_cpp(self).to_hd_key();
    return kth::to_hd_key_t(value_cpp);
}


// Operations

kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth_wallet_hd_public_const_cpp(x);
    return kth::bool_to_int(kth_wallet_hd_public_const_cpp(self).operator<(x_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    auto* obj = new kth::domain::wallet::hd_public(kth_wallet_hd_public_const_cpp(self).derive_public(index));
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}


// Static utilities

uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes) {
    return kth::domain::wallet::hd_public::to_prefix(prefixes);
}

} // extern "C"
