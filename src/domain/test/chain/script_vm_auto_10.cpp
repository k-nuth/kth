// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 10 (tests 1000 to 1099)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

// KNUTH_OVERRIDE #45: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1000 [Should Pass]: XOR, simple and", "[vm][auto]") { run_bchn_test({"1 1", "xor [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #48: error=KTH_OP_XOR, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141)
TEST_CASE("VM-AUTO #1001 [Should Fail: KTH_OP_XOR]: XOR, invalid parameter count", "[vm][auto]") { run_bchn_test({"0", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "XOR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_XOR

// KNUTH_OVERRIDE #49: error=KTH_OP_XOR, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141)
TEST_CASE("VM-AUTO #1002 [Should Fail: KTH_OP_XOR]: XOR, empty stack", "[vm][auto]") { run_bchn_test({"", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "XOR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_XOR

// KNUTH_OVERRIDE #50: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141) - different operand sizes
TEST_CASE("VM-AUTO #1003 [Should Fail: KTH_OPERAND_SIZE_MISMATCH]: XOR, different operand size", "[vm][auto]") { run_bchn_test({"0 1", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "XOR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OPERAND_SIZE_MISMATCH

// KNUTH_OVERRIDE #51: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1004 [Should Pass]: XOR, more complex operands", "[vm][auto]") { run_bchn_test({"[ab] [cd]", "xor [66] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #58: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1005 [Should Pass]: 1 1 | div 1 equal", "[vm][auto]") { run_bchn_test({"1 1", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #59: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1006 [Should Pass]: 1 -1 | div -1 equal", "[vm][auto]") { run_bchn_test({"1 -1", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #60: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1007 [Should Pass]: -1 1 | div -1 equal", "[vm][auto]") { run_bchn_test({"-1 1", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #61: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1008 [Should Pass]: -1 -1 | div 1 equal", "[vm][auto]") { run_bchn_test({"-1 -1", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #62: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1009 [Should Pass]: Round towards zero", "[vm][auto]") { run_bchn_test({"28 21", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #63: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1010 [Should Pass]: Round towards zero", "[vm][auto]") { run_bchn_test({"12 -7", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #64: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1011 [Should Pass]: Round towards zero", "[vm][auto]") { run_bchn_test({"-32 29", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #65: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1012 [Should Pass]: Round towards zero", "[vm][auto]") { run_bchn_test({"-42 -27", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #66: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1013 [Should Pass]: 0 123 | div 0 equal", "[vm][auto]") { run_bchn_test({"0 123", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #57: error=KTH_OP_DIV_BY_ZERO, fork=KTH_PYTHAGORAS, reason=
TEST_CASE("VM-AUTO #1014 [Should Fail: KTH_OP_DIV_BY_ZERO]: DIV, divide by zero", "[vm][auto]") { run_bchn_test({"511 0", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::division_by_zero, "DIV, divide by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV_BY_ZERO, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_DIV_BY_ZERO

// KNUTH_OVERRIDE #67: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1015 [Should Pass]: Stack depth correct", "[vm][auto]") { run_bchn_test({"1 1", "div depth 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #52: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
TEST_CASE("VM-AUTO #1016 [Should Fail: KTH_INSUFFICIENT_MAIN_STACK]: Not enough operands", "[vm][auto]") { run_bchn_test({"1", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_INSUFFICIENT_MAIN_STACK

// KNUTH_OVERRIDE #53: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
TEST_CASE("VM-AUTO #1017 [Should Fail: KTH_INSUFFICIENT_MAIN_STACK]: Not enough operands", "[vm][auto]") { run_bchn_test({"0", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_INSUFFICIENT_MAIN_STACK

// KNUTH_OVERRIDE #68: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1018 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 1", "div 2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #69: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1019 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"1 2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #70: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1020 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 2147483647", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #71: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1021 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 1", "div -2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #72: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1022 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-1 2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #73: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1023 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 2147483647", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #74: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1024 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 -1", "div -2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #75: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1025 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"1 -2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #76: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1026 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 -2147483647", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #77: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1027 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 -1", "div 2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #78: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1028 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-1 -2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #79: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1029 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 -2147483647", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #1030 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"2147483648 1", "div", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #54: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1031 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"1 2147483648", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #55: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1032 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"-2147483648 1", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #56: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1033 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"1 -2147483648", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1034 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"2147483648 1", "div", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1035 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"1 2147483648", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1036 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"-2147483648 1", "div -2147483648 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1037 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"1 -2147483648", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1038 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 1", "div 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1039 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"1 9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1040 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 9223372036854775807", "div 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1041 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 1", "div -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1042 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-1 9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1043 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 9223372036854775807", "div -1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1044 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 -1", "div -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1045 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"1 -9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1046 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 -9223372036854775807", "div -1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1047 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 -1", "div 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1048 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-1 -9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1049 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 -9223372036854775807", "div 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

// KNUTH_OVERRIDE #88: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1050 [Should Pass]: 1 1 | mod 0 equal", "[vm][auto]") { run_bchn_test({"1 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #89: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1051 [Should Pass]: -1 1 | mod 0 equal", "[vm][auto]") { run_bchn_test({"-1 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #90: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1052 [Should Pass]: 1 -1 | mod 0 equal", "[vm][auto]") { run_bchn_test({"1 -1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #91: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1053 [Should Pass]: -1 -1 | mod 0 equal", "[vm][auto]") { run_bchn_test({"-1 -1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #92: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1054 [Should Pass]: 82 23 | mod 13 equal", "[vm][auto]") { run_bchn_test({"82 23", "mod 13 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #93: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1055 [Should Pass]: 8 -3 | mod 2 equal", "[vm][auto]") { run_bchn_test({"8 -3", "mod 2 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #94: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1056 [Should Pass]: -71 13 | mod -6 equal", "[vm][auto]") { run_bchn_test({"-71 13", "mod -6 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #95: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1057 [Should Pass]: -110 -31 | mod -17 equal", "[vm][auto]") { run_bchn_test({"-110 -31", "mod -17 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #96: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1058 [Should Pass]: 0 1 | mod 0 equal", "[vm][auto]") { run_bchn_test({"0 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #80: error=KTH_OP_MOD_BY_ZERO, fork=KTH_PYTHAGORAS, reason=
TEST_CASE("VM-AUTO #1059 [Should Fail: KTH_OP_MOD_BY_ZERO]: MOD, modulo by zero", "[vm][auto]") { run_bchn_test({"1 0", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::division_by_zero, "MOD, modulo by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD_BY_ZERO, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_MOD_BY_ZERO

// KNUTH_OVERRIDE #97: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1060 [Should Pass]: Stack depth correct", "[vm][auto]") { run_bchn_test({"1 1", "mod depth 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #81: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
TEST_CASE("VM-AUTO #1061 [Should Fail: KTH_INSUFFICIENT_MAIN_STACK]: Not enough operands", "[vm][auto]") { run_bchn_test({"1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_INSUFFICIENT_MAIN_STACK

// KNUTH_OVERRIDE #82: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
TEST_CASE("VM-AUTO #1062 [Should Fail: KTH_INSUFFICIENT_MAIN_STACK]: Not enough operands", "[vm][auto]") { run_bchn_test({"0", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_INSUFFICIENT_MAIN_STACK

// KNUTH_OVERRIDE #98: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1063 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 123", "mod 79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #99: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1064 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"123 2147483647", "mod 123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #100: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1065 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #101: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1066 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 123", "mod -79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #102: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1067 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-123 2147483647", "mod -123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #103: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1068 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #104: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1069 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 -123", "mod 79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #105: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1070 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"123 -2147483647", "mod 123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #106: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1071 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"2147483647 -2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #107: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1072 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 -123", "mod -79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #108: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1073 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-123 -2147483647", "mod -123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #109: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #1074 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-2147483647 -2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #83: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1075 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"2147483648 1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #84: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1076 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"1 2147483648", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #85: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1077 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"-2147483648 1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

// KNUTH_OVERRIDE #87: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
TEST_CASE("VM-AUTO #1078 [Should Fail: INVALID_NUMBER_RANGE]: We cannot do math on 5-byte integers", "[vm][auto]") { run_bchn_test({"1 -2147483648", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1079 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"2147483648 1", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1080 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"1 2147483648", "mod 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1081 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"-2147483648 1", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1082 [Should Pass]: We can do math on 5-byte integers on 64-bit mode", "[vm][auto]") { run_bchn_test({"1 -2147483648", "mod 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1083 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 123", "mod 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1084 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"123 9223372036854775807", "mod 123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1085 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1086 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 123", "mod -7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1087 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-123 9223372036854775807", "mod -123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1088 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1089 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 -123", "mod 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1090 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"123 -9223372036854775807", "mod 123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1091 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1092 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 -123", "mod -7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1093 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-123 -9223372036854775807", "mod -123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1094 [Should Pass]: Check boundary condition", "[vm][auto]") { run_bchn_test({"-9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1095 [Should Fail: INVALID_STACK_OPERATION]: EQUAL must error when there are no stack items", "[vm][auto]") { run_bchn_test({"", "equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "EQUAL must error when there are no stack items"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1096 [Should Fail: INVALID_STACK_OPERATION]: EQUAL must error when there are not 2 stack items", "[vm][auto]") { run_bchn_test({"0", "equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "EQUAL must error when there are not 2 stack items"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1097 [Should Fail: EVAL_FALSE]: 0 1 | equal", "[vm][auto]") { run_bchn_test({"0 1", "equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #1098 [Should Fail: EVAL_FALSE]: 1 1 add | 0 equal", "[vm][auto]") { run_bchn_test({"1 1 add", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #1099 [Should Fail: EVAL_FALSE]: 11 1 add 12 sub | 11 equal", "[vm][auto]") { run_bchn_test({"11 1 add 12 sub", "11 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE
