// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_LIMITS_HPP
#define KTH_LIMITS_HPP

#include <algorithm>
#include <concepts>
#include <limits>
#include <stdexcept>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth::domain {

template <typename Space, typename Integer>
Space cast_add(Integer left, Integer right) {
    return static_cast<Space>(left) + static_cast<Space>(right);
}

template <typename Space, typename Integer>
Space cast_subtract(Integer left, Integer right) {
    return static_cast<Space>(left) - static_cast<Space>(right);
}

template <std::unsigned_integral Integer>
Integer ceiling_add(Integer left, Integer right) {
    static auto const ceiling = (std::numeric_limits<Integer>::max)();
    return left > ceiling - right ? ceiling : left + right;
}

template <std::unsigned_integral Integer>
Integer floor_subtract(Integer left, Integer right) {
    static auto const floor = (std::numeric_limits<Integer>::min)();
    return right >= left ? floor : left - right;
}

template <std::unsigned_integral Integer>
Integer safe_add(Integer left, Integer right) {
    static auto const maximum = (std::numeric_limits<Integer>::max)();

    if (left > maximum - right)
        throw std::overflow_error("addition overflow");

    return left + right;
}

template <std::unsigned_integral Integer>
Integer safe_subtract(Integer left, Integer right) {
    static auto const minimum = (std::numeric_limits<Integer>::min)();

    if (left < minimum + right)
        throw std::underflow_error("subtraction underflow");

    return left - right;
}

template <std::unsigned_integral Integer>
void safe_increment(Integer& value) {
    static constexpr auto one = Integer{1};
    value = safe_add(value, one);
}

template <std::unsigned_integral Integer>
void safe_decrement(Integer& value) {
    static constexpr auto one = Integer{1};
    value = safe_subtract(value, one);
}

template <typename To, typename From>
    requires std::signed_integral<To> && std::signed_integral<From>
To safe_signed(From signed_value) {
    static auto const signed_minimum = (std::numeric_limits<To>::min)();
    static auto const signed_maximum = (std::numeric_limits<To>::max)();

    if (signed_value < signed_minimum || signed_value > signed_maximum)
        throw std::range_error("signed assignment out of range");

    return static_cast<To>(signed_value);
}

template <typename To, typename From>
    requires std::unsigned_integral<To> && std::unsigned_integral<From>
To safe_unsigned(From unsigned_value) {
    static auto const unsigned_minimum = (std::numeric_limits<To>::min)();
    static auto const unsigned_maximum = (std::numeric_limits<To>::max)();

    if (unsigned_value < unsigned_minimum || unsigned_value > unsigned_maximum)
        throw std::range_error("unsigned assignment out of range");

    return static_cast<To>(unsigned_value);
}

template <typename To, typename From>
    requires std::signed_integral<To> && std::unsigned_integral<From>
To safe_to_signed(From unsigned_value) {
    static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
    static auto const signed_maximum = (std::numeric_limits<To>::max)();

    if (unsigned_value > uint64_t(signed_maximum))
        throw std::range_error("to signed assignment out of range");

    return static_cast<To>(unsigned_value);
}

template <typename To, typename From>
    requires std::unsigned_integral<To> && std::signed_integral<From>
To safe_to_unsigned(From signed_value) {
    static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
    static auto const unsigned_maximum = (std::numeric_limits<To>::max)();

    if (signed_value < 0 ||
        uint64_t(signed_value) > unsigned_maximum)
        throw std::range_error("to unsigned assignment out of range");

    return static_cast<To>(signed_value);
}

/// Constrain a numeric value within a given type domain.
template <typename To, typename From>
To domain_constrain(From value) {
    static auto const minimum = (std::numeric_limits<To>::min)();
    static auto const maximum = (std::numeric_limits<To>::max)();

    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return static_cast<To>(value);
}

/// Constrain a numeric value within a given range.
template <typename To, typename From>
To range_constrain(From value, To minimum, To maximum) {
    if (value < minimum)
        return minimum;

    if (value > maximum)
        return maximum;

    return static_cast<To>(value);
}

} // namespace kth::domain

#endif
