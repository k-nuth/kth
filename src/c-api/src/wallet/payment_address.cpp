// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/payment_address.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/payment_address.hpp>

// Conversion functions
kth::domain::wallet::payment_address& kth_wallet_payment_address_mut_cpp(kth_payment_address_mut_t o) {
    return *static_cast<kth::domain::wallet::payment_address*>(o);
}
kth::domain::wallet::payment_address const& kth_wallet_payment_address_const_cpp(kth_payment_address_const_t o) {
    return *static_cast<kth::domain::wallet::payment_address const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_payment_address_mut_t kth_wallet_payment_address_construct_default(void) {
    return new kth::domain::wallet::payment_address();
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded(kth_payment_t decoded) {
    auto const decoded_cpp = kth::payment_to_cpp(decoded.hash);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(decoded_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_decoded_unsafe(uint8_t const* decoded) {
    KTH_PRECONDITION(decoded != nullptr);
    auto const decoded_cpp = kth::payment_to_cpp(decoded);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(decoded_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_private(kth_ec_private_const_t secret) {
    KTH_PRECONDITION(secret != nullptr);
    auto const& secret_cpp = kth_wallet_ec_private_const_cpp(secret);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(secret_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address(char const* address) {
    KTH_PRECONDITION(address != nullptr);
    auto const address_cpp = std::string(address);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(address_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_address_net(char const* address, kth_network_t net) {
    KTH_PRECONDITION(address != nullptr);
    auto const address_cpp = std::string(address);
    auto const net_cpp = static_cast<kth::domain::config::network>(net);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(address_cpp, net_cpp));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version(kth_shorthash_t short_hash, uint8_t version) {
    auto const short_hash_cpp = kth::short_hash_to_cpp(short_hash.hash);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(short_hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_short_hash_version_unsafe(uint8_t const* short_hash, uint8_t version) {
    KTH_PRECONDITION(short_hash != nullptr);
    auto const short_hash_cpp = kth::short_hash_to_cpp(short_hash);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(short_hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version(kth_hash_t hash, uint8_t version) {
    auto const hash_cpp = kth::hash_to_cpp(hash.hash);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_hash_version_unsafe(uint8_t const* hash, uint8_t version) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(hash_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_ec_public_version(kth_ec_public_const_t point, uint8_t version) {
    KTH_PRECONDITION(point != nullptr);
    auto const& point_cpp = kth_wallet_ec_public_const_cpp(point);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(point_cpp, version));
}

kth_payment_address_mut_t kth_wallet_payment_address_construct_from_script_version(kth_script_const_t script, uint8_t version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address(script_cpp, version));
}


// Static factories

kth_payment_address_mut_t kth_wallet_payment_address_from_pay_public_key_hash_script(kth_script_const_t script, uint8_t version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    return kth::make_leaked_if_valid(kth::domain::wallet::payment_address::from_pay_public_key_hash_script(script_cpp, version));
}


// Destructor

void kth_wallet_payment_address_destruct(kth_payment_address_mut_t self) {
    if (self == nullptr) return;
    delete &kth_wallet_payment_address_mut_cpp(self);
}


// Copy

kth_payment_address_mut_t kth_wallet_payment_address_copy(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::wallet::payment_address(kth_wallet_payment_address_const_cpp(self));
}


// Equality

kth_bool_t kth_wallet_payment_address_equals(kth_payment_address_const_t self, kth_payment_address_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_wallet_payment_address_const_cpp(self) == kth_wallet_payment_address_const_cpp(other));
}


// Getters

kth_bool_t kth_wallet_payment_address_valid(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_wallet_payment_address_const_cpp(self).operator bool());
}

char* kth_wallet_payment_address_encoded_legacy(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_wallet_payment_address_const_cpp(self).encoded_legacy();
    return kth::create_c_str(s);
}

char* kth_wallet_payment_address_encoded_token(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth_wallet_payment_address_const_cpp(self).encoded_token();
    return kth::create_c_str(s);
}

uint8_t kth_wallet_payment_address_version(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_wallet_payment_address_const_cpp(self).version();
}

uint8_t const* kth_wallet_payment_address_hash_span(kth_payment_address_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const span = kth_wallet_payment_address_const_cpp(self).hash_span();
    *out_size = span.size();
    return span.data();
}

kth_shorthash_t kth_wallet_payment_address_hash20(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_payment_address_const_cpp(self).hash20();
    return kth::to_shorthash_t(value_cpp);
}

kth_hash_t kth_wallet_payment_address_hash32(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_payment_address_const_cpp(self).hash32();
    return kth::to_hash_t(value_cpp);
}

kth_payment_t kth_wallet_payment_address_to_payment(kth_payment_address_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_wallet_payment_address_const_cpp(self).to_payment();
    return kth::to_payment_t(value_cpp);
}


// Operations

kth_bool_t kth_wallet_payment_address_less(kth_payment_address_const_t self, kth_payment_address_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth_wallet_payment_address_const_cpp(x);
    return kth::bool_to_int(kth_wallet_payment_address_const_cpp(self).operator<(x_cpp));
}

char* kth_wallet_payment_address_encoded_cashaddr(kth_payment_address_const_t self, kth_bool_t token_aware) {
    KTH_PRECONDITION(self != nullptr);
    auto const token_aware_cpp = kth::int_to_bool(token_aware);
    auto const s = kth_wallet_payment_address_const_cpp(self).encoded_cashaddr(token_aware_cpp);
    return kth::create_c_str(s);
}


// Static utilities

char* kth_wallet_payment_address_cashaddr_prefix_for(kth_network_t net) {
    auto const net_cpp = static_cast<kth::domain::config::network>(net);
    auto const s = kth::domain::wallet::payment_address::cashaddr_prefix_for(net_cpp);
    return kth::create_c_str(s);
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    return new std::vector<kth::domain::wallet::payment_address>(kth::domain::wallet::payment_address::extract(script_cpp, p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract_input(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    return new std::vector<kth::domain::wallet::payment_address>(kth::domain::wallet::payment_address::extract_input(script_cpp, p2kh_version, p2sh_version));
}

kth_payment_address_list_mut_t kth_wallet_payment_address_extract_output(kth_script_const_t script, uint8_t p2kh_version, uint8_t p2sh_version) {
    KTH_PRECONDITION(script != nullptr);
    auto const& script_cpp = kth_chain_script_const_cpp(script);
    return new std::vector<kth::domain::wallet::payment_address>(kth::domain::wallet::payment_address::extract_output(script_cpp, p2kh_version, p2sh_version));
}

} // extern "C"
