// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cpu.hpp"

namespace secp256k1_turbo {

bool cpu_supports_avx512ifma() {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    __builtin_cpu_init();
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512dq") &&
           __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl") &&
           __builtin_cpu_supports("avx512ifma");
#else
    return false;
#endif
}

} // namespace secp256k1_turbo
