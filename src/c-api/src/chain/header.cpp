// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/header.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/header.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::header;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_header_mut_t kth_chain_header_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_header_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_header_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = cpp_t::from_data(data_cpp, wire_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_header_mut_t kth_chain_header_construct(uint32_t version, kth_hash_t const* previous_block_hash, kth_hash_t const* merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce) {
    KTH_PRECONDITION(previous_block_hash != nullptr);
    KTH_PRECONDITION(merkle != nullptr);
    auto const previous_block_hash_cpp = kth::hash_to_cpp(previous_block_hash->hash);
    auto const merkle_cpp = kth::hash_to_cpp(merkle->hash);
    return kth::leak<cpp_t>(version, previous_block_hash_cpp, merkle_cpp, timestamp, bits, nonce);
}

kth_header_mut_t kth_chain_header_construct_unsafe(uint32_t version, uint8_t const* previous_block_hash, uint8_t const* merkle, uint32_t timestamp, uint32_t bits, uint32_t nonce) {
    KTH_PRECONDITION(previous_block_hash != nullptr);
    KTH_PRECONDITION(merkle != nullptr);
    auto const previous_block_hash_cpp = kth::hash_to_cpp(previous_block_hash);
    auto const merkle_cpp = kth::hash_to_cpp(merkle);
    return kth::leak<cpp_t>(version, previous_block_hash_cpp, merkle_cpp, timestamp, bits, nonce);
}


// Destructor

void kth_chain_header_destruct(kth_header_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_header_mut_t kth_chain_header_copy(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_header_equals(kth_header_const_t self, kth_header_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_header_to_data(kth_header_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_header_serialized_size(kth_header_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    return kth::cpp_ref<cpp_t>(self).serialized_size(wire_cpp);
}


// Getters

kth_hash_t kth_chain_header_hash_pow(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).hash_pow());
}

uint32_t kth_chain_header_version(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).version();
}

kth_hash_t kth_chain_header_previous_block_hash(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).previous_block_hash());
}

kth_hash_t kth_chain_header_merkle(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).merkle());
}

uint32_t kth_chain_header_timestamp(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).timestamp();
}

uint32_t kth_chain_header_bits(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).bits();
}

uint32_t kth_chain_header_nonce(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).nonce();
}


// Setters

void kth_chain_header_set_version(kth_header_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_version(value);
}

void kth_chain_header_set_previous_block_hash(kth_header_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).set_previous_block_hash(value_cpp);
}

void kth_chain_header_set_previous_block_hash_unsafe(kth_header_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).set_previous_block_hash(value_cpp);
}

void kth_chain_header_set_merkle(kth_header_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).set_merkle(value_cpp);
}

void kth_chain_header_set_merkle_unsafe(kth_header_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).set_merkle(value_cpp);
}

void kth_chain_header_set_timestamp(kth_header_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_timestamp(value);
}

void kth_chain_header_set_bits(kth_header_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_bits(value);
}

void kth_chain_header_set_nonce(kth_header_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_nonce(value);
}


// Predicates

kth_bool_t kth_chain_header_is_valid_proof_of_work(kth_header_const_t self, kth_bool_t retarget) {
    KTH_PRECONDITION(self != nullptr);
    auto const retarget_cpp = kth::int_to_bool(retarget);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid_proof_of_work(retarget_cpp));
}

kth_bool_t kth_chain_header_is_valid(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}

kth_bool_t kth_chain_header_is_valid_timestamp(kth_header_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid_timestamp());
}


// Operations

kth_error_code_t kth_chain_header_check(kth_header_const_t self, kth_bool_t retarget) {
    KTH_PRECONDITION(self != nullptr);
    auto const retarget_cpp = kth::int_to_bool(retarget);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).check(retarget_cpp));
}

kth_error_code_t kth_chain_header_accept(kth_header_const_t self, kth_chain_state_const_t state) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(state != nullptr);
    auto const& state_cpp = kth::cpp_ref<kth::domain::chain::chain_state>(state);
    return kth::to_c_err(kth::cpp_ref<cpp_t>(self).accept(state_cpp));
}

void kth_chain_header_reset(kth_header_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}


// Static utilities

kth_size_t kth_chain_header_satoshi_fixed_size(void) {
    return cpp_t::satoshi_fixed_size();
}

} // extern "C"
