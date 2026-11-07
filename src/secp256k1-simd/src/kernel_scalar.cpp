// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "kernel.hpp"
#include "vec_scalar.hpp"

namespace kth::secp256k1_simd {

void kernel_scalar(lane_batch& b, check_mode mode) {
    ecmult<vec1>::run_all(b, mode);
}

} // namespace kth::secp256k1_simd
