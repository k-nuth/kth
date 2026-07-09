// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>
#include <utility>

#include <kth/capi/wallet/stealth_receiver.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/stealth_receiver.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::stealth_receiver;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_wallet_stealth_receiver_from_secrets(kth_hash_t const* scan_private, kth_hash_t const* spend_private, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_receiver_mut_t* out) {
    KTH_PRECONDITION(scan_private != nullptr);
    KTH_PRECONDITION(spend_private != nullptr);
    KTH_PRECONDITION(filter != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto scan_private_cpp = kth::hash_to_cpp(scan_private->hash);
    kth::secure_scrub scan_private_cpp_scrub{&scan_private_cpp, sizeof(scan_private_cpp)};
    auto spend_private_cpp = kth::hash_to_cpp(spend_private->hash);
    kth::secure_scrub spend_private_cpp_scrub{&spend_private_cpp, sizeof(spend_private_cpp)};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    auto result = cpp_t::from_secrets(scan_private_cpp, spend_private_cpp, filter_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_receiver_from_secrets_unsafe(uint8_t const* scan_private, uint8_t const* spend_private, kth_binary_const_t filter, uint8_t version, KTH_OUT_OWNED kth_stealth_receiver_mut_t* out) {
    KTH_PRECONDITION(scan_private != nullptr);
    KTH_PRECONDITION(spend_private != nullptr);
    KTH_PRECONDITION(filter != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto scan_private_cpp = kth::hash_to_cpp(scan_private);
    kth::secure_scrub scan_private_cpp_scrub{&scan_private_cpp, sizeof(scan_private_cpp)};
    auto spend_private_cpp = kth::hash_to_cpp(spend_private);
    kth::secure_scrub spend_private_cpp_scrub{&spend_private_cpp, sizeof(spend_private_cpp)};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    auto result = cpp_t::from_secrets(scan_private_cpp, spend_private_cpp, filter_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_wallet_stealth_receiver_destruct(kth_stealth_receiver_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_copy(kth_stealth_receiver_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_stealth_address_const_t kth_wallet_stealth_receiver_stealth_address(kth_stealth_receiver_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).stealth_address());
}


// Operations

kth_error_code_t kth_wallet_stealth_receiver_derive_address(kth_stealth_receiver_const_t self, kth_ec_compressed_t const* ephemeral_public, KTH_OUT_OWNED kth_payment_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public->data);
    auto result = kth::cpp_ref<cpp_t>(self).derive_address(ephemeral_public_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_receiver_derive_address_unsafe(kth_stealth_receiver_const_t self, uint8_t const* ephemeral_public, KTH_OUT_OWNED kth_payment_address_mut_t* out) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public);
    auto result = kth::cpp_ref<cpp_t>(self).derive_address(ephemeral_public_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_receiver_derive_private(kth_stealth_receiver_const_t self, kth_ec_compressed_t const* ephemeral_public, uint8_t* out, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(n >= 32);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public->data);
    auto const result = kth::cpp_ref<cpp_t>(self).derive_private(ephemeral_public_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    std::memcpy(out, result->data(), result->size());
    return kth_ec_success;
}

kth_error_code_t kth_wallet_stealth_receiver_derive_private_unsafe(kth_stealth_receiver_const_t self, uint8_t const* ephemeral_public, uint8_t* out, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(n >= 32);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public);
    auto const result = kth::cpp_ref<cpp_t>(self).derive_private(ephemeral_public_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    std::memcpy(out, result->data(), result->size());
    return kth_ec_success;
}

} // extern "C"
