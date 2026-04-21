// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <utility>

#include <kth/capi/chain/get_headers.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/byte_reader.hpp>
#include <kth/domain/message/get_headers.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::domain::message::get_headers;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_get_headers_mut_t kth_chain_get_headers_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_error_code_t kth_chain_get_headers_construct_from_data(uint8_t const* data, kth_size_t n, uint32_t version, KTH_OUT_OWNED kth_get_headers_mut_t* out) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto data_cpp = kth::byte_reader(kth::byte_span(data, kth::sz(n)));
    auto result = cpp_t::from_data(data_cpp, version);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_get_headers_mut_t kth_chain_get_headers_construct(kth_hash_list_const_t start, kth_hash_t const* stop) {
    KTH_PRECONDITION(start != nullptr);
    KTH_PRECONDITION(stop != nullptr);
    auto const& start_cpp = kth::cpp_ref<kth::hash_list>(start);
    auto const stop_cpp = kth::hash_to_cpp(stop->hash);
    return kth::leak<cpp_t>(start_cpp, stop_cpp);
}

kth_get_headers_mut_t kth_chain_get_headers_construct_unsafe(kth_hash_list_const_t start, uint8_t const* stop) {
    KTH_PRECONDITION(start != nullptr);
    KTH_PRECONDITION(stop != nullptr);
    auto const& start_cpp = kth::cpp_ref<kth::hash_list>(start);
    auto const stop_cpp = kth::hash_to_cpp(stop);
    return kth::leak<cpp_t>(start_cpp, stop_cpp);
}


// Destructor

void kth_chain_get_headers_destruct(kth_get_headers_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_get_headers_mut_t kth_chain_get_headers_copy(kth_get_headers_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_chain_get_headers_equals(kth_get_headers_const_t self, kth_get_headers_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Serialization

uint8_t* kth_chain_get_headers_to_data(kth_get_headers_const_t self, uint32_t version, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).to_data(version);
    return kth::create_c_array(data, *out_size);
}

kth_size_t kth_chain_get_headers_serialized_size(kth_get_headers_const_t self, uint32_t version) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).serialized_size(version);
}


// Getters

kth_hash_list_const_t kth_chain_get_headers_start_hashes(kth_get_headers_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return &(kth::cpp_ref<cpp_t>(self).start_hashes());
}

kth_hash_t kth_chain_get_headers_stop_hash(kth_get_headers_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::to_hash_t(kth::cpp_ref<cpp_t>(self).stop_hash());
}


// Setters

void kth_chain_get_headers_set_start_hashes(kth_get_headers_mut_t self, kth_hash_list_const_t value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const& value_cpp = kth::cpp_ref<kth::hash_list>(value);
    kth::cpp_ref<cpp_t>(self).set_start_hashes(value_cpp);
}

void kth_chain_get_headers_set_stop_hash(kth_get_headers_mut_t self, kth_hash_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value->hash);
    kth::cpp_ref<cpp_t>(self).set_stop_hash(value_cpp);
}

void kth_chain_get_headers_set_stop_hash_unsafe(kth_get_headers_mut_t self, uint8_t const* value) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(value != nullptr);
    auto const value_cpp = kth::hash_to_cpp(value);
    kth::cpp_ref<cpp_t>(self).set_stop_hash(value_cpp);
}


// Predicates

kth_bool_t kth_chain_get_headers_is_valid(kth_get_headers_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_valid());
}


// Operations

void kth_chain_get_headers_reset(kth_get_headers_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).reset();
}

} // extern "C"
