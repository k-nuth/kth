// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/get_blocks.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/get_blocks.hpp>

// Conversion functions
kth::domain::message::get_blocks& kth_chain_get_blocks_mut_cpp(kth_get_blocks_mut_t o) {
    return *static_cast<kth::domain::message::get_blocks*>(o);
}
kth::domain::message::get_blocks const& kth_chain_get_blocks_const_cpp(kth_get_blocks_const_t o) {
    return *static_cast<kth::domain::message::get_blocks const*>(o);
}

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_get_blocks_mut_t kth_chain_get_blocks_construct_default(void) {
    return new kth::domain::message::get_blocks();
}

kth_error_code_t kth_chain_get_blocks_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_get_blocks_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, static_cast<size_t>(n)));
    auto result = kth::domain::message::get_blocks::from_data(data_cpp, version);
    if ( ! result) return static_cast<kth_error_code_t>(result.error().value());
    *out = kth::make_leaked(std::move(*result));
    return kth_ec_success;
}

kth_get_blocks_mut_t kth_chain_get_blocks_construct(kth_hash_list_const_t start, kth_hash_t stop) {
    KTH_PRECONDITION(start != nullptr);
    auto const& start_cpp = kth_core_hash_list_const_cpp(start);
    auto const stop_cpp = kth::hash_to_cpp(stop.hash);
    return kth::make_leaked_if_valid(kth::domain::message::get_blocks(start_cpp, stop_cpp));
}

kth_get_blocks_mut_t kth_chain_get_blocks_construct_unsafe(kth_hash_list_const_t start, uint8_t const* stop) {
    KTH_PRECONDITION(start != nullptr);
    KTH_PRECONDITION(stop != nullptr);
    auto const& start_cpp = kth_core_hash_list_const_cpp(start);
    auto const stop_cpp = kth::hash_to_cpp(stop);
    return kth::make_leaked_if_valid(kth::domain::message::get_blocks(start_cpp, stop_cpp));
}


// Destructor

void kth_chain_get_blocks_destruct(kth_get_blocks_mut_t self) {
    if (self == nullptr) return;
    delete &kth_chain_get_blocks_mut_cpp(self);
}


// Copy

kth_get_blocks_mut_t kth_chain_get_blocks_copy(kth_get_blocks_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::domain::message::get_blocks(kth_chain_get_blocks_const_cpp(self));
}


// Equality

kth_bool_t kth_chain_get_blocks_equals(kth_get_blocks_const_t self, kth_get_blocks_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth_chain_get_blocks_const_cpp(self) == kth_chain_get_blocks_const_cpp(other));
}


// Serialization

uint8_t* kth_chain_get_blocks_to_data(kth_get_blocks_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth_chain_get_blocks_const_cpp(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_get_blocks_serialized_size(kth_get_blocks_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth_chain_get_blocks_const_cpp(self).serialized_size(version);
}


// Getters

kth_hash_list_const_t kth_chain_get_blocks_start_hashes(kth_get_blocks_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth_chain_get_blocks_const_cpp(self).start_hashes());
}

kth_hash_t kth_chain_get_blocks_stop_hash(kth_get_blocks_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth_chain_get_blocks_const_cpp(self).stop_hash());
}


// Setters

void kth_chain_get_blocks_set_start_hashes(kth_get_blocks_mut_t self, kth_hash_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth_core_hash_list_const_cpp(value);
    kth_chain_get_blocks_mut_cpp(self).set_start_hashes(value_cpp);
}

void kth_chain_get_blocks_set_stop_hash(kth_get_blocks_mut_t self, kth_hash_t value) {
    KTH_PRECONDITION(self != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value.hash);
    kth_chain_get_blocks_mut_cpp(self).set_stop_hash(value_cpp);
}

void kth_chain_get_blocks_set_stop_hash_unsafe(kth_get_blocks_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth_chain_get_blocks_mut_cpp(self).set_stop_hash(value_cpp);
}


// Predicates

kth_bool_t kth_chain_get_blocks_is_valid(kth_get_blocks_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth_chain_get_blocks_const_cpp(self).is_valid());
}


// Operations

void kth_chain_get_blocks_reset(kth_get_blocks_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth_chain_get_blocks_mut_cpp(self).reset();
}

} // extern "C"
