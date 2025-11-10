// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_LIMITS_HPP
#define KTH_INFRASTRUCTURE_LIMITS_HPP

#include <algorithm>
#include <concepts>
// #include <expected>
#include <limits>
#include <stdexcept>

#include <expected>

#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

using std::expected;
using std::unexpected;


template <typename Space, typename I>
Space cast_add(I left, I right) {
    return static_cast<Space>(left) + static_cast<Space>(right);
}

template <typename Space, typename I>
Space cast_subtract(I left, I right) {
    return static_cast<Space>(left) - static_cast<Space>(right);
}

template <std::unsigned_integral I>
I ceiling_add(I left, I right) {
    static auto const ceiling = std::numeric_limits<I>::max();
    return left > ceiling - right ? ceiling : left + right;
}

template <std::unsigned_integral I>
I floor_subtract(I left, I right) {
    static auto const floor = (std::numeric_limits<I>::min)();
    return right >= left ? floor : left - right;
}

//TODO(fernando): Implement safe operations without exceptions

// #if ! defined(__EMSCRIPTEN__)

// #define IF(T) std::enable_if<T>
// #define SIGN(T) std::is_signed<T>::value
// #define UNSIGN(T) std::is_unsigned<T>::value

// #define SIGNED(A) IF(SIGN(A))
// #define UNSIGNED(A) IF(UNSIGN(A))
// #define SIGNED_SIGNED(A, B) IF(SIGN(A) && SIGN(B))
// #define SIGNED_UNSIGNED(A, B) IF(SIGN(A) && UNSIGN(B))
// #define UNSIGNED_SIGNED(A, B) IF(UNSIGN(A) && SIGN(B))
// #define UNSIGNED_UNSIGNED(A, B) IF(UNSIGN(A) && UNSIGN(B))


// template <typename I, typename = UNSIGNED(I)>
// I safe_add(I left, I right) {
//     static auto const maximum = (std::numeric_limits<I>::max)();

//     if (left > maximum - right) {
//         throw std::overflow_error("addition overflow");
//     }

//     return left + right;
// }

// template <typename I, typename = UNSIGNED(I)>
// I safe_subtract(I left, I right) {
//     static auto const minimum = (std::numeric_limits<I>::min)();

//     if (left < minimum + right) {
//         throw std::underflow_error("subtraction underflow");
//     }

//     return left - right;
// }

// template <typename To, typename From, typename = SIGNED_SIGNED(To, From)>
// To safe_signed(From signed_value) {
//     static auto const signed_minimum = (std::numeric_limits<To>::min)();
//     static auto const signed_maximum = (std::numeric_limits<To>::max)();

//     if (signed_value < signed_minimum || signed_value > signed_maximum) {
//         throw std::range_error("signed assignment out of range");
//     }

//     return static_cast<To>(signed_value);
// }

// template <typename To, typename From, typename = UNSIGNED_UNSIGNED(To, From)>
// To safe_unsigned(From unsigned_value) {
//     static auto const unsigned_minimum = (std::numeric_limits<To>::min)();
//     static auto const unsigned_maximum = (std::numeric_limits<To>::max)();

//     if (unsigned_value < unsigned_minimum || unsigned_value > unsigned_maximum) {
//         throw std::range_error("unsigned assignment out of range");
//     }

//     return static_cast<To>(unsigned_value);
// }

// template <typename To, typename From, typename = SIGNED_UNSIGNED(To, From)>
// To safe_to_signed(From unsigned_value) {
//     static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
//     static auto const signed_maximum = (std::numeric_limits<To>::max)();

//     if (unsigned_value > static_cast<uint64_t>(signed_maximum)) {
//         throw std::range_error("to signed assignment out of range");
//     }

//     return static_cast<To>(unsigned_value);
// }

// template <typename To, typename From, typename = UNSIGNED_SIGNED(To, From)>
// To safe_to_unsigned(From signed_value) {
//     static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
//     static auto const unsigned_maximum = (std::numeric_limits<To>::max)();

