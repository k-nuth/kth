// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_COMPAT_HPP
#define KTH_INFRASTRUCTURE_COMPAT_HPP

#include <cstdint>
#include <limits>

// // CTP_Nov2013 implements noexcept but unfortunately VC12 and CTP_Nov2013
// // both identify as _MSC_VER = 1800, otherwise we could include CTP_Nov2013.
// #if defined(_MSC_VER) && (_MSC_VER <= 1800)
//     #define BC_NOEXCEPT _NOEXCEPT
//     #define constexpr inline
//     #define constexpr
// #else
//     #define BC_NOEXCEPT noexcept
//     #define constexpr constexpr
//     #define constexpr constexpr
// #endif

// TODO(legacy): prefix names with BC_
#ifdef _MSC_VER
    #define MIN_INT64 INT64_MIN
    #define MAX_INT64 INT64_MAX
    #define MIN_INT32 INT32_MIN
    #define MAX_INT32 INT32_MAX
    #define MAX_UINT64 UINT64_MAX
    #define MAX_UINT32 UINT32_MAX
    #define MAX_UINT16 UINT16_MAX
    #define MAX_UINT8 UINT8_MAX
    #define BC_MAX_SIZE SIZE_MAX
#else
    #define MIN_INT64 std::numeric_limits<int64_t>::min()
    #define MAX_INT64 std::numeric_limits<int64_t>::max()
    #define MIN_INT32 std::numeric_limits<int32_t>::min()
    #define MAX_INT32 std::numeric_limits<int32_t>::max()
    #define MAX_UINT64 std::numeric_limits<uint64_t>::max()
    #define MAX_UINT32 std::numeric_limits<uint32_t>::max()
    #define MAX_UINT16 std::numeric_limits<uint16_t>::max()
    #define MAX_UINT8 std::numeric_limits<uint8_t>::max()
    #define BC_MAX_SIZE std::numeric_limits<size_t>::max()
#endif

#endif
