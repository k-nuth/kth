// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::wallet;

// Start Test Suite: ec public tests

// Compile-time verification that the constexpr surface is actually
// usable as a constant expression. A `constexpr` variable definition
// forces evaluation at translation time — a regression that made
// `from_verified_point`, the private ctor, or the accessors runtime-
// only would surface as a compile error here, closer to the type than
// a downstream call site.
namespace {

constexpr ec_compressed some_point = {{
    0x02, 0x50, 0x86, 0x3a, 0xd6, 0x4a, 0x87, 0xae,
    0x8a, 0x2f, 0xe8, 0x3c, 0x1a, 0xf1, 0xa8, 0x40,
    0x3c, 0xb5, 0x3f, 0x53, 0xe4, 0x86, 0xd8, 0x51,
    0x1d, 0xad, 0x8a, 0x04, 0x88, 0x7e, 0x5b, 0x23,
    0x52,
}};

constexpr auto kSample = ec_public::from_verified_point(some_point, true);
static_assert(kSample.compressed());
static_assert(kSample.point() == some_point);
static_assert(kSample == kSample);
static_assert(ec_public::compressed_even == 0x02);
static_assert(ec_public::compressed_odd  == 0x03);
static_assert(ec_public::uncompressed    == 0x04);

} // namespace

// End Test Suite
