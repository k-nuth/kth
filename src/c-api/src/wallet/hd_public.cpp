// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/hd_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_public.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_hd_public_mut_t kth_wallet_hd_public_construct_default(void) {
    return new kth::domain::wallet::hd_public();
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key(kth_hd_key_t public_key) {
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_unsafe(uint8_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix(kth_hd_key_t public_key, uint32_t prefix) {
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(public_key_cpp, prefix));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix_unsafe(uint8_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(public_key_cpp, prefix));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded(char const* encoded) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(encoded_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded_prefix(char const* encoded, uint32_t prefix) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_public(encoded_cpp, prefix));
}


// Destructor

void kth_wallet_hd_public_destruct(kth_hd_public_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::wallet::hd_public>(self);
}


// Copy

kth_hd_public_mut_t kth_wallet_hd_public_copy(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::hd_public(kth::cpp_ref<kth::domain::wallet::hd_public>(self));
}


// Equality

kth_bool_t kth_wallet_hd_public_equals(kth_hd_public_const_t self, kth_hd_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::wallet::hd_public>(self) == kth::cpp_ref<kth::domain::wallet::hd_public>(other));
}


// Getters

kth_bool_t kth_wallet_hd_public_valid(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<kth::domain::wallet::hd_public>(self)));
}

char* kth_wallet_hd_public_encoded(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<kth::domain::wallet::hd_public>(self).encoded();
    return kth::create_c_str(s);
}

kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::wallet::hd_public>(self).chain_code());
}

kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_hd_lineage_t>(kth::cpp_ref<kth::domain::wallet::hd_public>(self).lineage());
}

kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<kth::domain::wallet::hd_public>(self).point());
}

kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hd_key_t(kth::cpp_ref<kth::domain::wallet::hd_public>(self).to_hd_key());
}


// Operations

kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<kth::domain::wallet::hd_public>(x);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::wallet::hd_public>(self).operator<(x_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::domain::wallet::hd_public>(self).derive_public(index));
}


// Static utilities

uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes) {
    return kth::domain::wallet::hd_public::to_prefix(prefixes);
}

} // extern "C"
