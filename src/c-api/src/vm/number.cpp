// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstring>
#include <utility>

#include <kth/capi/vm/number.h>

#include <kth/capi/conversions.hpp>
#include <kth/capi/helpers.hpp>
#include <kth/infrastructure/machine/number.hpp>

// File-local alias so `kth::cpp_ref<T>(...)` and friends don't
// spell out the full qualified C++ name at every call site.
namespace {
using cpp_t = kth::infrastructure::machine::number;
} // namespace

// ---------------------------------------------------------------------------
extern "C" {

// Constructors

kth_number_mut_t kth_vm_number_construct_default(void) {
    return kth::leak<cpp_t>();
}


// Destructor

void kth_vm_number_destruct(kth_number_mut_t self) {
    kth::del<cpp_t>(self);
}


// Copy

kth_number_mut_t kth_vm_number_copy(kth_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::clone<cpp_t>(self);
}


// Equality

kth_bool_t kth_vm_number_equals(kth_number_const_t self, kth_number_const_t other) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(other != nullptr);
    return kth::eq<cpp_t>(self, other);
}


// Getters

uint8_t* kth_vm_number_data(kth_number_const_t self, kth_size_t* out_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(out_size != nullptr);
    auto const data = kth::cpp_ref<cpp_t>(self).data();
    return kth::create_c_array(data, *out_size);
}

int32_t kth_vm_number_int32(kth_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).int32();
}

int64_t kth_vm_number_int64(kth_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::cpp_ref<cpp_t>(self).int64();
}


// Setters

kth_bool_t kth_vm_number_set_data(kth_number_mut_t self, uint8_t const* data, kth_size_t n, kth_size_t max_size) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    auto const max_size_cpp = kth::sz(max_size);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).set_data(data_cpp, max_size_cpp));
}


// Predicates

kth_bool_t kth_vm_number_is_true(kth_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_true());
}

kth_bool_t kth_vm_number_is_false(kth_number_const_t self) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).is_false());
}

kth_bool_t kth_vm_number_is_minimally_encoded(uint8_t const* data, kth_size_t n, kth_size_t max_integer_size) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    auto const data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    auto const max_integer_size_cpp = kth::sz(max_integer_size);
    return kth::bool_to_int(cpp_t::is_minimally_encoded(data_cpp, max_integer_size_cpp));
}


// Operations

kth_bool_t kth_vm_number_valid(kth_number_mut_t self, kth_size_t max_size) {
    KTH_PRECONDITION(self != nullptr);
    auto const max_size_cpp = kth::sz(max_size);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).valid(max_size_cpp));
}

kth_bool_t kth_vm_number_greater(kth_number_const_t self, int64_t value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).operator>(value));
}

kth_bool_t kth_vm_number_less(kth_number_const_t self, int64_t value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).operator<(value));
}

kth_bool_t kth_vm_number_greater_or_equal(kth_number_const_t self, int64_t value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).operator>=(value));
}

kth_bool_t kth_vm_number_less_or_equal(kth_number_const_t self, int64_t value) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).operator<=(value));
}

kth_bool_t kth_vm_number_safe_add_number(kth_number_mut_t self, kth_number_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_add(x_cpp));
}

kth_bool_t kth_vm_number_safe_add_int64(kth_number_mut_t self, int64_t x) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_add(x));
}

kth_bool_t kth_vm_number_safe_sub_number(kth_number_mut_t self, kth_number_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_sub(x_cpp));
}

kth_bool_t kth_vm_number_safe_sub_int64(kth_number_mut_t self, int64_t x) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_sub(x));
}

kth_bool_t kth_vm_number_safe_mul_number(kth_number_mut_t self, kth_number_const_t x) {
    KTH_PRECONDITION(self != nullptr);
    KTH_PRECONDITION(x != nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_mul(x_cpp));
}

kth_bool_t kth_vm_number_safe_mul_int64(kth_number_mut_t self, int64_t x) {
    KTH_PRECONDITION(self != nullptr);
    return kth::bool_to_int(kth::cpp_ref<cpp_t>(self).safe_mul(x));
}


// Static utilities

kth_error_code_t kth_vm_number_from_int(int64_t value, KTH_OUT_OWNED kth_number_mut_t* out) {
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto result = cpp_t::from_int(value);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_vm_number_safe_add_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out) {
    KTH_PRECONDITION(x != nullptr);
    KTH_PRECONDITION(y != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    auto const& y_cpp = kth::cpp_ref<cpp_t>(y);
    auto result = cpp_t::safe_add(x_cpp, y_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_vm_number_safe_sub_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out) {
    KTH_PRECONDITION(x != nullptr);
    KTH_PRECONDITION(y != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    auto const& y_cpp = kth::cpp_ref<cpp_t>(y);
    auto result = cpp_t::safe_sub(x_cpp, y_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_error_code_t kth_vm_number_safe_mul_number2(kth_number_const_t x, kth_number_const_t y, KTH_OUT_OWNED kth_number_mut_t* out) {
    KTH_PRECONDITION(x != nullptr);
    KTH_PRECONDITION(y != nullptr);
    KTH_PRECONDITION(out != nullptr);
    KTH_PRECONDITION(*out == nullptr);
    auto const& x_cpp = kth::cpp_ref<cpp_t>(x);
    auto const& y_cpp = kth::cpp_ref<cpp_t>(y);
    auto result = cpp_t::safe_mul(x_cpp, y_cpp);
    if ( ! result) return kth::to_c_err(result.error());
    *out = kth::leak(std::move(*result));
    return kth_ec_success;
}

kth_size_t kth_vm_number_minimally_encode(uint8_t* data, kth_size_t n) {
    KTH_PRECONDITION(data != nullptr || n == 0);
    kth::data_chunk data_cpp = n != 0 ? kth::data_chunk(data, data + n) : kth::data_chunk{};
    (void)cpp_t::minimally_encode(data_cpp);
    auto const required = data_cpp.size();
    if (kth::sz(n) >= required && required > 0) std::memcpy(data, data_cpp.data(), required);
    return static_cast<kth_size_t>(required);
}

} // extern "C"
