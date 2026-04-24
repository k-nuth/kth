// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/wallet/stealth_sender.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/wallet/stealth_sender.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::wallet::stealth_sender;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_stealth_address_seed_binary_version(kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version) {
    KTH_PRECONDITION(address != nullptr);
    KTH_PRECONDITION(seed != nullptr || n == 0);
    KTH_PRECONDITION(filter != nullptr);
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::stealth_address>(address);
    auto const seed_cpp = n != 0 ? kth::data_chunk(seed, seed + n) : kth::data_chunk{};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    return kth::leak_if_valid(cpp_t(address_cpp, seed_cpp, filter_cpp, version));
}

kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version(kth_hash_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version) {
    KTH_PRECONDITION(ephemeral_private != nullptr);
    KTH_PRECONDITION(address != nullptr);
    KTH_PRECONDITION(seed != nullptr || n == 0);
    KTH_PRECONDITION(filter != nullptr);
    auto ephemeral_private_cpp = kth::hash_to_cpp(ephemeral_private->hash);
    kth::secure_scrub ephemeral_private_cpp_scrub{&ephemeral_private_cpp, sizeof(ephemeral_private_cpp)};
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::stealth_address>(address);
    auto const seed_cpp = n != 0 ? kth::data_chunk(seed, seed + n) : kth::data_chunk{};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    return kth::leak_if_valid(cpp_t(ephemeral_private_cpp, address_cpp, seed_cpp, filter_cpp, version));
}

kth_stealth_sender_mut_t kth_wallet_stealth_sender_construct_from_ephemeral_private_stealth_address_seed_binary_version_unsafe(uint8_t const* ephemeral_private, kth_stealth_address_const_t address, uint8_t const* seed, kth_size_t n, kth_binary_const_t filter, uint8_t version) {
    KTH_PRECONDITION(ephemeral_private != nullptr);
    KTH_PRECONDITION(address != nullptr);
    KTH_PRECONDITION(seed != nullptr || n == 0);
    KTH_PRECONDITION(filter != nullptr);
    auto ephemeral_private_cpp = kth::hash_to_cpp(ephemeral_private);
    kth::secure_scrub ephemeral_private_cpp_scrub{&ephemeral_private_cpp, sizeof(ephemeral_private_cpp)};
    auto const& address_cpp = kth::cpp_ref<kth::domain::wallet::stealth_address>(address);
    auto const seed_cpp = n != 0 ? kth::data_chunk(seed, seed + n) : kth::data_chunk{};
    auto const& filter_cpp = kth::cpp_ref<kth::binary>(filter);
    return kth::leak_if_valid(cpp_t(ephemeral_private_cpp, address_cpp, seed_cpp, filter_cpp, version));
}


// Destructor

void kth_wallet_stealth_sender_destruct(kth_stealth_sender_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_stealth_sender_mut_t kth_wallet_stealth_sender_copy(kth_stealth_sender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Getters

kth_bool_t kth_wallet_stealth_sender_valid(kth_stealth_sender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(static_cast<bool>(kth::cpp_ref<cpp_t>(self)));
}

kth_script_const_t kth_wallet_stealth_sender_stealth_script(kth_stealth_sender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).stealth_script());
}

kth_payment_address_const_t kth_wallet_stealth_sender_payment_address(kth_stealth_sender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).payment_address());
}

} // extern "C"
