// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Port of Bitcoin Cash Node src/test/pow_tests.cpp, calculate_asert_test
// (commit 07576013c91ff4a3a74acd85f189c69121cdad1b).
// Original: Copyright (c) 2015-present The Bitcoin Core / Bitcoin developers, MIT license.
//
// BCHN's CalculateASERT(ref, spacing, time_diff, height_diff, ...) adds 1 to the
// height difference it receives; daa::aserti3_2d takes the difference already
// including it (chain_state::daa_aserti3_2d passes `height - anchor_height` of the
// block being evaluated). Every call below therefore passes BCHN's height_diff + 1.

#include <test_helpers.hpp>

#include <kth/domain/chain/compact.hpp>
#include <kth/domain/constants.hpp>

#if defined(KTH_CURRENCY_BCH)

#include <vector>

using namespace kth;
namespace chain = kth::domain::chain;

// kth/domain/chain/daa/aserti3_2d.hpp defines this function without `inline`, so a
// second translation unit that includes it duplicates the symbol that chain_state.cpp
// already defines in libdomain (failed_regression aserti3_2d_non_inline_in_header in
// coverage.json). The test declares it instead and links against that definition.
namespace kth::domain::chain::daa {
uint256_t aserti3_2d(uint256_t const& anchor_target, uint32_t target_spacing_seconds, int64_t time_diff,
                     int64_t height_diff, uint256_t const& pow_limit, uint32_t half_life) noexcept;
}

