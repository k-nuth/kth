// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>

#include <kth/capi/binary.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/utility/binary.hpp>

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_binary_mut_t kth_core_binary_construct_default(void) {
    return new kth::binary();
}

kth_binary_mut_t kth_core_binary_construct_from_bit_string(char const* bit_string) {
    KTH_PRECONDITION(bit_string != nullptr);
    auto const bit_string_cpp = std::string_view(bit_string);
    return kth::make_leaked<kth::binary>(bit_string_cpp);
}

kth_binary_mut_t kth_core_binary_construct_from_size_number(kth_size_t size, uint32_t number) {
    auto const size_cpp = static_cast<size_t>(size);
    return kth::make_leaked<kth::binary>(size_cpp, number);
}

kth_binary_mut_t kth_core_binary_construct_from_size_blocks(kth_size_t size, uint8_t const* blocks, kth_size_t n) {
    KTH_PRECONDITION(blocks != nullptr || n == 0);
    auto const size_cpp = static_cast<size_t>(size);
    auto const blocks_cpp = kth::byte_span(blocks, static_cast<size_t>(n));
    return kth::make_leaked<kth::binary>(size_cpp, blocks_cpp);
}


// Destructor

void kth_core_binary_destruct(kth_binary_mut_t self) {
    if (self == nullptr) return;
    delete &kth::cpp_ref<kth::binary>(self);
}


// Copy

kth_binary_mut_t kth_core_binary_copy(kth_binary_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return new kth::binary(kth::cpp_ref<kth::binary>(self));
}


// Equality

kth_bool_t kth_core_binary_equals(kth_binary_const_t self, kth_binary_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self) == kth::cpp_ref<kth::binary>(other));
}


// Getters

uint8_t* kth_core_binary_blocks(kth_binary_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<kth::binary>(self).blocks();
    return kth::create_c_array(data, *out_size);
}

char* kth_core_binary_encoded(kth_binary_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<kth::binary>(self).encoded();
    return kth::create_c_str(s);
}

kth_size_t kth_core_binary_size(kth_binary_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<kth::binary>(self).size();
}


// Predicates

kth_bool_t kth_core_binary_is_base2(char const* text) {
    KTH_PRECONDITION(text != nullptr);
    auto const text_cpp = std::string_view(text);
    return kth::bool_to_int(kth::binary::is_base2(text_cpp));
}

kth_bool_t kth_core_binary_is_prefix_of_span(kth_binary_const_t self, uint8_t const* field, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(field != nullptr || n == 0);
    auto const field_cpp = kth::byte_span(field, static_cast<size_t>(n));
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self).is_prefix_of(field_cpp));
}

kth_bool_t kth_core_binary_is_prefix_of_uint32(kth_binary_const_t self, uint32_t field) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self).is_prefix_of(field));
}

kth_bool_t kth_core_binary_is_prefix_of_binary(kth_binary_const_t self, kth_binary_const_t field) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(field != nullptr);
    auto const& field_cpp = kth::cpp_ref<kth::binary>(field);
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self).is_prefix_of(field_cpp));
}


// Operations

void kth_core_binary_resize(kth_binary_mut_t self, kth_size_t size) {
    KTH_PRECONDITION(self != nullptr);
    auto const size_cpp = static_cast<size_t>(size);
    kth::cpp_ref<kth::binary>(self).resize(size_cpp);
}

kth_bool_t kth_core_binary_at(kth_binary_const_t self, kth_size_t index) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(index < kth::cpp_ref<kth::binary>(self).size());
    auto const index_cpp = static_cast<size_t>(index);
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self).operator[](index_cpp));
}

void kth_core_binary_append(kth_binary_mut_t self, kth_binary_const_t post) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(post != nullptr);
    auto const& post_cpp = kth::cpp_ref<kth::binary>(post);
    kth::cpp_ref<kth::binary>(self).append(post_cpp);
}

void kth_core_binary_prepend(kth_binary_mut_t self, kth_binary_const_t prior) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(prior != nullptr);
    auto const& prior_cpp = kth::cpp_ref<kth::binary>(prior);
    kth::cpp_ref<kth::binary>(self).prepend(prior_cpp);
}

void kth_core_binary_shift_left(kth_binary_mut_t self, kth_size_t distance) {
    KTH_PRECONDITION(self != nullptr);
    auto const distance_cpp = static_cast<size_t>(distance);
    kth::cpp_ref<kth::binary>(self).shift_left(distance_cpp);
}

void kth_core_binary_shift_right(kth_binary_mut_t self, kth_size_t distance) {
    KTH_PRECONDITION(self != nullptr);
    auto const distance_cpp = static_cast<size_t>(distance);
    kth::cpp_ref<kth::binary>(self).shift_right(distance_cpp);
}

kth_binary_mut_t kth_core_binary_substring(kth_binary_const_t self, kth_size_t start, kth_size_t length) {
    KTH_PRECONDITION(self != nullptr);
    auto const start_cpp = static_cast<size_t>(start);
    auto const length_cpp = static_cast<size_t>(length);
    return kth::make_leaked_if_valid(kth::cpp_ref<kth::binary>(self).substring(start_cpp, length_cpp));
}

kth_bool_t kth_core_binary_less(kth_binary_const_t self, kth_binary_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<kth::binary>(x);
    return kth::bool_to_int(kth::cpp_ref<kth::binary>(self).operator<(x_cpp));
}


// Static utilities

kth_size_t kth_core_binary_blocks_size(kth_size_t bit_size) {
    auto const bit_size_cpp = static_cast<size_t>(bit_size);
    return kth::binary::blocks_size(bit_size_cpp);
}

} // extern "C"
