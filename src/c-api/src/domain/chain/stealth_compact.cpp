// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/capi/domain/chain/stealth_compact.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/domain/chain/stealth.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::stealth_compact;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Destructor

void kth_domain_chain_stealth_compact_destruct(kth_stealth_compact_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_stealth_compact_mut_t kth_domain_chain_stealth_compact_copy(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_domain_chain_stealth_compact_equals(kth_stealth_compact_const_t self, kth_stealth_compact_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}

kth_bool_t kth_domain_chain_stealth_compact_not_equal(kth_stealth_compact_const_t self, kth_stealth_compact_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::ne<cpp_t>(self, other);
}


// Ordering

kth_bool_t kth_domain_chain_stealth_compact_less(kth_stealth_compact_const_t self, kth_stealth_compact_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::lt<cpp_t>(self, x);
}

kth_bool_t kth_domain_chain_stealth_compact_greater(kth_stealth_compact_const_t self, kth_stealth_compact_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::gt<cpp_t>(self, x);
}

kth_bool_t kth_domain_chain_stealth_compact_less_or_equal(kth_stealth_compact_const_t self, kth_stealth_compact_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::le<cpp_t>(self, x);
}

kth_bool_t kth_domain_chain_stealth_compact_greater_or_equal(kth_stealth_compact_const_t self, kth_stealth_compact_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    return kth::ge<cpp_t>(self, x);
}


// Getters

kth_hash_t kth_domain_chain_stealth_compact_ephemeral_public_key_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).ephemeral_public_key_hash);
}

kth_shorthash_t kth_domain_chain_stealth_compact_public_key_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_shorthash_t(kth::cpp_ref<cpp_t>(self).public_key_hash);
}

kth_hash_t kth_domain_chain_stealth_compact_transaction_hash(kth_stealth_compact_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).transaction_hash);
}


// Setters

void kth_domain_chain_stealth_compact_set_ephemeral_public_key_hash(kth_stealth_compact_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).ephemeral_public_key_hash = value_cpp;
}

void kth_domain_chain_stealth_compact_set_ephemeral_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).ephemeral_public_key_hash = value_cpp;
}

void kth_domain_chain_stealth_compact_set_public_key_hash(kth_stealth_compact_mut_t self, kth_shorthash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::short_hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).public_key_hash = value_cpp;
}

void kth_domain_chain_stealth_compact_set_public_key_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::short_hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).public_key_hash = value_cpp;
}

void kth_domain_chain_stealth_compact_set_transaction_hash(kth_stealth_compact_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).transaction_hash = value_cpp;
}

void kth_domain_chain_stealth_compact_set_transaction_hash_unsafe(kth_stealth_compact_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).transaction_hash = value_cpp;
}

} // extern "C"
