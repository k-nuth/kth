// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/double_spend_proof_spender.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/double_spend_proof.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_error_code_t kth_chain_double_spend_proof_spender_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_double_spend_proof_spender_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto result = kth::domain::message::double_spend_proof::spender::from_data(data_cpp, version);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}


// Destructor

void kth_chain_double_spend_proof_spender_destruct(kth_double_spend_proof_spender_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self);
}


// Copy

kth_double_spend_proof_spender_mut_t kth_chain_double_spend_proof_spender_copy(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::message::double_spend_proof::spender(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self));
}


// Equality

kth_bool_t kth_chain_double_spend_proof_spender_equals(kth_double_spend_proof_spender_const_t self, kth_double_spend_proof_spender_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self) == kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(other));
}


// Serialization

kth_size_t kth_chain_double_spend_proof_spender_serialized_size(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).serialized_size();
}


// Getters

uint32_t kth_chain_double_spend_proof_spender_version(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).version;
}

uint32_t kth_chain_double_spend_proof_spender_out_sequence(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).out_sequence;
}

uint32_t kth_chain_double_spend_proof_spender_locktime(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).locktime;
}

kth_hash_t kth_chain_double_spend_proof_spender_prev_outs_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).prev_outs_hash);
}

kth_hash_t kth_chain_double_spend_proof_spender_sequence_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).sequence_hash);
}

kth_hash_t kth_chain_double_spend_proof_spender_outputs_hash(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).outputs_hash);
}

uint8_t* kth_chain_double_spend_proof_spender_push_data(kth_double_spend_proof_spender_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const& data = kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).push_data;
    return kth::create_c_array(data, *out_size);
}


// Setters

void kth_chain_double_spend_proof_spender_set_version(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).version = value;
}

void kth_chain_double_spend_proof_spender_set_out_sequence(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).out_sequence = value;
}

void kth_chain_double_spend_proof_spender_set_locktime(kth_double_spend_proof_spender_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).locktime = value;
}

void kth_chain_double_spend_proof_spender_set_prev_outs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).prev_outs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_prev_outs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).prev_outs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_sequence_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).sequence_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_sequence_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).sequence_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_outputs_hash(kth_double_spend_proof_spender_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).outputs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_outputs_hash_unsafe(kth_double_spend_proof_spender_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).outputs_hash = value_cpp;
}

void kth_chain_double_spend_proof_spender_set_push_data(kth_double_spend_proof_spender_mut_t self, uint8_t const* value, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr || n == 0);
    auto const value_cpp = n != 0 ? kth::data_chunk(value, value + n) : kth::data_chunk{};
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).push_data = value_cpp;
}


// Predicates

kth_bool_t kth_chain_double_spend_proof_spender_is_valid(kth_double_spend_proof_spender_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).is_valid());
}


// Operations

void kth_chain_double_spend_proof_spender_reset(kth_double_spend_proof_spender_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<kth::domain::message::double_spend_proof::spender>(self).reset();
}

} // extern "C"
