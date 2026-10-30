// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "bchn_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_bchn_bchn_2 {
// Source: script_vm_auto_10.cpp
// KNUTH_OVERRIDE #45: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #48: error=KTH_OP_XOR, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141)
// KNUTH_OVERRIDE #49: error=KTH_OP_XOR, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141)
// KNUTH_OVERRIDE #50: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_xor error (bitcoin:141) - different operand sizes
// KNUTH_OVERRIDE #51: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #58: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #59: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #60: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #61: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #62: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #63: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #64: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #65: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #66: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #57: error=KTH_OP_DIV_BY_ZERO, fork=KTH_PYTHAGORAS, reason=
// KNUTH_OVERRIDE #67: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #52: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
// KNUTH_OVERRIDE #53: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
// KNUTH_OVERRIDE #68: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #69: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #70: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #71: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #72: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #73: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #74: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #75: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #76: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #77: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #78: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #79: fork=KTH_PYTHAGORAS, reason=DIV requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #54: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #55: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #56: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #88: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #89: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #90: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #91: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #92: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #93: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #94: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #95: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #96: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #80: error=KTH_OP_MOD_BY_ZERO, fork=KTH_PYTHAGORAS, reason=
// KNUTH_OVERRIDE #97: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #81: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
// KNUTH_OVERRIDE #82: error=KTH_INSUFFICIENT_MAIN_STACK, fork=KTH_PYTHAGORAS, reason=Stack has insufficient elements for arithmetic op
// KNUTH_OVERRIDE #98: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #99: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #100: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #101: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #102: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #103: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #104: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #105: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #106: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #107: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #108: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #109: fork=KTH_PYTHAGORAS, reason=MOD requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #83: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #84: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #85: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
// KNUTH_OVERRIDE #87: fork=KTH_PYTHAGORAS, reason=INVALID_NUMBER_RANGE now maps to invalid_operand_size via ERROR_MAP
static constexpr bchn_vector rows_0[] = {
    {1000, "1 1", "xor [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1001, "0", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "XOR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"},
    {1002, "", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "XOR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"},
    {1003, "0 1", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "XOR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"},
    {1004, "[ab] [cd]", "xor [66] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1005, "1 1", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1006, "1 -1", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1007, "-1 1", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1008, "-1 -1", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1009, "28 21", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1010, "12 -7", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1011, "-32 29", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1012, "-42 -27", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1013, "0 123", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1014, "511 0", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::division_by_zero, "DIV, divide by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV_BY_ZERO, FORK: KTH_PYTHAGORAS]"},
    {1015, "1 1", "div depth 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1016, "1", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"},
    {1017, "0", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"},
    {1018, "2147483647 1", "div 2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1019, "1 2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1020, "2147483647 2147483647", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1021, "-2147483647 1", "div -2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1022, "-1 2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1023, "-2147483647 2147483647", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1024, "2147483647 -1", "div -2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1025, "1 -2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1026, "2147483647 -2147483647", "div -1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1027, "-2147483647 -1", "div 2147483647 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1028, "-1 -2147483647", "div 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1029, "-2147483647 -2147483647", "div 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1030, "2147483648 1", "div", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers"},
    {1031, "1 2147483648", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1032, "-2147483648 1", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1033, "1 -2147483648", "div", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1034, "2147483648 1", "div", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1035, "1 2147483648", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1036, "-2147483648 1", "div -2147483648 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1037, "1 -2147483648", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1038, "9223372036854775807 1", "div 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1039, "1 9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1040, "9223372036854775807 9223372036854775807", "div 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1041, "-9223372036854775807 1", "div -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1042, "-1 9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1043, "-9223372036854775807 9223372036854775807", "div -1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1044, "9223372036854775807 -1", "div -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1045, "1 -9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1046, "9223372036854775807 -9223372036854775807", "div -1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1047, "-9223372036854775807 -1", "div 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1048, "-1 -9223372036854775807", "div 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1049, "-9223372036854775807 -9223372036854775807", "div 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1050, "1 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1051, "-1 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1052, "1 -1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1053, "-1 -1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1054, "82 23", "mod 13 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1055, "8 -3", "mod 2 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1056, "-71 13", "mod -6 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1057, "-110 -31", "mod -17 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1058, "0 1", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1059, "1 0", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::division_by_zero, "MOD, modulo by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD_BY_ZERO, FORK: KTH_PYTHAGORAS]"},
    {1060, "1 1", "mod depth 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1061, "1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"},
    {1062, "0", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_INSUFFICIENT_MAIN_STACK, FORK: KTH_PYTHAGORAS]"},
    {1063, "2147483647 123", "mod 79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1064, "123 2147483647", "mod 123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1065, "2147483647 2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1066, "-2147483647 123", "mod -79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1067, "-123 2147483647", "mod -123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1068, "-2147483647 2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1069, "2147483647 -123", "mod 79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1070, "123 -2147483647", "mod 123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1071, "2147483647 -2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1072, "-2147483647 -123", "mod -79 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1073, "-123 -2147483647", "mod -123 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1074, "-2147483647 -2147483647", "mod 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1075, "2147483648 1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1076, "1 2147483648", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1077, "-2147483648 1", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1078, "1 -2147483648", "mod", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::invalid_operand_size, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {1079, "2147483648 1", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1080, "1 2147483648", "mod 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1081, "-2147483648 1", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1082, "1 -2147483648", "mod 1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"},
    {1083, "9223372036854775807 123", "mod 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1084, "123 9223372036854775807", "mod 123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1085, "9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1086, "-9223372036854775807 123", "mod -7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1087, "-123 9223372036854775807", "mod -123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1088, "-9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1089, "9223372036854775807 -123", "mod 7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1090, "123 -9223372036854775807", "mod 123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1091, "9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1092, "-9223372036854775807 -123", "mod -7 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1093, "-123 -9223372036854775807", "mod -123 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1094, "-9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Check boundary condition"},
    {1095, "", "equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "EQUAL must error when there are no stack items"},
    {1096, "0", "equal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "EQUAL must error when there are not 2 stack items"},
    {1097, "0 1", "equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {1098, "1 1 add", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {1099, "11 1 add 12 sub", "11 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
};
static constexpr bchn_vector_group group = {
    "script_vm_auto_10.cpp", rows_0
};
} // namespace kth::test::data_bchn_bchn_2
