// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_CONSTANTS_HPP_
#define KTH_INFRASTRUCTURE_CONSTANTS_HPP_

#include <cstddef>
#include <cstdint>

#include <kth/infrastructure/assumptions.hpp>
#include <kth/infrastructure/compat.hpp>
#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/hash_define.hpp>

namespace kth {

// Generic constants.
//-----------------------------------------------------------------------------

constexpr int64_t min_int64 = MIN_INT64;
constexpr int64_t max_int64 = MAX_INT64;
constexpr int32_t min_int32 = MIN_INT32;
constexpr int32_t max_int32 = MAX_INT32;
constexpr uint64_t max_uint64 = MAX_UINT64;
constexpr uint32_t max_uint32 = MAX_UINT32;
constexpr uint16_t max_uint16 = MAX_UINT16;
constexpr uint8_t max_uint8 = MAX_UINT8;
constexpr uint64_t max_size_t = BC_MAX_SIZE;
constexpr uint8_t byte_bits = 8;

// Network protocol constants.
//-----------------------------------------------------------------------------

// Variable integer prefix sentinels.
constexpr uint8_t varint_two_bytes = 0xfd;
constexpr uint8_t varint_four_bytes = 0xfe;
constexpr uint8_t varint_eight_bytes = 0xff;

// String padding sentinel.
constexpr uint8_t string_terminator = 0x00;

} // namespace kth

#endif // KTH_INFRASTRUCTURE_CONSTANTS_HPP_
