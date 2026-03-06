// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 4 (tests 400 to 499)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #400 [Should Pass]: 8388610 | [02008000] equal", "[vm][auto]") { run_bchn_test({"8388610", "[02008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #401 [Should Pass]: 2147483646 | [FEFFFF7F] equal", "[vm][auto]") { run_bchn_test({"2147483646", "[FEFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #402 [Should Pass]: 2147483647 | [FFFFFF7F] equal", "[vm][auto]") { run_bchn_test({"2147483647", "[FFFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #403 [Should Pass]: 2147483648 | [0000008000] equal", "[vm][auto]") { run_bchn_test({"2147483648", "[0000008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #404 [Should Pass]: 2147483649 | [0100008000] equal", "[vm][auto]") { run_bchn_test({"2147483649", "[0100008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #405 [Should Pass]: 549755813887 | [FFFFFFFF7F] equal", "[vm][auto]") { run_bchn_test({"549755813887", "[FFFFFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #406 [Should Pass]: 549755813888 | [000000008000] equal", "[vm][auto]") { run_bchn_test({"549755813888", "[000000008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #407 [Should Pass]: 140737488355327 | [FFFFFFFFFF7F] equal", "[vm][auto]") { run_bchn_test({"140737488355327", "[FFFFFFFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #408 [Should Pass]: 140737488355328 | [00000000008000] equal", "[vm][auto]") { run_bchn_test({"140737488355328", "[00000000008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #409 [Should Pass]: 36028797018963967 | [FFFFFFFFFFFF7F] equal", "[vm][auto]") { run_bchn_test({"36028797018963967", "[FFFFFFFFFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #410 [Should Pass]: 36028797018963968 | [0000000000008000] equal", "[vm][auto]") { run_bchn_test({"36028797018963968", "[0000000000008000] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #411 [Should Pass]: 9223372036854775807 | [FFFFFFFFFFFFFF7F] equal", "[vm][auto]") { run_bchn_test({"9223372036854775807", "[FFFFFFFFFFFFFF7F] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #412 [Should Pass]: Numbers are little-endian with the MSB being a sign bit", "[vm][auto]") { run_bchn_test({"-1", "[81] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Numbers are little-endian with the MSB being a sign bit"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #413 [Should Pass]: -127 | [FF] equal", "[vm][auto]") { run_bchn_test({"-127", "[FF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #414 [Should Pass]: -128 | [8080] equal", "[vm][auto]") { run_bchn_test({"-128", "[8080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #415 [Should Pass]: -32767 | [FFFF] equal", "[vm][auto]") { run_bchn_test({"-32767", "[FFFF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #416 [Should Pass]: -32768 | [008080] equal", "[vm][auto]") { run_bchn_test({"-32768", "[008080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #417 [Should Pass]: -8388607 | [FFFFFF] equal", "[vm][auto]") { run_bchn_test({"-8388607", "[FFFFFF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #418 [Should Pass]: -8388608 | [00008080] equal", "[vm][auto]") { run_bchn_test({"-8388608", "[00008080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #419 [Should Pass]: -2147483647 | [FFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-2147483647", "[FFFFFFFF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #420 [Should Pass]: -2147483648 | [0000008080] equal", "[vm][auto]") { run_bchn_test({"-2147483648", "[0000008080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #421 [Should Pass]: -4294967295 | [FFFFFFFF80] equal", "[vm][auto]") { run_bchn_test({"-4294967295", "[FFFFFFFF80] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #422 [Should Pass]: -549755813887 | [FFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-549755813887", "[FFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #423 [Should Pass]: -549755813888 | [000000008080] equal", "[vm][auto]") { run_bchn_test({"-549755813888", "[000000008080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #424 [Should Pass]: -9223372036854775807 | [FFFFFFFFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-9223372036854775807", "[FFFFFFFFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #425 [Should Pass]: Numbers are little-endian with the MSB being a sign bit", "[vm][auto]") { run_bchn_test({"-1", "[81] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Numbers are little-endian with the MSB being a sign bit"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #426 [Should Pass]: -2 | [82] equal", "[vm][auto]") { run_bchn_test({"-2", "[82] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #427 [Should Pass]: -125 | [FD] equal", "[vm][auto]") { run_bchn_test({"-125", "[FD] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #428 [Should Pass]: -126 | [FE] equal", "[vm][auto]") { run_bchn_test({"-126", "[FE] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #429 [Should Pass]: -127 | [FF] equal", "[vm][auto]") { run_bchn_test({"-127", "[FF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #430 [Should Pass]: -128 | [8080] equal", "[vm][auto]") { run_bchn_test({"-128", "[8080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #431 [Should Pass]: -129 | [8180] equal", "[vm][auto]") { run_bchn_test({"-129", "[8180] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #432 [Should Pass]: -255 | [FF80] equal", "[vm][auto]") { run_bchn_test({"-255", "[FF80] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #433 [Should Pass]: -32765 | [FDFF] equal", "[vm][auto]") { run_bchn_test({"-32765", "[FDFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #434 [Should Pass]: -32766 | [FEFF] equal", "[vm][auto]") { run_bchn_test({"-32766", "[FEFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #435 [Should Pass]: -32767 | [FFFF] equal", "[vm][auto]") { run_bchn_test({"-32767", "[FFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #436 [Should Pass]: -32768 | [008080] equal", "[vm][auto]") { run_bchn_test({"-32768", "[008080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #437 [Should Pass]: -32769 | [018080] equal", "[vm][auto]") { run_bchn_test({"-32769", "[018080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #438 [Should Pass]: -65535 | [FFFF80] equal", "[vm][auto]") { run_bchn_test({"-65535", "[FFFF80] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #439 [Should Pass]: -8388605 | [FDFFFF] equal", "[vm][auto]") { run_bchn_test({"-8388605", "[FDFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #440 [Should Pass]: -8388606 | [FEFFFF] equal", "[vm][auto]") { run_bchn_test({"-8388606", "[FEFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #441 [Should Pass]: -8388607 | [FFFFFF] equal", "[vm][auto]") { run_bchn_test({"-8388607", "[FFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #442 [Should Pass]: -8388608 | [00008080] equal", "[vm][auto]") { run_bchn_test({"-8388608", "[00008080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #443 [Should Pass]: -8388609 | [01008080] equal", "[vm][auto]") { run_bchn_test({"-8388609", "[01008080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #444 [Should Pass]: -2147483645 | [FDFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-2147483645", "[FDFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #445 [Should Pass]: -2147483646 | [FEFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-2147483646", "[FEFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #446 [Should Pass]: -2147483647 | [FFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-2147483647", "[FFFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #447 [Should Pass]: -2147483648 | [0000008080] equal", "[vm][auto]") { run_bchn_test({"-2147483648", "[0000008080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #448 [Should Pass]: -4294967295 | [FFFFFFFF80] equal", "[vm][auto]") { run_bchn_test({"-4294967295", "[FFFFFFFF80] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #449 [Should Pass]: -549755813887 | [FFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-549755813887", "[FFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #450 [Should Pass]: -549755813888 | [000000008080] equal", "[vm][auto]") { run_bchn_test({"-549755813888", "[000000008080] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #451 [Should Pass]: -140737488355327 | [FFFFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-140737488355327", "[FFFFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #452 [Should Pass]: -36028797018963967 | [FFFFFFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-36028797018963967", "[FFFFFFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #453 [Should Pass]: -9223372036854775807 | [FFFFFFFFFFFFFFFF] equal", "[vm][auto]") { run_bchn_test({"-9223372036854775807", "[FFFFFFFFFFFFFFFF] equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #454 [Should Pass]: We can do math on 4-byte integers, and compare 5-byte ones", "[vm][auto]") { run_bchn_test({"2147483647", "add1 2147483648 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 4-byte integers, and compare 5-byte ones"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #455 [Should Pass]: 2147483647 | add1 1", "[vm][auto]") { run_bchn_test({"2147483647", "add1 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #456 [Should Pass]: -2147483647 | add1 1", "[vm][auto]") { run_bchn_test({"-2147483647", "add1 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #457 [Should Pass]: We can do math on 8-byte integers", "[vm][auto]") { run_bchn_test({"2147483647", "add1 2147483648 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 8-byte integers"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #458 [Should Pass]: 2147483647 | add1 1", "[vm][auto]") { run_bchn_test({"2147483647", "add1 1", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #459 [Should Pass]: -2147483647 | add1 1", "[vm][auto]") { run_bchn_test({"-2147483647", "add1 1", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

