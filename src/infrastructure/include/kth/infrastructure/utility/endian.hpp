// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ENDIAN_HPP
#define KTH_INFRASTRUCTURE_ENDIAN_HPP

#include <bit>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

template <typename Integer, typename Iterator>
constexpr
Integer from_big_endian(Iterator start, Iterator end);

template <typename Integer, typename Iterator>
constexpr
Integer from_little_endian(Iterator start, Iterator end);

template <typename Integer, typename Iterator>
constexpr
Integer from_big_endian_unsafe(Iterator start);

template <typename Integer, typename Iterator>
constexpr
Integer from_little_endian_unsafe(Iterator start);

template <typename Integer>
byte_array<sizeof(Integer)> to_big_endian(Integer value);

template <typename Integer>
byte_array<sizeof(Integer)> to_little_endian(Integer value);

constexpr 
uint32_t to_big_endian_int(uint32_t value) {
    if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(value);  // C++23
    } else {
        return value;
    }
}

} // namespace kth

#include <kth/infrastructure/impl/utility/endian.ipp>

#endif
