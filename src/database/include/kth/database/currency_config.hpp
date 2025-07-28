// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_CURRENCY_CONFIG_HPP_
#define KTH_DATABASE_CURRENCY_CONFIG_HPP_

#include <cstdint>
#include <filesystem>

// #include <boost/filesystem.hpp>

#include <kth/database/define.hpp>

namespace kth::database {

#if defined(KTH_CURRENCY_BCH)
#define KTH_WITNESS_DEFAULT false
#define KTH_POSITION_WRITER write_4_bytes_little_endian
#define KTH_POSITION_READER read_4_bytes_little_endian
static constexpr auto position_size = sizeof(uint32_t);
size_t const position_max = max_uint32;
#else
#define KTH_WITNESS_DEFAULT true
#define KTH_POSITION_WRITER write_2_bytes_little_endian
#define KTH_POSITION_READER read_2_bytes_little_endian
static constexpr auto position_size = sizeof(uint16_t);
size_t const position_max = max_uint16;
#endif  // KTH_CURRENCY_BCH

}  // namespace kth::database

#endif  // KTH_DATABASE_CURRENCY_CONFIG_HPP_