// KNUTH_OVERRIDE #144: error=OVERFLOW, reason=Knuth returns overflow for 64-bit arithmetic result exceeding max size, BCHN returns INVALID_NUMBER_RANGE_64_BIT
TEST_CASE("VM-AUTO #460 [Should Fail: OVERFLOW]: 64-bit integer overflow detected", "[vm][auto]") { run_bchn_test({"9223372036854775807", "add1 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::overflow, "64-bit integer overflow detected [KNUTH_OVERRIDE: OVERFLOW]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OVERFLOW

// KNUTH_OVERRIDE #145: error=OVERFLOW, reason=Knuth returns overflow for 64-bit arithmetic result exceeding max size, BCHN returns INVALID_NUMBER_RANGE_64_BIT
TEST_CASE("VM-AUTO #461 [Should Fail: OVERFLOW]: 9223372036854775807 | add1 1", "[vm][auto]") { run_bchn_test({"9223372036854775807", "add1 1", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::overflow, " [KNUTH_OVERRIDE: OVERFLOW]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OVERFLOW

TEST_CASE("VM-AUTO #462 [Should Pass]: -9223372036854775807 | add1 1", "[vm][auto]") { run_bchn_test({"-9223372036854775807", "add1 1", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #463 [Should Pass]: Not the same byte array...", "[vm][auto]") { run_bchn_test({"1", "[0100] equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Not the same byte array..."}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #464 [Should Pass]: ... but they are numerically equal", "[vm][auto]") { run_bchn_test({"1", "[0100] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "... but they are numerically equal"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #465 [Should Pass]: 11 | [1.0b0000] numequal", "[vm][auto]") { run_bchn_test({"11", "[1.0b0000] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #466 [Should Pass]: 0 | [80] equal not", "[vm][auto]") { run_bchn_test({"0", "[80] equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #467 [Should Pass]: Zero numerically equals negative zero", "[vm][auto]") { run_bchn_test({"0", "[80] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Zero numerically equals negative zero"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #468 [Should Pass]: 0 | [0080] numequal", "[vm][auto]") { run_bchn_test({"0", "[0080] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #469 [Should Pass]: [000080] | [00000080] numequal", "[vm][auto]") { run_bchn_test({"[000080]", "[00000080] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #470 [Should Pass]: [100080] | [10000080] numequal", "[vm][auto]") { run_bchn_test({"[100080]", "[10000080] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #471 [Should Pass]: [100000] | [10000000] numequal", "[vm][auto]") { run_bchn_test({"[100000]", "[10000000] numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #472 [Should Pass]: The following tests check the if(stack.size() < N) tests in each opcode", "[vm][auto]") { run_bchn_test({"nop", "nop 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "The following tests check the if(stack.size() < N) tests in each opcode"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #473 [Should Pass]: They are here to catch copy-and-paste errors", "[vm][auto]") { run_bchn_test({"1", "if 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "They are here to catch copy-and-paste errors"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #474 [Should Pass]: Most of them are duplicated elsewhere,", "[vm][auto]") { run_bchn_test({"0", "notif 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Most of them are duplicated elsewhere,"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #475 [Should Pass]: but, hey, more is always better, right?", "[vm][auto]") { run_bchn_test({"1", "verify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "but, hey, more is always better, right?"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #476 [Should Pass]: 0 | toaltstack 1", "[vm][auto]") { run_bchn_test({"0", "toaltstack 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #477 [Should Pass]: 1 | toaltstack fromaltstack", "[vm][auto]") { run_bchn_test({"1", "toaltstack fromaltstack", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #478 [Should Pass]: 0 0 | 2drop 1", "[vm][auto]") { run_bchn_test({"0 0", "2drop 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #479 [Should Pass]: 0 1 | 2dup", "[vm][auto]") { run_bchn_test({"0 1", "2dup", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #480 [Should Pass]: 0 0 1 | 3dup", "[vm][auto]") { run_bchn_test({"0 0 1", "3dup", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #481 [Should Pass]: 0 1 0 0 | 2over", "[vm][auto]") { run_bchn_test({"0 1 0 0", "2over", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #482 [Should Pass]: 0 1 0 0 0 0 | 2rot", "[vm][auto]") { run_bchn_test({"0 1 0 0 0 0", "2rot", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #483 [Should Pass]: 0 1 0 0 | 2swap", "[vm][auto]") { run_bchn_test({"0 1 0 0", "2swap", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #484 [Should Pass]: 1 | ifdup", "[vm][auto]") { run_bchn_test({"1", "ifdup", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #485 [Should Pass]: nop | depth 1", "[vm][auto]") { run_bchn_test({"nop", "depth 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #486 [Should Pass]: 0 | drop 1", "[vm][auto]") { run_bchn_test({"0", "drop 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #487 [Should Pass]: 1 | dup", "[vm][auto]") { run_bchn_test({"1", "dup", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #488 [Should Pass]: 0 1 | nip", "[vm][auto]") { run_bchn_test({"0 1", "nip", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #489 [Should Pass]: 1 0 | over", "[vm][auto]") { run_bchn_test({"1 0", "over", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #490 [Should Pass]: 1 0 0 0 3 | pick", "[vm][auto]") { run_bchn_test({"1 0 0 0 3", "pick", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #491 [Should Pass]: 1 0 | pick", "[vm][auto]") { run_bchn_test({"1 0", "pick", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #492 [Should Pass]: 1 0 0 0 3 | roll", "[vm][auto]") { run_bchn_test({"1 0 0 0 3", "roll", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #493 [Should Pass]: 1 0 | roll", "[vm][auto]") { run_bchn_test({"1 0", "roll", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #494 [Should Pass]: 1 0 0 | rot", "[vm][auto]") { run_bchn_test({"1 0 0", "rot", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #495 [Should Pass]: 1 0 | swap", "[vm][auto]") { run_bchn_test({"1 0", "swap", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #496 [Should Pass]: 0 1 | tuck", "[vm][auto]") { run_bchn_test({"0 1", "tuck", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #497 [Should Pass]: 1 | size", "[vm][auto]") { run_bchn_test({"1", "size", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #498 [Should Pass]: 0 0 | equal", "[vm][auto]") { run_bchn_test({"0 0", "equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #499 [Should Pass]: 0 0 | equalverify 1", "[vm][auto]") { run_bchn_test({"0 0", "equalverify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK
