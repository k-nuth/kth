// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/double_spend_proof.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/double_spend_proof.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::double_spend_proof;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_double_spend_proof_mut_t kth_chain_double_spend_proof_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_double_spend_proof_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_double_spend_proof_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_double_spend_proof_mut_t kth_chain_double_spend_proof_construct(kth_output_point_const_t out_point, kth_double_spend_proof_spender_const_t spender1, kth_double_spend_proof_spender_const_t spender2) {
    KTH_PRECONDITION(out_point != nullptr);
    KTH_PRECONDITION(spender1 != nullptr);
    KTH_PRECONDITION(spender2 != nullptr);
    auto const& out_point_cpp = kth::cpp_ref<kth::domain::chain::output_point>(out_point);
    auto const& spender1_cpp = kth::cpp_ref<cpp_t::spender>(spender1);
    auto const& spender2_cpp = kth::cpp_ref<cpp_t::spender>(spender2);
    return kth::leak<cpp_t>(out_point_cpp, spender1_cpp, spender2_cpp);
}


// Destructor

void kth_chain_double_spend_proof_destruct(kth_double_spend_proof_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_double_spend_proof_mut_t kth_chain_double_spend_proof_copy(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_double_spend_proof_equals(kth_double_spend_proof_const_t self, kth_double_spend_proof_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_double_spend_proof_to_data(kth_double_spend_proof_const_t self, kth_size_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const version_cpp = kth::sz(version);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(version_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_double_spend_proof_serialized_size(kth_double_spend_proof_const_t self, kth_size_t version) {
    KTH_PRECONDITION(self != nullptr);
    auto const version_cpp = kth::sz(version);
    return kth::cpp_ref<cpp_t>(self).serialized_size(version_cpp);
}


// Getters

kth_output_point_const_t kth_chain_double_spend_proof_out_point(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).out_point());
}

kth_double_spend_proof_spender_const_t kth_chain_double_spend_proof_spender1(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).spender1());
}

kth_double_spend_proof_spender_const_t kth_chain_double_spend_proof_spender2(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).spender2());
}

kth_hash_t kth_chain_double_spend_proof_hash(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).hash());
}


// Setters

void kth_chain_double_spend_proof_set_out_point(kth_double_spend_proof_mut_t self, kth_output_point_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<kth::domain::chain::output_point>(x);
    kth::cpp_ref<cpp_t>(self).set_out_point(x_cpp);
}

void kth_chain_double_spend_proof_set_spender1(kth_double_spend_proof_mut_t self, kth_double_spend_proof_spender_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t::spender>(x);
    kth::cpp_ref<cpp_t>(self).set_spender1(x_cpp);
}

void kth_chain_double_spend_proof_set_spender2(kth_double_spend_proof_mut_t self, kth_double_spend_proof_spender_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t::spender>(x);
    kth::cpp_ref<cpp_t>(self).set_spender2(x_cpp);
}


// Predicates

kth_bool_t kth_chain_double_spend_proof_is_valid(kth_double_spend_proof_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}


// Operations

void kth_chain_double_spend_proof_reset(kth_double_spend_proof_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
