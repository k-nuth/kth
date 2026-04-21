// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/hd_public.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_public.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::hd_public;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_hd_public_mut_t kth_wallet_hd_public_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key(kth_hd_key_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key->data);
    return kth::leak_if_valid(cpp_t(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_unsafe(uint8_t const* public_key) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::leak_if_valid(cpp_t(public_key_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix(kth_hd_key_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key->data);
    return kth::leak_if_valid(cpp_t(public_key_cpp, prefix));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_public_key_prefix_unsafe(uint8_t const* public_key, uint32_t prefix) {
    KTH_PRECONDITION(public_key != nullptr);
    auto const public_key_cpp = kth::hd_key_to_cpp(public_key);
    return kth::leak_if_valid(cpp_t(public_key_cpp, prefix));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded(char const* encoded) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::leak_if_valid(cpp_t(encoded_cpp));
}

kth_hd_public_mut_t kth_wallet_hd_public_construct_from_encoded_prefix(char const* encoded, uint32_t prefix) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::leak_if_valid(cpp_t(encoded_cpp, prefix));
}


// Destructor

void kth_wallet_hd_public_destruct(kth_hd_public_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_hd_public_mut_t kth_wallet_hd_public_copy(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_hd_public_equals(kth_hd_public_const_t self, kth_hd_public_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Getters

kth_bool_t kth_wallet_hd_public_valid(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

char* kth_wallet_hd_public_encoded(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded();
    return kth::create_c_str(s);
}

kth_hash_t kth_wallet_hd_public_chain_code(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).chain_code());
}

kth_hd_lineage_t kth_wallet_hd_public_lineage(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_hd_lineage_t>(kth::cpp_ref<cpp_t>(self).lineage());
}

kth_ec_compressed_t kth_wallet_hd_public_point(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<cpp_t>(self).point());
}

kth_hd_key_t kth_wallet_hd_public_to_hd_key(kth_hd_public_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hd_key_t(kth::cpp_ref<cpp_t>(self).to_hd_key());
}


// Operations

kth_bool_t kth_wallet_hd_public_less(kth_hd_public_const_t self, kth_hd_public_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_hd_public_mut_t kth_wallet_hd_public_derive_public(kth_hd_public_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak_if_valid(kth::cpp_ref<cpp_t>(self).derive_public(index));
}


// Static utilities

uint32_t kth_wallet_hd_public_to_prefix(uint64_t prefixes) {
    return cpp_t::to_prefix(prefixes);
}

} // extern "C"
