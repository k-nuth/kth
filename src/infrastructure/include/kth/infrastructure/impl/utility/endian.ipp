// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ENDIAN_IPP
#define KTH_INFRASTRUCTURE_ENDIAN_IPP

#include <type_traits>

namespace kth {

#define VERIFY_UNSIGNED(T) static_assert(std::is_unsigned<T>::value, \
    "The endian functions only work on unsigned types")

template <typename Integer, typename Iterator>
constexpr
Integer from_big_endian(Iterator start, Iterator end) {
    VERIFY_UNSIGNED(Integer);
    Integer out = 0;
    size_t i = sizeof(Integer);

    while (0 < i && start != end) {
        out |= static_cast<Integer>(*start++) << (8 * --i);
    }

    return out;
}

template <typename Integer, typename Iterator>
constexpr
Integer from_little_endian(Iterator start, Iterator end) {
    //// TODO: Type traits does not work for uint256_t.
    ////VERIFY_UNSIGNED(Integer);
    Integer out = 0;
    size_t i = 0;

    while (i < sizeof(Integer) && start != end) {
        out |= static_cast<Integer>(*start++) << (8 * i++);
    }

    return out;
}

template <typename Integer, typename Iterator>
constexpr
Integer from_big_endian_unsafe(Iterator start) {
    VERIFY_UNSIGNED(Integer);
    Integer out = 0;
    size_t i = sizeof(Integer);

    while (0 < i) {
        out |= static_cast<Integer>(*start++) << (8 * --i);
    }

    return out;
}

template <typename Integer, typename Iterator>
constexpr
Integer from_little_endian_unsafe(Iterator start) {
    VERIFY_UNSIGNED(Integer);
    Integer out = 0;
    size_t i = 0;

    while (i < sizeof(Integer)) {
        out |= static_cast<Integer>(*start++) << (8 * i++);
    }

    return out;
}

template <typename Integer>
byte_array<sizeof(Integer)> to_big_endian(Integer value) {
    VERIFY_UNSIGNED(Integer);
    byte_array<sizeof(Integer)> out;

    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        *it = static_cast<uint8_t>(value);
        value >>= 8;
    }

    return out;
}

template <typename Integer>
byte_array<sizeof(Integer)> to_little_endian(Integer value) {
    VERIFY_UNSIGNED(Integer);
    byte_array<sizeof(Integer)> out;

    for (auto it = out.begin(); it != out.end(); ++it) {
        *it = static_cast<uint8_t>(value);
        value >>= 8;
    }

    return out;
}

#undef VERIFY_UNSIGNED

} // namespace kth

#endif

