// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/hd_private.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/hd_private.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_hd_private_mut_t kth_wallet_hd_private_construct_default(void) {
    return new kth::domain::wallet::hd_private();
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_seed_prefixes(uint8_t const* seed, kth_size_t n, uint64_t prefixes) {
    KTH_PRECONDITION(seed != nullptr || n == 0);
    auto const seed_cpp = n != 0 ? kth::data_chunk(seed, seed + n) : kth::data_chunk{};
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(seed_cpp, prefixes));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key(kth_hd_key_t private_key) {
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_unsafe(uint8_t const* private_key) {
    KTH_PRECONDITION(private_key != nullptr);
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefixes(kth_hd_key_t private_key, uint64_t prefixes) {
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp, prefixes));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefixes_unsafe(uint8_t const* private_key, uint64_t prefixes) {
    KTH_PRECONDITION(private_key != nullptr);
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp, prefixes));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefix(kth_hd_key_t private_key, uint32_t prefix) {
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key.data);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp, prefix));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_private_key_prefix_unsafe(uint8_t const* private_key, uint32_t prefix) {
    KTH_PRECONDITION(private_key != nullptr);
    auto const private_key_cpp = kth::hd_key_to_cpp(private_key);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(private_key_cpp, prefix));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded(char const* encoded) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(encoded_cpp));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded_prefixes(char const* encoded, uint64_t prefixes) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(encoded_cpp, prefixes));
}

kth_hd_private_mut_t kth_wallet_hd_private_construct_from_encoded_prefix(char const* encoded, uint32_t prefix) {
    KTH_PRECONDITION(encoded != nullptr);
    auto const encoded_cpp = std::string(encoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::hd_private(encoded_cpp, prefix));
}


// Destructor

void kth_wallet_hd_private_destruct(kth_hd_private_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::wallet::hd_private>(self);
}


// Copy

kth_hd_private_mut_t kth_wallet_hd_private_copy(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::hd_private(kth::cpp_ref<kth::domain::wallet::hd_private>(self));
}


// Equality

kth_bool_t kth_wallet_hd_private_equals(kth_hd_private_const_t self, kth_hd_private_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::wallet::hd_private>(self) == kth::cpp_ref<kth::domain::wallet::hd_private>(other));
}


// Getters

char* kth_wallet_hd_private_encoded(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<kth::domain::wallet::hd_private>(self).encoded();
    return kth::create_c_str(s);
}

kth_hash_t kth_wallet_hd_private_secret(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::wallet::hd_private>(self).secret());
}

kth_hd_key_t kth_wallet_hd_private_to_hd_key(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hd_key_t(kth::cpp_ref<kth::domain::wallet::hd_private>(self).to_hd_key());
}

kth_hd_public_mut_t kth_wallet_hd_private_to_public(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::domain::wallet::hd_private>(self).to_public());
}

kth_bool_t kth_wallet_hd_private_valid(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<kth::domain::wallet::hd_private>(self)));
}

kth_hash_t kth_wallet_hd_private_chain_code(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::wallet::hd_private>(self).chain_code());
}

kth_hd_lineage_t kth_wallet_hd_private_lineage(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_c_struct<kth_hd_lineage_t>(kth::cpp_ref<kth::domain::wallet::hd_private>(self).lineage());
}

kth_ec_compressed_t kth_wallet_hd_private_point(kth_hd_private_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_ec_compressed_t(kth::cpp_ref<kth::domain::wallet::hd_private>(self).point());
}


// Operations

kth_bool_t kth_wallet_hd_private_less(kth_hd_private_const_t self, kth_hd_private_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<kth::domain::wallet::hd_private>(x);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::wallet::hd_private>(self).operator<(x_cpp));
}

kth_hd_private_mut_t kth_wallet_hd_private_derive_private(kth_hd_private_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::domain::wallet::hd_private>(self).derive_private(index));
}

kth_hd_public_mut_t kth_wallet_hd_private_derive_public(kth_hd_private_const_t self, uint32_t index) {
    KTH_PRECONDITION(self != nullptr);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::domain::wallet::hd_private>(self).derive_public(index));
}


// Static utilities

uint32_t kth_wallet_hd_private_to_prefix(uint64_t prefixes) {
    return kth::domain::wallet::hd_private::to_prefix(prefixes);
}

} // extern "C"
