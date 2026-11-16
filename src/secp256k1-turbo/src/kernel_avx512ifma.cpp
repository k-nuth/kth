// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Compiled with -mavx512f -mavx512ifma -mavx512vl -mavx512dq -mavx512bw.
// Nothing in this translation unit may be inlined elsewhere; the only entry
// point is kernel_avx512ifma, called after a CPUID check.

#include "kernel.hpp"
#include "vec_avx512ifma.hpp"

namespace secp256k1_turbo {

void kernel_avx512ifma(lane_batch& b, check_mode mode) {
    ecmult<vec8>::run_all(b, mode);
}

} // namespace secp256k1_turbo
