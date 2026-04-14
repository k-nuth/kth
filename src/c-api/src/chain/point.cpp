// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/point.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/chain/point.hpp>

// Conversion functions
kth::domain::chain::point& kth_chain_point_mut_cpp(kth_point_mut_t o) {
    return *static_cast<kth::domain::chain::point*>(o);
}
kth::domain::chain::point const& kth_chain_point_const_cpp(kth_point_const_t o) {
    return *static_cast<kth::domain::chain::point const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_point_mut_t kth_chain_point_construct_default(void) {
    return new kth::domain::chain::point();
}

kth_error_code_t kth_chain_point_construct_from_data(uint8_t const* data, kth_size_t n, kth_bool_t wire, KTH_OUT_OWNED kth_point_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto const wire_cpp = kth::int_to_bool(wire);
    auto result = kth::domain::chain::point::from_data(data_cpp, wire_cpp);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = new kth::domain::chain::point(std::move(*result));
    return kth_ec_success;
}

kth_point_mut_t kth_chain_point_construct(kth_hash_t hash, uint32_t index) {
    auto const hash_cpp = kth::hash_to_cpp(hash.hash);
    auto* obj = new kth::domain::chain::point(hash_cpp, index);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}

kth_point_mut_t kth_chain_point_construct_unsafe(uint8_t const* hash, uint32_t index) {
    KTH_PRECONDITION(hash != nullptr);
    auto const hash_cpp = kth::hash_to_cpp(hash);
    auto* obj = new kth::domain::chain::point(hash_cpp, index);
    if ( ! kth::check_valid(obj)) { delete obj; return nullptr; }
    return obj;
}


// Static factories

kth_point_mut_t kth_chain_point_null(void) {
    auto* obj = new kth::domain::chain::point(kth::domain::chain::point::null());
    return obj;
}


// Destructor

void kth_chain_point_destruct(kth_point_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_point_mut_cpp(self);
}


// Copy

kth_point_mut_t kth_chain_point_copy(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::chain::point(kth_chain_point_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_point_equals(kth_point_const_t self, kth_point_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_point_const_cpp(self) == kth_chain_point_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_point_to_data(kth_point_const_t self, kth_bool_t wire, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    auto const data = kth_chain_point_const_cpp(self).to_data(wire_cpp);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_point_serialized_size(kth_point_const_t self, kth_bool_t wire) {
    KTH_PRECONDITION(self != nullptr);
    auto const wire_cpp = kth::int_to_bool(wire);
    return kth_chain_point_const_cpp(self).serialized_size(wire_cpp);
}


// Getters

kth_hash_t kth_chain_point_hash(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth_chain_point_const_cpp(self).hash();
    return kth::to_hash_t(value_cpp);
}

uint32_t kth_chain_point_index(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_point_const_cpp(self).index();
}

uint64_t kth_chain_point_checksum(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_point_const_cpp(self).checksum();
}


// Setters

void kth_chain_point_set_hash(kth_point_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth_chain_point_mut_cpp(self).set_hash(value_cpp);
}

void kth_chain_point_set_hash_unsafe(kth_point_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth_chain_point_mut_cpp(self).set_hash(value_cpp);
}

void kth_chain_point_set_index(kth_point_mut_t self, uint32_t value) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_point_mut_cpp(self).set_index(value);
}


// Predicates

kth_bool_t kth_chain_point_is_valid(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_point_const_cpp(self).is_valid());
}

kth_bool_t kth_chain_point_is_null(kth_point_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_point_const_cpp(self).is_null());
}


// Operations

void kth_chain_point_reset(kth_point_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_point_mut_cpp(self).reset();
}


// Static utilities

kth_size_t kth_chain_point_satoshi_fixed_size(void) {
    return kth::domain::chain::point::satoshi_fixed_size();
}

} // extern "C"
