// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UTILITY_UNITS_HPP_
#define KTH_INFRASTRUCTURE_UTILITY_UNITS_HPP_

#include <cstdint>

namespace kth {

// User-defined literals for binary storage units (IEC standard)
// 1 KiB = 1024 bytes, 1 MiB = 1024 KiB, 1 GiB = 1024 MiB
constexpr uint64_t operator""_KiB(unsigned long long n) { return n * (uint64_t(1) << 10); }
constexpr uint64_t operator""_MiB(unsigned long long n) { return n * (uint64_t(1) << 20); }
constexpr uint64_t operator""_GiB(unsigned long long n) { return n * (uint64_t(1) << 30); }
constexpr uint64_t operator""_TiB(unsigned long long n) { return n * (uint64_t(1) << 40); }

// User-defined literals for decimal storage units (SI standard)
// 1 KB = 1000 bytes, 1 MB = 1000 KB, 1 GB = 1000 MB
constexpr uint64_t operator""_KB(unsigned long long n) { return n * 1'000ULL; }
constexpr uint64_t operator""_MB(unsigned long long n) { return n * 1'000'000ULL; }
constexpr uint64_t operator""_GB(unsigned long long n) { return n * 1'000'000'000ULL; }
constexpr uint64_t operator""_TB(unsigned long long n) { return n * 1'000'000'000'000ULL; }

} // namespace kth

#endif // KTH_INFRASTRUCTURE_UTILITY_UNITS_HPP_