//     if (signed_value < 0 || static_cast<uint64_t>(signed_value) > unsigned_maximum) {
//         throw std::range_error("to unsigned assignment out of range");
//     }

//     return static_cast<To>(signed_value);
// }

// #undef IF
// #undef SIGN
// #undef UNSIGN
// #undef SIGNED
// #undef UNSIGNED
// #undef SIGNED_SIGNED
// #undef SIGNED_UNSIGNED
// #undef UNSIGNED_SIGNED
// #undef UNSIGNED_UNSIGNED

// #else

template <std::unsigned_integral I>
expected<I, code> safe_add(I left, I right) {
    static auto const maximum = std::numeric_limits<I>::max();

    if (left > maximum - right) {
        return std::unexpected(error::overflow);
    }
    return left + right;
}

template <std::unsigned_integral I>
expected<I, code> safe_subtract(I left, I right) {
    static auto const minimum = (std::numeric_limits<I>::min)();

    if (left < minimum + right) {
        return std::unexpected(error::underflow);
    }

    return left - right;
}

template <std::signed_integral To, std::signed_integral From>
expected<To, code> safe_signed(From signed_value) {
    static auto const signed_minimum = (std::numeric_limits<To>::min)();
    static auto const signed_maximum = (std::numeric_limits<To>::max)();

    if (signed_value < signed_minimum || signed_value > signed_maximum) {
        return std::unexpected(error::out_of_range);
    }

    return static_cast<To>(signed_value);
}

template <std::unsigned_integral To, std::unsigned_integral From>
expected<To, code> safe_unsigned(From unsigned_value) {
    static auto const unsigned_minimum = (std::numeric_limits<To>::min)();
    static auto const unsigned_maximum = (std::numeric_limits<To>::max)();

    if (unsigned_value < unsigned_minimum || unsigned_value > unsigned_maximum) {
        return std::unexpected(error::out_of_range);
    }

    return static_cast<To>(unsigned_value);
}

template <std::signed_integral To, std::unsigned_integral From>
expected<To, code> safe_to_signed(From unsigned_value) {
    static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
    static auto const signed_maximum = (std::numeric_limits<To>::max)();

    if (unsigned_value > static_cast<uint64_t>(signed_maximum)) {
        return std::unexpected(error::out_of_range);
    }

    return static_cast<To>(unsigned_value);
}

template <std::unsigned_integral To, std::signed_integral From>
expected<To, code> safe_to_unsigned(From signed_value) {
    static_assert(sizeof(uint64_t) >= sizeof(To), "safe assign out of range");
    static auto const unsigned_maximum = (std::numeric_limits<To>::max());

    if (signed_value < 0 || static_cast<uint64_t>(signed_value) > unsigned_maximum) {
        return std::unexpected(error::out_of_range);
    }

    return static_cast<To>(signed_value);
}

// #endif // ! defined(__EMSCRIPTEN__)

// template <typename I>
// void safe_increment(I& value) {
//     static constexpr auto one = I{1};
//     //TODO: how to return the expected value?
//     value = *safe_add(value, one);
// }

// template <typename I>
// void safe_decrement(I& value) {
//     static constexpr auto one = I{1};
//     value = *safe_subtract(value, one);
// }

/// Constrain a numeric value within a given type domain.
template <typename To, typename From>
To domain_constrain(From value) {
    static auto const minimum = (std::numeric_limits<To>::min)();
    static auto const maximum = (std::numeric_limits<To>::max)();

    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return static_cast<To>(value);
}

/// Constrain a numeric value within a given range.
template <typename To, typename From>
To range_constrain(From value, To minimum, To maximum) {
    if (value < minimum) {
        return minimum;
    }

    if (value > maximum) {
        return maximum;
    }

    return static_cast<To>(value);
}

} // namespace kth

#endif
