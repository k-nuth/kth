// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/double_spend_proof_spender.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/double_spend_proof.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::double_spend_proof::spender;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_chain_double_spend_proof_spender_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_double_spend_proof_spender_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_chain_double_spend_proof_spender_destruct(kth_double_spend_proof_spender_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_double_spend_proof_spender_mut_t kth_chain_double_spend_proof_spender_copy(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_double_spend_proof_spender_equals(kth_double_spend_proof_spender_const_t self, kth_double_spend_proof_spender_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

kth_size_t kth_chain_double_spend_proof_spender_serialized_size(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size();
}


// Getters

uint32_t kth_chain_double_spend_proof_spender_version(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).version;
}

uint32_t kth_chain_double_spend_proof_spender_out_sequence(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).out_sequence;
}

uint32_t kth_chain_double_spend_proof_spender_locktime(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).locktime;
}

kth_hash_t kth_chain_double_spend_proof_spender_prev_outs_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).prev_outs_hash);
}

kth_hash_t kth_chain_double_spend_proof_spender_sequence_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).sequence_hash);
}

kth_hash_t kth_chain_double_spend_proof_spender_outputs_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).outputs_hash);
}

uint8_t* kth_chain_double_spend_proof_spender_push_data(kth_double_spend_proof_spender_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& data = kth::cpp_ref<cpp_t>(self).push_data;
    return kth::create_c_array(data, *out_size);
}


// Setters

void kth_chain_double_spend_proof_spender_set_version(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).version = value;
}

void kth_chain_double_spend_proof_spender_set_out_sequence(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).out_sequence = value;
}

void kth_chain_double_spend_proof_spender_set_locktime(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).locktime = value;
}

void kth_chain_double_spend_proof_spender_set_prev_outs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<cpp_t>(self).prev_outs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_prev_outs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).prev_outs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_sequence_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<cpp_t>(self).sequence_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_sequence_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).sequence_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_outputs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<cpp_t>(self).outputs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_outputs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).outputs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_push_data(kth_double_spend_proof_spender_mut_t self, uint8_t const* value, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr || n == 0);
    auto const value_cpp = n != 0 ? kth::data_chunk(value, value + n) : kth::data_chunk{};
    kth::cpp_ref<cpp_t>(self).push_data = value_cpp;
}


// Predicates

kth_bool_t kth_chain_double_spend_proof_spender_is_valid(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}


// Operations

void kth_chain_double_spend_proof_spender_reset(kth_double_spend_proof_spender_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
