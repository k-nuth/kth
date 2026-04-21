// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/payment_address.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/payment_address.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::payment_address;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_payment_address_mut_t kth_wallet_payment_address_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded(kth_payment_t const* decoded) {
    KTH_PRECONDITION(decoded != nullptr);
    auto const decoded_cpp = kth::payment_to_cpp(decoded->hash);
    return kth::leak_if_valid(cpp_t(decoded_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded_unsafe(uint8_t const* decoded) {
    KTH_PRECONDITION(decoded != nullptr);
    auto const decoded_cpp = kth::payment_to_cpp(decoded);
    return kth::leak_if_valid(cpp_t(decoded_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_private(kth_ec_private_const_t secret) {
    KTH_PRECONDITION(secret != nullptr);
    auto const& secret_cpp = kth::cpp_ref<kth::domain::wallet::ec_private>(secret);
    return kth::leak_if_valid(cpp_t(secret_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address(char const* address) {
    KTH_PRECONDITION(address != nullptr);
    auto const address_cpp = std::string(address);
    return kth::leak_if_valid(cpp_t(address_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address_net(char const* address, kth_network_t net) {
    KTH_PRECONDITION(address != nullptr);
    auto const address_cpp = std::string(address);
    auto const net_cpp = kth::network_to_cpp(net);
    return kth::leak_if_valid(cpp_t(address_cpp, net_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version(kth_shorthash_t const* short_hash, uint8_t version) {
    KTH_PRECONDITION(short_hash != nullptr);
    auto const short_hash_cpp = kth::short_hash_to_cpp(short_hash->hash);
    return kth::leak_if_valid(cpp_t(short_hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version_unsafe(uint8_t const* short_hash, uint8_t version) {
    KTH_PRECONDITION(short_hash != nullptr);
    auto const short_hash_cpp = kth::short_hash_to_cpp(short_hash);
    return kth::leak_if_valid(cpp_t(short_hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version(kth_hash_t const* hash, uint8_t version) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash->hash);
    return kth::leak_if_valid(cpp_t(hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version_unsafe(uint8_t const* hash, uint8_t version) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    return kth::leak_if_valid(cpp_t(hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_public_version(kth_ec_public_const_t point, uint8_t version) {
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth::cpp_ref<kth::domain::wallet::ec_public>(point);
    return kth::leak_if_valid(cpp_t(point_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_script_version(kth_script_const_t script, uint8_t version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak_if_valid(cpp_t(script_cpp, version));
}


// Static factories

kth_payment_address_mut_t kth_wallet_payment_address_from_pay_public_key_hash_script(kth_script_const_t script, uint8_t version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak_if_valid(cpp_t::from_pay_public_key_hash_script(script_cpp, version));
}


// Destructor

void kth_wallet_payment_address_destruct(kth_payment_address_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_payment_address_mut_t kth_wallet_payment_address_copy(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_wallet_payment_address_equals(kth_payment_address_const_t self, kth_payment_address_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Getters

kth_bool_t kth_wallet_payment_address_valid(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

char* kth_wallet_payment_address_encoded_legacy(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded_legacy();
    return kth::create_c_str(s);
}

char* kth_wallet_payment_address_encoded_token(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded_token();
    return kth::create_c_str(s);
}

uint8_t kth_wallet_payment_address_version(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).version();
}

uint8_t const* kth_wallet_payment_address_hash_span(kth_payment_address_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const span = kth::cpp_ref<cpp_t>(self).hash_span();
    *out_size = span.size();
    return span.data();
}

kth_shorthash_t kth_wallet_payment_address_hash20(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_shorthash_t(kth::cpp_ref<cpp_t>(self).hash20());
}

kth_hash_t kth_wallet_payment_address_hash32(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).hash32());
}

kth_payment_t kth_wallet_payment_address_to_payment(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_payment_t(kth::cpp_ref<cpp_t>(self).to_payment());
}


// Operations

kth_bool_t kth_wallet_payment_address_less(kth_payment_address_const_t self, kth_payment_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

char* kth_wallet_payment_address_encoded_cashaddr(kth_payment_address_const_t self, kth_bool_t token_aware) {
    KTH_PRECONDITION(self != nullptr);
    auto const token_aware_cpp = kth::int_to_bool(token_aware);
    auto const s = kth::cpp_ref<cpp_t>(self).encoded_cashaddr(token_aware_cpp);
    return kth::create_c_str(s);
}


// Static utilities

char* kth_wallet_payment_address_cashaddr_prefix_for(kth_network_t net) {
    auto const net_cpp = kth::network_to_cpp(net);
    auto const s = cpp_t::cashaddr_prefix_for(net_cpp);
    return kth::create_c_str(s);
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak_list<cpp_t>(cpp_t::extract(script_cpp, p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract_input(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak_list<cpp_t>(cpp_t::extract_input(script_cpp, p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract_output(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth::cpp_ref<kth::domain::chain::script>(script);
    return kth::leak_list<cpp_t>(cpp_t::extract_output(script_cpp, p2kh_version, p2sh_version));
}

} // extern "C"