namespace {

constexpr uint32_t half_life = 2 * 24 * 60 * 60;     // params.nASERTHalfLife on mainnet
constexpr int64_t parent_time_diff = 600;

// BCHN mainnet params.powLimit: 32 zero bits followed by all ones. (Knuth keeps the
// compact-decoded limit 0x1d00ffff, which has zeros after the mantissa; both encode
// to the same nBits, and the vectors below use the all-ones value as input.)
uint256_t const& pow_limit() {
    static uint256_t const value("0x00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    return value;
}

uint256_t calculate_asert(uint256_t const& ref_target, int64_t spacing, int64_t time_diff, int64_t height_diff) {
    return chain::daa::aserti3_2d(ref_target, uint32_t(spacing), time_diff, height_diff + 1, pow_limit(), half_life);
}

uint32_t bits_of(uint256_t const& target) {
    return chain::compact(target).normal();
}

} // namespace

TEST_CASE("BCHN pow_tests - calculate_asert_test", "[bchn_non_vm][pow][asert]") {
    auto const& limit = pow_limit();
    uint256_t const initial_target = limit >> 4;
    int64_t height = 0;
    int64_t const spacing = target_spacing_seconds;

    // Steady
    auto next_target = calculate_asert(initial_target, spacing, parent_time_diff + 600, ++height);
    CHECK(next_target == initial_target);

    // A block that arrives in half the expected time
    next_target = calculate_asert(initial_target, spacing, parent_time_diff + 600 + 300, ++height);
    CHECK(next_target < initial_target);

    // A block that makes up for the shortfall of the previous one, restores the target to initial
    auto prev_target = next_target;
    next_target = calculate_asert(initial_target, spacing, parent_time_diff + 600 + 300 + 900, ++height);
    CHECK(next_target > prev_target);
    CHECK(next_target == initial_target);

    // Two days ahead of schedule should double the target (halve the difficulty)
    prev_target = next_target;
    next_target = calculate_asert(prev_target, spacing, parent_time_diff + 288 * 1200, 288);
    CHECK(next_target == prev_target * 2);

    // Two days behind schedule should halve the target (double the difficulty)
    prev_target = next_target;
    next_target = calculate_asert(prev_target, spacing, parent_time_diff + 288 * 0, 288);
    CHECK(next_target == prev_target / 2);
    CHECK(next_target == initial_target);

    // Ramp up from initial_target to the pow limit: should only take 4 doublings
    uint32_t const pow_limit_bits = bits_of(limit);
    uint32_t next_bits = 0;
    for (size_t k = 0; k < 3; k++) {
        prev_target = next_target;
        next_target = calculate_asert(prev_target, spacing, parent_time_diff + 288 * 1200, 288);
        CHECK(next_target == prev_target * 2);
        CHECK(next_target < limit);
        next_bits = bits_of(next_target);
        CHECK(next_bits != pow_limit_bits);
    }

    prev_target = next_target;
    next_target = calculate_asert(prev_target, spacing, parent_time_diff + 288 * 1200, 288);
    next_bits = bits_of(next_target);
    CHECK(next_target == prev_target * 2);
    CHECK(next_bits == pow_limit_bits);

    // Fast periods now cannot increase the target beyond the pow limit, even if we try to
    // overflow next_target: 512 days would overflow unless the overflow is detected
    next_target = calculate_asert(prev_target, spacing, parent_time_diff + int64_t(512) * 144 * 600, 0);
    next_bits = bits_of(next_target);
    CHECK(next_bits == pow_limit_bits);

    // Underflows: an extra ~446 days of blocks brings a pow limit target down to the minimum target of 1
    next_target = calculate_asert(limit, spacing, 0, int64_t(2) * (256 - 33) * 144);
    next_bits = bits_of(next_target);
    CHECK(next_bits == bits_of(uint256_t(1)));

    struct calc_params {
        uint256_t ref_target;
        int64_t target_spacing;
        int64_t time_diff;
        int64_t height_diff;
        uint256_t expected_target;
        uint32_t expected_bits;
    };

    uint256_t const single_300_target("0x00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff");
    uint256_t const funny_ref_target("0x000000008000000000000000000fffffffffffffffffffffffffffffffffffff");

    // timeDiff entries exclude `parent_time_diff`, which is added in the loop below.
    std::vector<calc_params> const calculate_args = {
        {limit, 600, 0, 2 * 144, limit >> 1, 0x1c7fffff},
        {limit, 600, 0, 4 * 144, limit >> 2, 0x1c3fffff},
        {limit >> 1, 600, 0, 2 * 144, limit >> 2, 0x1c3fffff},
        {limit >> 2, 600, 0, 2 * 144, limit >> 3, 0x1c1fffff},
        {limit >> 3, 600, 0, 2 * 144, limit >> 4, 0x1c0fffff},
        {limit, 600, 0, 2 * (256 - 34) * 144, 3, 0x01030000},
        {limit, 600, 0, 2 * (256 - 34) * 144 + 119, 3, 0x01030000},
        {limit, 600, 0, 2 * (256 - 34) * 144 + 120, 2, 0x01020000},
        {limit, 600, 0, 2 * (256 - 33) * 144 - 1, 2, 0x01020000},
        {limit, 600, 0, 2 * (256 - 33) * 144, 1, 0x01010000},    // 1 bit less since we do not need to shift to 0
        {limit, 600, 0, 2 * (256 - 32) * 144, 1, 0x01010000},    // more will not decrease below 1
        {1, 600, 0, 2 * (256 - 32) * 144, 1, 0x01010000},
        {limit, 600, 2 * (512 - 32) * 144, 0, limit, pow_limit_bits},
        {1, 600, (512 - 64) * 144 * 600, 0, limit, pow_limit_bits},
        {limit, 600, 300, 1, single_300_target, 0x1d00ffb1},     // clamps to the pow limit
        {funny_ref_target, 600, int64_t(600) * 2 * 33 * 144, 0, limit, pow_limit_bits},   // confuses any attempt to detect overflow by inspecting the result
        {1, 600, int64_t(600) * 2 * 256 * 144, 0, limit, pow_limit_bits},                  // overflow to exactly 2^256
        {1, 600, int64_t(600) * 2 * 224 * 144 - 1, 0, uint256_t(0xffff8) << 204, pow_limit_bits},   // just under the pow limit (not clamped) yet over its compact value
    };

    size_t index = 0;
    for (auto const& v : calculate_args) {
        DYNAMIC_SECTION("calculate_args[" << index++ << "]") {
            next_target = calculate_asert(v.ref_target, v.target_spacing, parent_time_diff + v.time_diff, v.height_diff);
            next_bits = bits_of(next_target);
            CHECK(next_target == v.expected_target);
            CHECK(next_bits == v.expected_bits);
        }
    }
}

#endif // defined(KTH_CURRENCY_BCH)
