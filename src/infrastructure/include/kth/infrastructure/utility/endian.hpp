// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ENDIAN_HPP
#define KTH_INFRASTRUCTURE_ENDIAN_HPP

#include <bit>
#include <concepts>
#include <cstdint>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Concept for types that support endian conversion operations
// Includes std::unsigned_integral and boost::multiprecision types like uint256_t
// Not used yet.
template <typename T>
concept unsigned_integer = std::unsigned_integral<T> ||
    requires(T a, uint8_t b, size_t shift) {
        { T(0) };                       // default constructible from 0
        { T(b) } -> std::same_as<T>;    // constructible from uint8_t
        { a | T{} } -> std::same_as<T>; // supports bitwise or
        { T{} << shift };               // supports left shift
    };

// ============================================================================
// to_* functions (integer → byte array)
// ============================================================================

template <std::endian Endian, unsigned_integer Int>
constexpr
byte_array<sizeof(Int)> to_endian(Int value) {
    if constexpr (std::endian::native != Endian) {
        value = std::byteswap(value);
    }
    return std::bit_cast<byte_array<sizeof(Int)>>(value);
}

template <std::unsigned_integral Int>
constexpr
byte_array<sizeof(Int)> to_little_endian(Int value) {
    return to_endian<std::endian::little>(value);
}

template <std::unsigned_integral Int>
constexpr
byte_array<sizeof(Int)> to_big_endian(Int value) {
    return to_endian<std::endian::big>(value);
}

// ============================================================================
// from_* functions (fixed-size byte span → integer)
// ============================================================================

// Optimized version for contiguous memory with compile-time size guarantee.
// GCC and Clang optimize this to a single `mov` instruction (+ `bswap` if byte swap is needed).
template <std::endian Endian, std::unsigned_integral Int>
constexpr
Int from_endian(std::span<uint8_t const, sizeof(Int)> data) {
    byte_array<sizeof(Int)> arr;
    std::copy_n(data.data(), sizeof(Int), arr.begin());
    Int value = std::bit_cast<Int>(arr);
    if constexpr (std::endian::native != Endian) {
        value = std::byteswap(value);
    }
    return value;
}

template <std::unsigned_integral Int>
constexpr
Int from_little_endian(std::span<uint8_t const, sizeof(Int)> data) {
    return from_endian<std::endian::little, Int>(data);
}

template <std::unsigned_integral Int>
constexpr
Int from_big_endian(std::span<uint8_t const, sizeof(Int)> data) {
    return from_endian<std::endian::big, Int>(data);
}

// ============================================================================
// from_*_unsafe functions (dynamic byte span → integer)
// ============================================================================

// Unsafe versions: caller must ensure data.size() >= sizeof(Int)
template <std::unsigned_integral Int>
constexpr
Int from_little_endian_unsafe(byte_span data) {
    // precondition: data.size() >= sizeof(Int) //TODO: replace with C++26? Contracts
    return from_endian<std::endian::little, Int>(
        std::span<uint8_t const, sizeof(Int)>(data.data(), sizeof(Int)));
}

template <std::unsigned_integral Int>
constexpr
Int from_big_endian_unsafe(byte_span data) {
    // precondition: data.size() >= sizeof(Int) //TODO: replace with C++26? Contracts
    return from_endian<std::endian::big, Int>(
        std::span<uint8_t const, sizeof(Int)>(data.data(), sizeof(Int)));
}

} // namespace kth

#endif
