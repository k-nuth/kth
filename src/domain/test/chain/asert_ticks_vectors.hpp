// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Exact targets from CHIP-2025-03, revision bf058dfa7f6f593b725bef7adb9228965097cf97.
// https://gitlab.com/0353F40E/fablous/-/blob/main/tests/asert/asert.json
// These values are transcribed from the reference data, not computed by
// the implementation under test. Only the pure calculation vectors live here;
// the wrapper/minimum-difficulty cases belong at the chain-state boundary.
#pragma once
#include <cstdint>
namespace asert_test_vectors {
inline constexpr char pow_limit[] = "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
struct vector {
    char const* name;
    char const* anchor;
    int64_t time_diff, tick_delta;
    uint64_t anchor_interval, next_interval;
    uint32_t half_life;
    char const* expected;
};
inline constexpr vector calculation[]{
    {"calc_args[00]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 600, 172800, "000000007fffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[01]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 345600, 600, 600, 172800, "000000003fffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[02]", "000000007fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 600, 172800, "000000003fffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[03]", "000000003fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 600, 172800, "000000001fffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[04]", "000000001fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 600, 172800, "000000000fffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[05]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38361600, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000003"},
    {"calc_args[06]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38433000, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000003"},
    {"calc_args[07]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38433600, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000002"},
    {"calc_args[08]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38533800, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000002"},
    {"calc_args[09]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38534400, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000001"},
    {"calc_args[10]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38707200, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000001"},
    {"calc_args[11]", "0000000000000000000000000000000000000000000000000000000000000001", 600, 38707200, 600, 600, 172800, "0000000000000000000000000000000000000000000000000000000000000001"},
    {"calc_args[12]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 138840, 0, 600, 600, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[13]", "0000000000000000000000000000000000000000000000000000000000000001", 38707800, 0, 600, 600, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[14]", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 900, 600, 600, 600, 172800, "00000000ffb1ffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[15]", "000000008000000000000000000fffffffffffffffffffffffffffffffffffff", 5703000, 0, 600, 600, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[16]", "0000000000000000000000000000000000000000000000000000000000000001", 44237400, 0, 600, 600, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[17]", "0000000000000000000000000000000000000000000000000000000000000001", 38707799, 0, 600, 600, 172800, "00000000ffff8000000000000000000000000000000000000000000000000000"},
    {"calc_args[00]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[01]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 345600, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[02]_60s", "000000007fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[03]_60s", "000000003fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[04]_60s", "000000001fffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 172800, 600, 60, 172800, "000000009ffffffffffffffffffffffffffffffffffffffffffffffffffffff6"},
    {"calc_args[05]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38361600, 600, 60, 172800, "000000000000000000000000000000000000000000000000000000000000001e"},
    {"calc_args[06]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38433000, 600, 60, 172800, "000000000000000000000000000000000000000000000000000000000000001e"},
    {"calc_args[07]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38433600, 600, 60, 172800, "0000000000000000000000000000000000000000000000000000000000000014"},
    {"calc_args[08]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38533800, 600, 60, 172800, "0000000000000000000000000000000000000000000000000000000000000014"},
    {"calc_args[09]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 600, 38534400, 600, 60, 172800, "000000000000000000000000000000000000000000000000000000000000000a"},
    {"calc_args[12]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 138840, 0, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[13]_60s", "0000000000000000000000000000000000000000000000000000000000000001", 38707800, 0, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[14]_60s", "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff", 900, 600, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[15]_60s", "000000008000000000000000000fffffffffffffffffffffffffffffffffffff", 5703000, 0, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[16]_60s", "0000000000000000000000000000000000000000000000000000000000000001", 44237400, 0, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"calc_args[17]_60s", "0000000000000000000000000000000000000000000000000000000000000001", 38707799, 0, 600, 60, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"anchor_steady_600", "0000000000000100000000000000000000000000000000000000000000000000", 600, 0, 600, 600, 172800, "0000000000000100000000000000000000000000000000000000000000000000"},
    {"anchor_steady_60", "0000000000000100000000000000000000000000000000000000000000000000", 60, 0, 60, 60, 172800, "0000000000000100000000000000000000000000000000000000000000000000"},
    {"activation_600_to_60", "0000000000000100000000000000000000000000000000000000000000000000", 660, 60, 600, 60, 172800, "0000000000000a00000000000000000000000000000000000000000000000000"},
    {"negative_solvetime_halve", "0000000000000100000000000000000000000000000000000000000000000000", -172200, 0, 600, 600, 172800, "0000000000000080000000000000000000000000000000000000000000000000"},
    {"positive_solvetime_double", "0000000000000100000000000000000000000000000000000000000000000000", 173400, 0, 600, 600, 172800, "0000000000000200000000000000000000000000000000000000000000000000"},
    {"ratio_guard_wrap_low", "0000000000000100000000000000000000000000000000000000000000000000", 9159000, 0, 600, 75, 172800, "00000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
    {"equiv_1M_600s", "0000000000000100000000000000000000000000000000000000000000000000", 2400, 600, 600, 600, 172800, "00000000000001013d0000000000000000000000000000000000000000000000"},
    {"equiv_1M_60s", "0000000000000100000000000000000000000000000000000000000000000000", 2400, 600, 600, 60, 172800, "0000000000000a0c620000000000000000000000000000000000000000000000"},
    {"testnet_halflife_steady", "0000000000000100000000000000000000000000000000000000000000000000", 1200, 600, 600, 600, 3600, "0000000000000100000000000000000000000000000000000000000000000000"},
    {"testnet_halflife_fast", "0000000000000100000000000000000000000000000000000000000000000000", 2400, 600, 600, 600, 3600, "0000000000000142870000000000000000000000000000000000000000000000"},
    {"testnet_halflife_60tick", "0000000000000100000000000000000000000000000000000000000000000000", 660, 60, 600, 60, 3600, "0000000000000a00000000000000000000000000000000000000000000000000"},
};
} // namespace asert_test_vectors
