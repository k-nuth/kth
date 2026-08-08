// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/math/limits.hpp>

#include <concepts>
#include <cstdint>

namespace {

namespace kd = kth::domain;

template <typename Integer>
concept ceiling_add_invocable = requires(Integer value) {
    kd::ceiling_add(value, value);
};

template <typename Integer>
concept floor_subtract_invocable = requires(Integer value) {
    kd::floor_subtract(value, value);
};

template <typename Integer>
concept safe_add_invocable = requires(Integer value) {
    kd::safe_add(value, value);
};

template <typename Integer>
concept safe_subtract_invocable = requires(Integer value) {
    kd::safe_subtract(value, value);
};

template <typename Integer>
concept safe_increment_invocable = requires(Integer value) {
    kd::safe_increment(value);
};

template <typename Integer>
concept safe_decrement_invocable = requires(Integer value) {
    kd::safe_decrement(value);
};

template <typename To, typename From>
concept safe_signed_invocable = requires(From value) {
    kd::safe_signed<To, From>(value);
};

template <typename To, typename From>
concept safe_unsigned_invocable = requires(From value) {
    kd::safe_unsigned<To, From>(value);
};

template <typename To, typename From>
concept safe_to_signed_invocable = requires(From value) {
    kd::safe_to_signed<To, From>(value);
};

template <typename To, typename From>
concept safe_to_unsigned_invocable = requires(From value) {
    kd::safe_to_unsigned<To, From>(value);
};

static_assert(ceiling_add_invocable<uint32_t>);
static_assert( ! ceiling_add_invocable<int32_t>);
static_assert(floor_subtract_invocable<uint32_t>);
static_assert( ! floor_subtract_invocable<int32_t>);
static_assert(safe_add_invocable<uint32_t>);
static_assert( ! safe_add_invocable<int32_t>);
static_assert(safe_subtract_invocable<uint32_t>);
static_assert( ! safe_subtract_invocable<int32_t>);
static_assert(safe_increment_invocable<uint32_t>);
static_assert( ! safe_increment_invocable<int32_t>);
static_assert(safe_decrement_invocable<uint32_t>);
static_assert( ! safe_decrement_invocable<int32_t>);

static_assert(safe_signed_invocable<int32_t, int64_t>);
static_assert( ! safe_signed_invocable<uint32_t, int64_t>);
static_assert( ! safe_signed_invocable<int32_t, uint64_t>);

static_assert(safe_unsigned_invocable<uint32_t, uint64_t>);
static_assert( ! safe_unsigned_invocable<int32_t, uint64_t>);
static_assert( ! safe_unsigned_invocable<uint32_t, int64_t>);

static_assert(safe_to_signed_invocable<int32_t, uint64_t>);
static_assert( ! safe_to_signed_invocable<uint32_t, uint64_t>);
static_assert( ! safe_to_signed_invocable<int32_t, int64_t>);

static_assert(safe_to_unsigned_invocable<uint32_t, int64_t>);
static_assert( ! safe_to_unsigned_invocable<int32_t, int64_t>);
static_assert( ! safe_to_unsigned_invocable<uint32_t, uint64_t>);

} // namespace
