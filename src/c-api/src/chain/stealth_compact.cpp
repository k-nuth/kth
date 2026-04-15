// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/chain/stealth_compact.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/stealth.hpp>

// Conversion functions
kth::domain::chain::stealth_compact& kth_chain_stealth_compact_mut_cpp(kth_stealth_compact_mut_t o) {
    return *static_cast<kth::domain::chain::stealth_compact*>(o);
}
kth::domain::chain::stealth_compact const& kth_chain_stealth_compact_const_cpp(kth_stealth_compact_const_t o) {
    return *static_cast<kth::domain::chain::stealth_compact const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_chain_stealth_compact_destruct(kth_stealth_compact_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_stealth_compact_mut_cpp(self);
}


// Copy

kth_stealth_compact_mut_t kth_chain_stealth_compact_copy(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::stealth_compact(kth_chain_stealth_compact_const_cpp(self));
}


// Getters

kth_hash_t kth_chain_stealth_compact_ephemeral_public_key_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth_chain_stealth_compact_const_cpp(self).ephemeral_public_key_hash);
}

kth_shorthash_t kth_chain_stealth_compact_public_key_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_shorthash_t(kth_chain_stealth_compact_const_cpp(self).public_key_hash);
}

kth_hash_t kth_chain_stealth_compact_transaction_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth_chain_stealth_compact_const_cpp(self).transaction_hash);
}


// Setters

void kth_chain_stealth_compact_set_ephemeral_public_key_hash(kth_stealth_compact_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth_chain_stealth_compact_mut_cpp(self).ephemeral_public_key_hash = value_cpp;
}

void kth_chain_stealth_compact_set_ephemeral_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth_chain_stealth_compact_mut_cpp(self).ephemeral_public_key_hash = value_cpp;
}

void kth_chain_stealth_compact_set_public_key_hash(kth_stealth_compact_mut_t self, kth_shorthash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::short_hash_to_cpp(value.hash);
    kth_chain_stealth_compact_mut_cpp(self).public_key_hash = value_cpp;
}

void kth_chain_stealth_compact_set_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::short_hash_to_cpp(value);
    kth_chain_stealth_compact_mut_cpp(self).public_key_hash = value_cpp;
}

void kth_chain_stealth_compact_set_transaction_hash(kth_stealth_compact_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth_chain_stealth_compact_mut_cpp(self).transaction_hash = value_cpp;
}

void kth_chain_stealth_compact_set_transaction_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth_chain_stealth_compact_mut_cpp(self).transaction_hash = value_cpp;
}

} // extern "C"
