// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string_view>

#include <kth/capi/vm/big_number.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/machine/big_number.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::infrastructure::machine::big_number;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_big_number_mut_t kth_vm_big_number_construct_default(void) {
    return kth::leak<cpp_t>();
}

kth_big_number_mut_t kth_vm_big_number_construct_from_value(int64_t value) {
    return kth::leak<cpp_t>(value);
}

kth_big_number_mut_t kth_vm_big_number_construct_from_decimal_str(char const* decimal_str) {
    KTH_PRECONDITION(decimal_str != nullptr);
    auto const decimal_str_cpp = std::string_view(decimal_str);
    return kth::leak<cpp_t>(decimal_str_cpp);
}


// Static factories

kth_big_number_mut_t kth_vm_big_number_from_hex(char const* hex_str) {
    KTH_PRECONDITION(hex_str != nullptr);
    auto const hex_str_cpp = std::string_view(hex_str);
    return kth::leak(cpp_t::from_hex(hex_str_cpp));
}


// Destructor

void kth_vm_big_number_destruct(kth_big_number_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_big_number_mut_t kth_vm_big_number_copy(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_vm_big_number_equals(kth_big_number_const_t self, kth_big_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Getters

uint8_t* kth_vm_big_number_serialize(kth_big_number_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).serialize();
    return kth::create_c_array(data, *out_size);
}

char* kth_vm_big_number_to_string(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_string();
    return kth::create_c_str(s);
}

char* kth_vm_big_number_to_hex(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    auto const s = kth::cpp_ref<cpp_t>(self).to_hex();
    return kth::create_c_str(s);
}

int kth_vm_big_number_sign(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).sign();
}

int32_t kth_vm_big_number_to_int32_saturating(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).to_int32_saturating();
}

kth_size_t kth_vm_big_number_byte_count(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).byte_count();
}

kth_big_number_mut_t kth_vm_big_number_abs(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::leak(kth::cpp_ref<cpp_t>(self).abs());
}

uint8_t* kth_vm_big_number_data(kth_big_number_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).data();
    return kth::create_c_array(data, *out_size);
}


// Setters

kth_bool_t kth_vm_big_number_set_data(kth_big_number_mut_t self, uint8_t const* d, kth_size_t n, kth_size_t max_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(d != nullptr || n == 0);
    auto const d_cpp = n != 0 ? kth::data_chunk(d, d + n) : kth::data_chunk{};
    auto const max_size_cpp = kth::sz(max_size);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_data(d_cpp, max_size_cpp));
}


// Predicates

kth_bool_t kth_vm_big_number_is_zero(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_zero());
}

kth_bool_t kth_vm_big_number_is_nonzero(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_nonzero());
}

kth_bool_t kth_vm_big_number_is_negative(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_negative());
}

kth_bool_t kth_vm_big_number_is_true(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_true());
}

kth_bool_t kth_vm_big_number_is_false(kth_big_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_false());
}

kth_bool_t kth_vm_big_number_is_minimally_encoded(uint8_t const* data, kth_size_t n, kth_size_t max_size) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    auto const max_size_cpp = kth::sz(max_size);
    return kth::bool_to_int(cpp_t::is_minimally_encoded(data_cpp, max_size_cpp));
}


// Operations

kth_bool_t kth_vm_big_number_deserialize(kth_big_number_mut_t self, uint8_t const* data, kth_size_t n) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).deserialize(data_cpp));
}

int kth_vm_big_number_compare(kth_big_number_const_t self, kth_big_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    auto const& other_cpp = kth::cpp_ref<cpp_t>(other);
    return kth::cpp_ref<cpp_t>(self).compare(other_cpp);
}

kth_big_number_mut_t kth_vm_big_number_add(kth_big_number_const_t self, kth_big_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    auto const& other_cpp = kth::cpp_ref<cpp_t>(other);
    return kth::leak(kth::cpp_ref<cpp_t>(self).operator+(other_cpp));
}

kth_big_number_mut_t kth_vm_big_number_subtract(kth_big_number_const_t self, kth_big_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    auto const& other_cpp = kth::cpp_ref<cpp_t>(other);
    return kth::leak(kth::cpp_ref<cpp_t>(self).operator-(other_cpp));
}

kth_big_number_mut_t kth_vm_big_number_multiply(kth_big_number_const_t self, kth_big_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    auto const& other_cpp = kth::cpp_ref<cpp_t>(other);
    return kth::leak(kth::cpp_ref<cpp_t>(self).operator*(other_cpp));
}

void kth_vm_big_number_negate(kth_big_number_mut_t self) {
    KTH_PRECONDITION(self != nullptr);
    kth::cpp_ref<cpp_t>(self).negate();
}

kth_big_number_mut_t kth_vm_big_number_pow(kth_big_number_const_t self, kth_big_number_const_t exp) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(exp != nullptr);
    auto const& exp_cpp = kth::cpp_ref<cpp_t>(exp);
    return kth::leak(kth::cpp_ref<cpp_t>(self).pow(exp_cpp));
}

kth_big_number_mut_t kth_vm_big_number_pow_mod(kth_big_number_const_t self, kth_big_number_const_t exp, kth_big_number_const_t mod) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(exp != nullptr);
    KTH_PRECONDITION(mod != nullptr);
    auto const& exp_cpp = kth::cpp_ref<cpp_t>(exp);
    auto const& mod_cpp = kth::cpp_ref<cpp_t>(mod);
    return kth::leak(kth::cpp_ref<cpp_t>(self).pow_mod(exp_cpp, mod_cpp));
}

kth_big_number_mut_t kth_vm_big_number_math_modulo(kth_big_number_const_t self, kth_big_number_const_t mod) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(mod != nullptr);
    auto const& mod_cpp = kth::cpp_ref<cpp_t>(mod);
    return kth::leak(kth::cpp_ref<cpp_t>(self).math_modulo(mod_cpp));
}

} // extern "C"
