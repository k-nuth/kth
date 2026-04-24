// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>

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

kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_construct(kth_hash_t const* scan_private, kth_hash_t const* spend_private, kth_binary_const_t filter, uint8_t version) {
    KTH_PRECONDITION(scan_private != nullptr);
    KTH_PRECONDITION(spend_private != nullptr);
    KTH_PRECONDITION(filter != nullptr);
    auto scan_private_cpp = kth::hash_to_cpp(scan_private->hash);
    kth::secure_scrub scan_private_cpp_scrub{&scan_private_cpp, sizeof(scan_private_cpp)};
    auto spend_private_cpp = kth::hash_to_cpp(spend_private->hash);
    kth::secure_scrub spend_private_cpp_scrub{&spend_private_cpp, sizeof(spend_private_cpp)};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    return kth::leak_if_valid(cpp_t(scan_private_cpp, spend_private_cpp, filter_cpp, version));
}

kth_stealth_receiver_mut_t kth_wallet_stealth_receiver_construct_unsafe(uint8_t const* scan_private, uint8_t const* spend_private, kth_binary_const_t filter, uint8_t version) {
    KTH_PRECONDITION(scan_private != nullptr);
    KTH_PRECONDITION(spend_private != nullptr);
    KTH_PRECONDITION(filter != nullptr);
    auto scan_private_cpp = kth::hash_to_cpp(scan_private);
    kth::secure_scrub scan_private_cpp_scrub{&scan_private_cpp, sizeof(scan_private_cpp)};
    auto spend_private_cpp = kth::hash_to_cpp(spend_private);
    kth::secure_scrub spend_private_cpp_scrub{&spend_private_cpp, sizeof(spend_private_cpp)};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    return kth::leak_if_valid(cpp_t(scan_private_cpp, spend_private_cpp, filter_cpp, version));
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

kth_bool_t kth_wallet_stealth_receiver_valid(kth_stealth_receiver_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

kth_stealth_address_const_t kth_wallet_stealth_receiver_stealth_address(kth_stealth_receiver_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).stealth_address());
}


// Operations

kth_bool_t kth_wallet_stealth_receiver_derive_address(kth_stealth_receiver_const_t self, kth_payment_address_mut_t out_address, kth_ec_compressed_t const* ephemeral_public) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_address != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    auto& out_address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(out_address);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public->data);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).derive_address(out_address_cpp, ephemeral_public_cpp));
}

kth_bool_t kth_wallet_stealth_receiver_derive_address_unsafe(kth_stealth_receiver_const_t self, kth_payment_address_mut_t out_address, uint8_t const* ephemeral_public) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_address != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    auto& out_address_cpp = kth::cpp_ref<kth::domain::wallet::payment_address>(out_address);
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).derive_address(out_address_cpp, ephemeral_public_cpp));
}

kth_bool_t kth_wallet_stealth_receiver_derive_private(kth_stealth_receiver_const_t self, kth_hash_t* out_private, kth_ec_compressed_t const* ephemeral_public) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_private != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    kth::hash_digest out_private_cpp;
    kth::secure_scrub out_private_cpp_scrub{&out_private_cpp, sizeof(out_private_cpp)};
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public->data);
    auto const cpp_result = kth::cpp_ref<cpp_t>(self).derive_private(out_private_cpp, ephemeral_public_cpp);
    if (cpp_result) {
        std::memcpy(out_private->hash, out_private_cpp.data(), out_private_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

kth_bool_t kth_wallet_stealth_receiver_derive_private_unsafe(kth_stealth_receiver_const_t self, kth_hash_t* out_private, uint8_t const* ephemeral_public) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_private != nullptr);
    KTH_PRECONDITION(ephemeral_public != nullptr);
    kth::hash_digest out_private_cpp;
    kth::secure_scrub out_private_cpp_scrub{&out_private_cpp, sizeof(out_private_cpp)};
    auto const ephemeral_public_cpp = kth::ec_compressed_to_cpp(ephemeral_public);
    auto const cpp_result = kth::cpp_ref<cpp_t>(self).derive_private(out_private_cpp, ephemeral_public_cpp);
    if (cpp_result) {
        std::memcpy(out_private->hash, out_private_cpp.data(), out_private_cpp.size());
    }
    return kth::bool_to_int(cpp_result);
}

} // extern "C"
