// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 1 (tests 100 to 199)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #100 [Should Pass]: 2147483647 | size 4 equal", "[vm][auto]") { run_bchn_test({"2147483647", "size 4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #101 [Should Pass]: 2147483648 | size 5 equal", "[vm][auto]") { run_bchn_test({"2147483648", "size 5 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #102 [Should Pass]: 549755813887 | size 5 equal", "[vm][auto]") { run_bchn_test({"549755813887", "size 5 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #103 [Should Pass]: 549755813888 | size 6 equal", "[vm][auto]") { run_bchn_test({"549755813888", "size 6 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #104 [Should Pass]: 9223372036854775807 | size 8 equal", "[vm][auto]") { run_bchn_test({"9223372036854775807", "size 8 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #105 [Should Pass]: 0 | size 0 equal", "[vm][auto]") { run_bchn_test({"0", "size 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #106 [Should Pass]: 1 | size 1 equal", "[vm][auto]") { run_bchn_test({"1", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #107 [Should Pass]: 2 | size 1 equal", "[vm][auto]") { run_bchn_test({"2", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #108 [Should Pass]: 3 | size 1 equal", "[vm][auto]") { run_bchn_test({"3", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #109 [Should Pass]: 126 | size 1 equal", "[vm][auto]") { run_bchn_test({"126", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #110 [Should Pass]: 127 | size 1 equal", "[vm][auto]") { run_bchn_test({"127", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #111 [Should Pass]: 128 | size 2 equal", "[vm][auto]") { run_bchn_test({"128", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #112 [Should Pass]: 129 | size 2 equal", "[vm][auto]") { run_bchn_test({"129", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #113 [Should Pass]: 130 | size 2 equal", "[vm][auto]") { run_bchn_test({"130", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #114 [Should Pass]: 255 | size 2 equal", "[vm][auto]") { run_bchn_test({"255", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #115 [Should Pass]: 32766 | size 2 equal", "[vm][auto]") { run_bchn_test({"32766", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #116 [Should Pass]: 32767 | size 2 equal", "[vm][auto]") { run_bchn_test({"32767", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #117 [Should Pass]: 32768 | size 3 equal", "[vm][auto]") { run_bchn_test({"32768", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #118 [Should Pass]: 32769 | size 3 equal", "[vm][auto]") { run_bchn_test({"32769", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #119 [Should Pass]: 32770 | size 3 equal", "[vm][auto]") { run_bchn_test({"32770", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #120 [Should Pass]: 65535 | size 3 equal", "[vm][auto]") { run_bchn_test({"65535", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #121 [Should Pass]: 8388606 | size 3 equal", "[vm][auto]") { run_bchn_test({"8388606", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #122 [Should Pass]: 8388607 | size 3 equal", "[vm][auto]") { run_bchn_test({"8388607", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #123 [Should Pass]: 8388608 | size 4 equal", "[vm][auto]") { run_bchn_test({"8388608", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #124 [Should Pass]: 8388609 | size 4 equal", "[vm][auto]") { run_bchn_test({"8388609", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #125 [Should Pass]: 8388610 | size 4 equal", "[vm][auto]") { run_bchn_test({"8388610", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #126 [Should Pass]: 16777215 | size 4 equal", "[vm][auto]") { run_bchn_test({"16777215", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #127 [Should Pass]: 2147483646 | size 4 equal", "[vm][auto]") { run_bchn_test({"2147483646", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #128 [Should Pass]: 2147483647 | size 4 equal", "[vm][auto]") { run_bchn_test({"2147483647", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #129 [Should Pass]: 2147483648 | size 5 equal", "[vm][auto]") { run_bchn_test({"2147483648", "size 5 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #130 [Should Pass]: 2147483649 | size 5 equal", "[vm][auto]") { run_bchn_test({"2147483649", "size 5 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #131 [Should Pass]: 549755813887 | size 5 equal", "[vm][auto]") { run_bchn_test({"549755813887", "size 5 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #132 [Should Pass]: 549755813888 | size 6 equal", "[vm][auto]") { run_bchn_test({"549755813888", "size 6 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #133 [Should Pass]: 140737488355327 | size 6 equal", "[vm][auto]") { run_bchn_test({"140737488355327", "size 6 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #134 [Should Pass]: 140737488355328 | size 7 equal", "[vm][auto]") { run_bchn_test({"140737488355328", "size 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #135 [Should Pass]: 36028797018963967 | size 7 equal", "[vm][auto]") { run_bchn_test({"36028797018963967", "size 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #136 [Should Pass]: 36028797018963968 | size 8 equal", "[vm][auto]") { run_bchn_test({"36028797018963968", "size 8 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #137 [Should Pass]: 9223372036854775807 | size 8 equal", "[vm][auto]") { run_bchn_test({"9223372036854775807", "size 8 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #138 [Should Pass]: -1 | size 1 equal", "[vm][auto]") { run_bchn_test({"-1", "size 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #139 [Should Pass]: -127 | size 1 equal", "[vm][auto]") { run_bchn_test({"-127", "size 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #140 [Should Pass]: -128 | size 2 equal", "[vm][auto]") { run_bchn_test({"-128", "size 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #141 [Should Pass]: -32767 | size 2 equal", "[vm][auto]") { run_bchn_test({"-32767", "size 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #142 [Should Pass]: -32768 | size 3 equal", "[vm][auto]") { run_bchn_test({"-32768", "size 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #143 [Should Pass]: -8388607 | size 3 equal", "[vm][auto]") { run_bchn_test({"-8388607", "size 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #144 [Should Pass]: -8388608 | size 4 equal", "[vm][auto]") { run_bchn_test({"-8388608", "size 4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #145 [Should Pass]: -2147483647 | size 4 equal", "[vm][auto]") { run_bchn_test({"-2147483647", "size 4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #146 [Should Pass]: -2147483648 | size 5 equal", "[vm][auto]") { run_bchn_test({"-2147483648", "size 5 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #147 [Should Pass]: -549755813887 | size 5 equal", "[vm][auto]") { run_bchn_test({"-549755813887", "size 5 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #148 [Should Pass]: -549755813888 | size 6 equal", "[vm][auto]") { run_bchn_test({"-549755813888", "size 6 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #149 [Should Pass]: -9223372036854775807 | size 8 equal", "[vm][auto]") { run_bchn_test({"-9223372036854775807", "size 8 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #150 [Should Pass]: -1 | size 1 equal", "[vm][auto]") { run_bchn_test({"-1", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #151 [Should Pass]: -2 | size 1 equal", "[vm][auto]") { run_bchn_test({"-2", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #152 [Should Pass]: -127 | size 1 equal", "[vm][auto]") { run_bchn_test({"-127", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #153 [Should Pass]: -125 | size 1 equal", "[vm][auto]") { run_bchn_test({"-125", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #154 [Should Pass]: -126 | size 1 equal", "[vm][auto]") { run_bchn_test({"-126", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #155 [Should Pass]: -127 | size 1 equal", "[vm][auto]") { run_bchn_test({"-127", "size 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #156 [Should Pass]: -128 | size 2 equal", "[vm][auto]") { run_bchn_test({"-128", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #157 [Should Pass]: -129 | size 2 equal", "[vm][auto]") { run_bchn_test({"-129", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #158 [Should Pass]: -255 | size 2 equal", "[vm][auto]") { run_bchn_test({"-255", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #159 [Should Pass]: -32765 | size 2 equal", "[vm][auto]") { run_bchn_test({"-32765", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #160 [Should Pass]: -32766 | size 2 equal", "[vm][auto]") { run_bchn_test({"-32766", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #161 [Should Pass]: -32767 | size 2 equal", "[vm][auto]") { run_bchn_test({"-32767", "size 2 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #162 [Should Pass]: -32768 | size 3 equal", "[vm][auto]") { run_bchn_test({"-32768", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #163 [Should Pass]: -32769 | size 3 equal", "[vm][auto]") { run_bchn_test({"-32769", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #164 [Should Pass]: -65535 | size 3 equal", "[vm][auto]") { run_bchn_test({"-65535", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #165 [Should Pass]: -8388605 | size 3 equal", "[vm][auto]") { run_bchn_test({"-8388605", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #166 [Should Pass]: -8388606 | size 3 equal", "[vm][auto]") { run_bchn_test({"-8388606", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #167 [Should Pass]: -8388607 | size 3 equal", "[vm][auto]") { run_bchn_test({"-8388607", "size 3 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #168 [Should Pass]: -8388608 | size 4 equal", "[vm][auto]") { run_bchn_test({"-8388608", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #169 [Should Pass]: -8388609 | size 4 equal", "[vm][auto]") { run_bchn_test({"-8388609", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #170 [Should Pass]: -2147483645 | size 4 equal", "[vm][auto]") { run_bchn_test({"-2147483645", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #171 [Should Pass]: -2147483646 | size 4 equal", "[vm][auto]") { run_bchn_test({"-2147483646", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #172 [Should Pass]: -2147483647 | size 4 equal", "[vm][auto]") { run_bchn_test({"-2147483647", "size 4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #173 [Should Pass]: -2147483648 | size 5 equal", "[vm][auto]") { run_bchn_test({"-2147483648", "size 5 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #174 [Should Pass]: -549755813887 | size 5 equal", "[vm][auto]") { run_bchn_test({"-549755813887", "size 5 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #175 [Should Pass]: -549755813888 | size 6 equal", "[vm][auto]") { run_bchn_test({"-549755813888", "size 6 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #176 [Should Pass]: -140737488355327 | size 6 equal", "[vm][auto]") { run_bchn_test({"-140737488355327", "size 6 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #177 [Should Pass]: -140737488355328 | size 7 equal", "[vm][auto]") { run_bchn_test({"-140737488355328", "size 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #178 [Should Pass]: -36028797018963967 | size 7 equal", "[vm][auto]") { run_bchn_test({"-36028797018963967", "size 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #179 [Should Pass]: -36028797018963968 | size 8 equal", "[vm][auto]") { run_bchn_test({"-36028797018963968", "size 8 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #180 [Should Pass]: -9223372036854775806 | size 8 equal", "[vm][auto]") { run_bchn_test({"-9223372036854775806", "size 8 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #181 [Should Pass]: -9223372036854775807 | size 8 equal", "[vm][auto]") { run_bchn_test({"-9223372036854775807", "size 8 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #182 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | size 26 equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "size 26 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #183 [Should Pass]: SIZE does not consume argument", "[vm][auto]") { run_bchn_test({"42", "size 1 equalverify 42 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SIZE does not consume argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #184 [Should Pass]: 2 -2 add | 0 equal", "[vm][auto]") { run_bchn_test({"2 -2 add", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #185 [Should Pass]: 2147483647 -2147483647 add | 0 equal", "[vm][auto]") { run_bchn_test({"2147483647 -2147483647 add", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #186 [Should Pass]: 9223372036854775807 -9223372036854775807 add | 0 equal", "[vm][auto]") { run_bchn_test({"9223372036854775807 -9223372036854775807 add", "0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #187 [Should Pass]: -1 -1 add | -2 equal", "[vm][auto]") { run_bchn_test({"-1 -1 add", "-2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #188 [Should Pass]: 0 0 | equal", "[vm][auto]") { run_bchn_test({"0 0", "equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #189 [Should Pass]: 1 1 add | 2 equal", "[vm][auto]") { run_bchn_test({"1 1 add", "2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #190 [Should Pass]: 1 add1 | 2 equal", "[vm][auto]") { run_bchn_test({"1 add1", "2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #191 [Should Pass]: 111 sub1 | 110 equal", "[vm][auto]") { run_bchn_test({"111 sub1", "110 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #192 [Should Pass]: 111 1 add 12 sub | 100 equal", "[vm][auto]") { run_bchn_test({"111 1 add 12 sub", "100 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #193 [Should Pass]: 0 abs | 0 equal", "[vm][auto]") { run_bchn_test({"0 abs", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #194 [Should Pass]: 16 abs | 16 equal", "[vm][auto]") { run_bchn_test({"16 abs", "16 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #195 [Should Pass]: -16 abs | -16 negate equal", "[vm][auto]") { run_bchn_test({"-16 abs", "-16 negate equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #196 [Should Pass]: 0 not | nop", "[vm][auto]") { run_bchn_test({"0 not", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #197 [Should Pass]: 1 not | 0 equal", "[vm][auto]") { run_bchn_test({"1 not", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #198 [Should Pass]: 11 not | 0 equal", "[vm][auto]") { run_bchn_test({"11 not", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #199 [Should Pass]: 0 0notequal | 0 equal", "[vm][auto]") { run_bchn_test({"0 0notequal", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK
