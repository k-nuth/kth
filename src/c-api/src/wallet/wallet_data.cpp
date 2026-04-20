// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/wallet/wallet_data.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/wallet_manager.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::wallet_data;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_wallet_wallet_data_destruct(kth_wallet_data_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_wallet_data_mut_t kth_wallet_wallet_data_copy(kth_wallet_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_string_list_mut_t kth_wallet_wallet_data_mnemonics(kth_wallet_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const cpp_result = kth::cpp_ref<cpp_t>(self).mnemonics;
    return kth::leak_list<std::string>(cpp_result.begin(), cpp_result.end());
}

kth_hd_public_const_t kth_wallet_wallet_data_xpub(kth_wallet_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).xpub);
}

kth_encrypted_seed_t kth_wallet_wallet_data_encrypted_seed(kth_wallet_data_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_encrypted_seed_t(kth::cpp_ref<cpp_t>(self).encrypted_seed);
}


// Setters

void kth_wallet_wallet_data_set_mnemonics(kth_wallet_data_mut_t self, kth_string_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<std::vector<std::string>>(value);
    kth::cpp_ref<cpp_t>(self).mnemonics = value_cpp;
}

void kth_wallet_wallet_data_set_xpub(kth_wallet_data_mut_t self, kth_hd_public_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::domain::wallet::hd_public>(value);
    kth::cpp_ref<cpp_t>(self).xpub = value_cpp;
}

void kth_wallet_wallet_data_set_encrypted_seed(kth_wallet_data_mut_t self, kth_encrypted_seed_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::encrypted_seed_to_cpp(value.hash);
    kth::cpp_ref<cpp_t>(self).encrypted_seed = value_cpp;
}

void kth_wallet_wallet_data_set_encrypted_seed_unsafe(kth_wallet_data_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::encrypted_seed_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).encrypted_seed = value_cpp;
}


// Static utilities

kth_error_code_t kth_wallet_create(char const* password, char const* normalized_passphrase, KTH_OUT_OWNED kth_wallet_data_mut_t* out) {
    KTH_PRECONDITION(password != nullptr);
    KTH_PRECONDITION(normalized_passphrase != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const password_cpp = std::string(password);
    auto const normalized_passphrase_cpp = std::string(normalized_passphrase);
    auto result = kth::domain::wallet::create(password_cpp, normalized_passphrase_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

} // extern "C"
