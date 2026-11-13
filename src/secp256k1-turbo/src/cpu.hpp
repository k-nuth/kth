// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_SECP256K1_TURBO_CPU_HPP
#define KTH_SECP256K1_TURBO_CPU_HPP

namespace secp256k1_turbo {

// True when the running CPU (and OS) support AVX512F/DQ/BW/VL and IFMA.
bool cpu_supports_avx512ifma();

} // namespace secp256k1_turbo

#endif
