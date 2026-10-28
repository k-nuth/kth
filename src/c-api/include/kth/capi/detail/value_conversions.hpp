// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CAPI_DETAIL_VALUE_CONVERSIONS_HPP_
#define KTH_CAPI_DETAIL_VALUE_CONVERSIONS_HPP_

// Generic algorithms for crossing fixed-size values between C and C++. Nothing
// here names a concrete type: the per-type wrappers built on these templates
// are generated into `kth/capi/conversions/`.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <tuple>
#include <type_traits>

namespace kth {

// A type that has an explicit field-by-field conversion opts out of the byte
// copy of `struct_cast`, on both sides of the crossing. Specialize it next to
// that conversion; the generic path then refuses the type instead of silently
// reintroducing the coupling the explicit conversion exists to remove.
template<typename T>
struct forbids_struct_cast : std::false_type {};

// Generic helper for C-compatible POD struct conversions.
// Uses memcpy (standard-compliant, zero overhead — compilers optimize
// small struct memcpy to register moves). static_assert guards that the
// source and destination structs have the same size.
// Works both directions: C→C++ and C++→C.
template<typename To, typename From>
inline To struct_cast(From const& src) {
    static_assert( ! forbids_struct_cast<To>::value && ! forbids_struct_cast<From>::value,
                  "this type crosses field by field; the memcpy conversion would put its "
                  "layout, padding and declaration order back into the ABI");
    static_assert(sizeof(To) == sizeof(From),
                  "C and C++ struct sizes must match for memcpy conversion");
    static_assert(std::is_trivially_copyable_v<To> && std::is_trivially_copyable_v<From>,
                  "both types must be trivially copyable for memcpy conversion");
    static_assert(std::is_standard_layout_v<To> && std::is_standard_layout_v<From>,
                  "both types must be standard layout for layout-compatible conversion");
    To dst;
    std::memcpy(&dst, &src, sizeof(dst));
    return dst;
}

// Convenience aliases for readability in generated code.
template<typename CType, typename CppType>
inline CType to_c_struct(CppType const& cpp) { return struct_cast<CType>(cpp); }

template<typename CppType, typename CType>
inline CppType from_c_struct(CType const& c) { return struct_cast<CppType>(c); }

// Generic helpers for value_struct ↔ C++ array conversions: any fixed-size
// byte array.

// C byte buffer -> `std::array<uint8_t, N>`.
template<std::size_t N>
inline
std::array<uint8_t, N> to_array_cpp(uint8_t const* x) {
    std::array<uint8_t, N> ret;
    std::copy_n(x, N, ret.begin());
    return ret;
}

// `std::array` -> C struct whose only meaningful member is the byte array
// `Member` (a pointer to the array member). The extents must match.
template<typename CType, auto Member, typename Array>
inline CType to_c_value_array(Array const& x) {
    CType ret;
    auto& dst = ret.*Member;
    static_assert(std::extent_v<std::remove_reference_t<decltype(dst)>> == std::tuple_size_v<Array>,
                  "C struct member and C++ array must have the same length");
    std::copy(x.begin(), x.end(), std::begin(dst));
    return ret;
}

template <typename HashCpp, typename HashC>
inline
void copy_c_hash(HashCpp const& in, HashC* out) {
    //precondition: size of out->hash is greater or equal than in.size()
    std::copy_n(in.begin(), in.size(), static_cast<uint8_t*>(out->hash));
}

} // namespace kth

#endif /* KTH_CAPI_DETAIL_VALUE_CONVERSIONS_HPP_ */
