// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_ASSUMPTIONS_HPP_
#define KTH_INFRASTRUCTURE_ASSUMPTIONS_HPP_

#include <climits>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// This guards assumptions within the codebase.
static_assert(sizeof(size_t) >= sizeof(uint32_t), "unsupported size_t");

static_assert(sizeof(short) == 2, "16-bit short assumed");      //NOLINT
static_assert(sizeof(int) == 4, "32-bit int assumed");

// We assume 8-bit bytes, because 32-bit int and 16-bit short are assumed.
static_assert(CHAR_BIT == 8, "8-bit bytes assumed");

// We assume uint8_t is an alias of unsigned char.
// char, unsigned char, and std::byte (C++17) are the only "byte types"
// according to the C++ Standard. "byte type" means a type that can be used to
// observe an object's value representation. We use uint8_t everywhere to see
// bytes, so we have to ensure that uint8_t is an alias to a "byte type".
// http://eel.is/c++draft/basic.types
// http://eel.is/c++draft/basic.memobj#def:byte
// http://eel.is/c++draft/expr.sizeof#1
// http://eel.is/c++draft/cstdint#syn
static_assert(std::is_same<uint8_t, unsigned char>::value);

// We assume two's complement of signed number representation.
static_assert(-1 == ~0, "two's complement signed number representation assumed");

#endif // KTH_INFRASTRUCTURE_ASSUMPTIONS_HPP_
