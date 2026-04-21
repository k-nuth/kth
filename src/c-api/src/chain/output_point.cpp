// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/output_point.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/output_point.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::chain::output_point;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_output_point_mut_t kth_chain_output_point_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_output_point_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_output_point_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = cpp_t::from_data(data_cpp, wire_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak<cpp_t>(std::move(*result));
    return kth_ec_success;
}

kth_output_point_mut_t kth_chain_output_point_construct_from_hash_index(kth_hash_t const* hash, uint32_t index) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash->hash);
    return kth::leak<cpp_t>(hash_cpp, index);
}

kth_output_point_mut_t kth_chain_output_point_construct_from_hash_index_unsafe(uint8_t const* hash, uint32_t index) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    return kth::leak<cpp_t>(hash_cpp, index);
}

kth_output_point_mut_t kth_chain_output_point_construct_from_point(kth_point_const_t x) {
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<kth::domain::chain::point>(x);
    return kth::leak<cpp_t>(x_cpp);
}


// Destructor

void kth_chain_output_point_destruct(kth_output_point_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_output_point_mut_t kth_chain_output_point_copy(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_output_point_equals(kth_output_point_const_t self, kth_output_point_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_output_point_to_data(kth_output_point_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_output_point_serialized_size(kth_output_point_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    return kth::cpp_ref<cpp_t>(self).serialized_size(wire_cpp);
}


// Getters

kth_hash_t kth_chain_output_point_hash(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).hash());
}

uint32_t kth_chain_output_point_index(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).index();
}

uint64_t kth_chain_output_point_checksum(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).checksum();
}


// Setters

void kth_chain_output_point_set_hash(kth_output_point_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).set_hash(value_cpp);
}

void kth_chain_output_point_set_hash_unsafe(kth_output_point_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).set_hash(value_cpp);
}

void kth_chain_output_point_set_index(kth_output_point_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).set_index(value);
}


// Predicates

kth_bool_t kth_chain_output_point_is_mature(kth_output_point_const_t self, kth_size_t height) {
    KTH_PRECONDITION(self != nullptr);
    auto const height_cpp = kth::sz(height);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_mature(height_cpp));
}

kth_bool_t kth_chain_output_point_is_valid(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}

kth_bool_t kth_chain_output_point_is_null(kth_output_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_null());
}


// Operations

void kth_chain_output_point_reset(kth_output_point_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}


// Static utilities

kth_size_t kth_chain_output_point_satoshi_fixed_size(void) {
    return cpp_t::satoshi_fixed_size();
}

} // extern "C"
