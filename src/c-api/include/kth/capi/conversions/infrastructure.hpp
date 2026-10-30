// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

// Hashes, elliptic-curve points and script versions (kth/infrastructure).

#ifndef KTH_CAPI_CONVERSIONS_INFRASTRUCTURE_HPP_
#define KTH_CAPI_CONVERSIONS_INFRASTRUCTURE_HPP_

#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include <kth/capi/detail/enum_cast.hpp>
#include <kth/capi/detail/value_conversions.hpp>

#include <kth/capi/chain/script_version.h>
#include <kth/capi/handles.h>
#include <kth/capi/wallet/primitives.h>

#include <kth/infrastructure/machine/script_version.hpp>
#include <kth/infrastructure/math/elliptic_curve.hpp>
#include <kth/infrastructure/math/hash.hpp>

namespace kth {

// hash (32 bytes)
inline kth_hash_t to_hash_t(kth::hash_digest const& x)
    noexcept(noexcept(to_c_value_array<kth_hash_t, &kth_hash_t::hash>(x))) {
    return to_c_value_array<kth_hash_t, &kth_hash_t::hash>(x);
}
inline kth::hash_digest hash_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::hash_digest>>(x);
}
static_assert(noexcept(to_c_value_array<kth_hash_t, &kth_hash_t::hash>(std::declval<kth::hash_digest const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::hash_digest>>(std::declval<uint8_t const*>())));

// shorthash (20 bytes)
inline kth_shorthash_t to_shorthash_t(kth::short_hash const& x)
    noexcept(noexcept(to_c_value_array<kth_shorthash_t, &kth_shorthash_t::hash>(x))) {
    return to_c_value_array<kth_shorthash_t, &kth_shorthash_t::hash>(x);
}
inline kth::short_hash short_hash_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::short_hash>>(x);
}
static_assert(noexcept(to_c_value_array<kth_shorthash_t, &kth_shorthash_t::hash>(std::declval<kth::short_hash const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::short_hash>>(std::declval<uint8_t const*>())));

// longhash (64 bytes)
inline kth_longhash_t to_longhash_t(kth::long_hash const& x)
    noexcept(noexcept(to_c_value_array<kth_longhash_t, &kth_longhash_t::hash>(x))) {
    return to_c_value_array<kth_longhash_t, &kth_longhash_t::hash>(x);
}
inline kth::long_hash long_hash_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::long_hash>>(x);
}
static_assert(noexcept(to_c_value_array<kth_longhash_t, &kth_longhash_t::hash>(std::declval<kth::long_hash const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::long_hash>>(std::declval<uint8_t const*>())));

// ec_compressed (33 bytes)
inline kth_ec_compressed_t to_ec_compressed_t(kth::ec_compressed const& x)
    noexcept(noexcept(to_c_value_array<kth_ec_compressed_t, &kth_ec_compressed_t::data>(x))) {
    return to_c_value_array<kth_ec_compressed_t, &kth_ec_compressed_t::data>(x);
}
inline kth::ec_compressed ec_compressed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::ec_compressed>>(x);
}
static_assert(noexcept(to_c_value_array<kth_ec_compressed_t, &kth_ec_compressed_t::data>(std::declval<kth::ec_compressed const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::ec_compressed>>(std::declval<uint8_t const*>())));

// ec_uncompressed (65 bytes)
inline kth_ec_uncompressed_t to_ec_uncompressed_t(kth::ec_uncompressed const& x)
    noexcept(noexcept(to_c_value_array<kth_ec_uncompressed_t, &kth_ec_uncompressed_t::data>(x))) {
    return to_c_value_array<kth_ec_uncompressed_t, &kth_ec_uncompressed_t::data>(x);
}
inline kth::ec_uncompressed ec_uncompressed_to_cpp(uint8_t const* x) noexcept {
    return to_array_cpp<std::tuple_size_v<kth::ec_uncompressed>>(x);
}
static_assert(noexcept(to_c_value_array<kth_ec_uncompressed_t, &kth_ec_uncompressed_t::data>(std::declval<kth::ec_uncompressed const&>())));
static_assert(noexcept(to_array_cpp<std::tuple_size_v<kth::ec_uncompressed>>(std::declval<uint8_t const*>())));

#if ! defined(KTH_CURRENCY_BCH)
// Out-of-range C values map to `unversioned`.
inline constexpr kth::infrastructure::machine::script_version script_version_to_cpp(kth_script_version_t x) noexcept {
    switch (x) {
        case kth_script_version_zero:
            return kth::infrastructure::machine::script_version::zero;
        case kth_script_version_reserved:
            return kth::infrastructure::machine::script_version::reserved;
        default:
        case kth_script_version_unversioned:
            return kth::infrastructure::machine::script_version::unversioned;
    }
}

inline constexpr kth_script_version_t script_version_to_c(kth::infrastructure::machine::script_version x) noexcept {
    return ecast<kth_script_version_t>(x);
}

// Numeric ABI: the C and C++ values of every named enumerator are the
// same integer, checked without going through the conversions.
static_assert(sizeof(std::underlying_type_t<kth::infrastructure::machine::script_version>) <= sizeof(kth_script_version_t));
static_assert(std::cmp_equal(+static_cast<std::underlying_type_t<kth::infrastructure::machine::script_version>>(kth::infrastructure::machine::script_version::zero), +kth_script_version_zero));
static_assert(script_version_to_cpp(kth_script_version_zero) == kth::infrastructure::machine::script_version::zero);
static_assert(std::cmp_equal(+static_cast<std::underlying_type_t<kth::infrastructure::machine::script_version>>(kth::infrastructure::machine::script_version::reserved), +kth_script_version_reserved));
static_assert(script_version_to_cpp(kth_script_version_reserved) == kth::infrastructure::machine::script_version::reserved);
static_assert(std::cmp_equal(+static_cast<std::underlying_type_t<kth::infrastructure::machine::script_version>>(kth::infrastructure::machine::script_version::unversioned), +kth_script_version_unversioned));
static_assert(script_version_to_cpp(kth_script_version_unversioned) == kth::infrastructure::machine::script_version::unversioned);
#endif // ! defined(KTH_CURRENCY_BCH)

} // namespace kth

#endif /* KTH_CAPI_CONVERSIONS_INFRASTRUCTURE_HPP_ */
