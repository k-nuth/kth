// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "kernel.hpp"
#include "vec_scalar.hpp"

namespace secp256k1_turbo {

void kernel_scalar(lane_batch& b, check_mode mode) {
    ecmult<vec1>::run_all(b, mode);
}

} // namespace secp256k1_turbo
