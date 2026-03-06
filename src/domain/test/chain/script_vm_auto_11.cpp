// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 11 (tests 1100 to 1199)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #1100 [Should Fail: INVALID_STACK_OPERATION]:  | checkdatasig", "[vm][auto]") { run_bchn_test({"", "checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1101 [Should Fail: INVALID_STACK_OPERATION]: 0 | checkdatasig", "[vm][auto]") { run_bchn_test({"0", "checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1102 [Should Fail: INVALID_STACK_OPERATION]: 0 0 | checkdatasig", "[vm][auto]") { run_bchn_test({"0 0", "checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1103 [Should Fail: PUBKEYTYPE]: 0 0 | 0 checkdatasig", "[vm][auto]") { run_bchn_test({"0 0", "0 checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::pubkey_type, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #1104 [Should Fail: EVAL_FALSE]: 0 0 | [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", "[vm][auto]") { run_bchn_test({"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #1105 [Should Fail: EVAL_FALSE]: Check that NULLFAIL trigger only when specified", "[vm][auto]") { run_bchn_test({"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "Check that NULLFAIL trigger only when specified"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #1106 [Should Fail: NULLFAIL]: [3006020101020101] 0 | [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", "[vm][auto]") { run_bchn_test({"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::sig_nullfail, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: NULLFAIL

TEST_CASE("VM-AUTO #1107 [Should Fail: SIG_DER]: Ensure that sighashtype is ignored", "[vm][auto]") { run_bchn_test({"[300602010102010101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_signature_lax_encoding, "Ensure that sighashtype is ignored"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER

TEST_CASE("VM-AUTO #1108 [Should Fail: SIG_DER]: Non canonical DER encoding", "[vm][auto]") { run_bchn_test({"[300702010102020001] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_signature_lax_encoding, "Non canonical DER encoding"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER

TEST_CASE("VM-AUTO #1109 [Should Fail: INVALID_STACK_OPERATION]:  | checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"", "checkdatasigverify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1110 [Should Fail: INVALID_STACK_OPERATION]: 0 | checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"0", "checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1111 [Should Fail: INVALID_STACK_OPERATION]: 0 0 | checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"0 0", "checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #1112 [Should Fail: PUBKEYTYPE]: 0 0 | 0 checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"0 0", "0 checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::pubkey_type, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #1113 [Should Fail: CHECKDATASIGVERIFY]: 0 0 | [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: CHECKDATASIGVERIFY

TEST_CASE("VM-AUTO #1114 [Should Fail: CHECKDATASIGVERIFY]: Check that NULLFAIL trigger only when specified", "[vm][auto]") { run_bchn_test({"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, "Check that NULLFAIL trigger only when specified"}); } // flags: P2SH,STRICTENC, expected: CHECKDATASIGVERIFY

TEST_CASE("VM-AUTO #1115 [Should Fail: NULLFAIL]: [3006020101020101] 0 | [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", "[vm][auto]") { run_bchn_test({"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::sig_nullfail, ""}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: NULLFAIL

TEST_CASE("VM-AUTO #1116 [Should Fail: SIG_DER]: Ensure that sighashtype is ignored", "[vm][auto]") { run_bchn_test({"[300602010102010101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_signature_lax_encoding, "Ensure that sighashtype is ignored"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER

TEST_CASE("VM-AUTO #1117 [Should Fail: SIG_DER]: Non canonical DER encoding", "[vm][auto]") { run_bchn_test({"[300702010102020001] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_signature_lax_encoding, "Non canonical DER encoding"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER

TEST_CASE("VM-AUTO #1118 [Should Fail: INVALID_NUMBER_RANGE]: arithmetic operands must be in range [-2^31 + 1, 2^31 - 1]", "[vm][auto]") { run_bchn_test({"2147483648 0 add", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "arithmetic operands must be in range [-2^31 + 1, 2^31 - 1]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1119 [Should Fail: INVALID_NUMBER_RANGE]: arithmetic operands must be in range [-2^31 + 1, 2^31 - 1]", "[vm][auto]") { run_bchn_test({"-2147483648 0 add", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "arithmetic operands must be in range [-2^31 + 1, 2^31 - 1]"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1120 [Should Fail: INVALID_NUMBER_RANGE]: NUMEQUAL must be in numeric range", "[vm][auto]") { run_bchn_test({"2147483647 dup add", "4294967294 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "NUMEQUAL must be in numeric range"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1121 [Should Pass]: arithmetic operands are within range [-2^63 + 1, 2^63 - 1]", "[vm][auto]") { run_bchn_test({"2147483648 0 add", "nop", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "arithmetic operands are within range [-2^63 + 1, 2^63 - 1]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1122 [Should Pass]: arithmetic operands are within range [-2^63 + 1, 2^63 - 1]", "[vm][auto]") { run_bchn_test({"-2147483648 0 add", "nop", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "arithmetic operands are within range [-2^63 + 1, 2^63 - 1]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1123 [Should Pass]: NUMEQUAL is within the proper integer range", "[vm][auto]") { run_bchn_test({"2147483647 dup add", "4294967294 numequal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUMEQUAL is within the proper integer range"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1124 [Should Fail: INVALID_NUMBER_RANGE]: NOT is an arithmetic operand", "[vm][auto]") { run_bchn_test({"'abcdef' not", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "NOT is an arithmetic operand"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #1125 [Should Fail: DISABLED_OPCODE]: disabled", "[vm][auto]") { run_bchn_test({"2 dup mul", "4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #1126 [Should Pass]: OP_MUL re-enabled", "[vm][auto]") { run_bchn_test({"2 dup mul", "4 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "OP_MUL re-enabled"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1127 [Should Pass]: 4611686018427387903 2 mul | 9223372036854775806 equal", "[vm][auto]") { run_bchn_test({"4611686018427387903 2 mul", "9223372036854775806 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #1128 [Should Pass]: 1317624576693539401 7 mul | 9223372036854775807 equal", "[vm][auto]") { run_bchn_test({"1317624576693539401 7 mul", "9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

// KNUTH_OVERRIDE #131: error=KTH_OVERFLOW, reason=MUL overflow. Knuth returns overflow, BCHN returns INVALID_NUMBER_RANGE_64_BIT.
TEST_CASE("VM-AUTO #1129 [Should Fail: KTH_OVERFLOW]: 64-bit integer overflow detected", "[vm][auto]") { run_bchn_test({"4611686018427387904 2 mul", "1 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::overflow, "64-bit integer overflow detected [KNUTH_OVERRIDE: KTH_OVERFLOW]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: KTH_OVERFLOW

TEST_CASE("VM-AUTO #1130 [Should Fail: DISABLED_OPCODE]: disabled", "[vm][auto]") { run_bchn_test({"2 lshiftnum", "4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #1131 [Should Fail: DISABLED_OPCODE]: disabled", "[vm][auto]") { run_bchn_test({"2 rshiftnum", "1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #1132 [Should Fail: DISABLED_OPCODE]: disabled", "[vm][auto]") { run_bchn_test({"2 2 lshiftbin", "8 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #1133 [Should Fail: DISABLED_OPCODE]: disabled", "[vm][auto]") { run_bchn_test({"2 1 rshiftbin", "1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

// KNUTH_OVERRIDE #9: error=EVAL_FALSE, reason=Knuth executes CLTV/CSV as NOPs when bip65/bip112 not enabled, reaches EQUAL which fails with stack_false
TEST_CASE("VM-AUTO #1134 [Should Fail: EVAL_FALSE]: 1 | nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 2 equal", "[vm][auto]") { run_bchn_test({"1", "nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, " [KNUTH_OVERRIDE: EVAL_FALSE]"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

// KNUTH_OVERRIDE #13: error=EVAL_FALSE, reason=Knuth executes CLTV/CSV as NOPs when bip65/bip112 not enabled, reaches EQUAL which fails with stack_false
TEST_CASE("VM-AUTO #1135 [Should Fail: EVAL_FALSE]: 'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 | 'NOP_1_to_11' equal", "[vm][auto]") { run_bchn_test({"'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'NOP_1_to_11' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, " [KNUTH_OVERRIDE: EVAL_FALSE]"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #1136 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop1", "[vm][auto]") { run_bchn_test({"1", "nop1", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1137 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop4", "[vm][auto]") { run_bchn_test({"1", "nop4", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1138 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop5", "[vm][auto]") { run_bchn_test({"1", "nop5", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1139 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop6", "[vm][auto]") { run_bchn_test({"1", "nop6", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1140 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop7", "[vm][auto]") { run_bchn_test({"1", "nop7", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1141 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop8", "[vm][auto]") { run_bchn_test({"1", "nop8", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1142 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop9", "[vm][auto]") { run_bchn_test({"1", "nop9", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1143 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: 1 | nop10", "[vm][auto]") { run_bchn_test({"1", "nop10", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, ""}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1144 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: Discouraged NOP10 in scriptSig", "[vm][auto]") { run_bchn_test({"nop10", "1", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, "Discouraged NOP10 in scriptSig"}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1145 [Should Fail: DISCOURAGE_UPGRADABLE_NOPS]: Discouraged NOP10 in redeemScript", "[vm][auto]") { run_bchn_test({"1 [b9]", "hash160 [15727299b05b45fdaf9ac9ecf7565cfe27c3e567] equal", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bip16_rule, kth::error::operation_failed, "Discouraged NOP10 in redeemScript"}); } // flags: P2SH,DISCOURAGE_UPGRADABLE_NOPS, expected: DISCOURAGE_UPGRADABLE_NOPS

TEST_CASE("VM-AUTO #1146 [Should Fail: BAD_OPCODE]: opcode 0x50 is reserved", "[vm][auto]") { run_bchn_test({"0x50", "1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "opcode 0x50 is reserved"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1147 [Should Fail: BAD_OPCODE]: available, unassigned codepoints, invalid if executed", "[vm][auto]") { run_bchn_test({"1", "if 0xbd else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "available, unassigned codepoints, invalid if executed"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1148 [Should Fail: BAD_OPCODE]: 1 | if 0xbe else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xbe else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1149 [Should Fail: BAD_OPCODE]: 1 | if 0xbf else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xbf else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1150 [Should Fail: BAD_OPCODE]: TOKEN_PREFIX; invalid if executed", "[vm][auto]") { run_bchn_test({"1", "if 0xef else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "TOKEN_PREFIX; invalid if executed"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1151 [Should Fail: BAD_OPCODE]: opcodes for native introspection (not activated here)", "[vm][auto]") { run_bchn_test({"1", "if inputindex else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "opcodes for native introspection (not activated here)"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1152 [Should Fail: BAD_OPCODE]: 1 | if activebytecode else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if activebytecode else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1153 [Should Fail: BAD_OPCODE]: 1 | if txversion else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if txversion else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1154 [Should Fail: BAD_OPCODE]: 1 | if txinputcount else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if txinputcount else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1155 [Should Fail: BAD_OPCODE]: 1 | if txoutputcount else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if txoutputcount else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1156 [Should Fail: BAD_OPCODE]: 1 | if txlocktime else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if txlocktime else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1157 [Should Fail: BAD_OPCODE]: 1 | if utxovalue else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if utxovalue else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1158 [Should Fail: BAD_OPCODE]: 1 | if utxobytecode else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if utxobytecode else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1159 [Should Fail: BAD_OPCODE]: 1 | if outpointtxhash else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if outpointtxhash else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1160 [Should Fail: BAD_OPCODE]: 1 | if outpointindex else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if outpointindex else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1161 [Should Fail: BAD_OPCODE]: 1 | if inputbytecode else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if inputbytecode else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1162 [Should Fail: BAD_OPCODE]: 1 | if inputsequencenumber else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if inputsequencenumber else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1163 [Should Fail: BAD_OPCODE]: 1 | if outputvalue else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if outputvalue else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1164 [Should Fail: BAD_OPCODE]: 1 | if outputbytecode else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if outputbytecode else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1165 [Should Fail: BAD_OPCODE]: OP_RESERVED3, invalid if executed", "[vm][auto]") { run_bchn_test({"1", "if reserved3 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "OP_RESERVED3, invalid if executed"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1166 [Should Fail: BAD_OPCODE]: OP_RESERVED4, invalid if executed", "[vm][auto]") { run_bchn_test({"1", "if reserved4 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "OP_RESERVED4, invalid if executed"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1167 [Should Fail: BAD_OPCODE]: opcodes >= FIRST_UNDEFINED_OP_VALUE invalid if executed", "[vm][auto]") { run_bchn_test({"1", "if 0xd6 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "opcodes >= FIRST_UNDEFINED_OP_VALUE invalid if executed"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1168 [Should Fail: BAD_OPCODE]: 1 | if 0xd7 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xd7 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1169 [Should Fail: BAD_OPCODE]: 1 | if 0xd8 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xd8 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1170 [Should Fail: BAD_OPCODE]: 1 | if 0xd9 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xd9 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1171 [Should Fail: BAD_OPCODE]: 1 | if 0xda else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xda else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1172 [Should Fail: BAD_OPCODE]: 1 | if 0xdb else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xdb else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1173 [Should Fail: BAD_OPCODE]: 1 | if 0xdc else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xdc else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1174 [Should Fail: BAD_OPCODE]: 1 | if 0xdd else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xdd else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1175 [Should Fail: BAD_OPCODE]: 1 | if 0xde else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xde else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1176 [Should Fail: BAD_OPCODE]: 1 | if 0xdf else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xdf else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1177 [Should Fail: BAD_OPCODE]: 1 | if 0xe0 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe0 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1178 [Should Fail: BAD_OPCODE]: 1 | if 0xe1 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe1 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1179 [Should Fail: BAD_OPCODE]: 1 | if 0xe2 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe2 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1180 [Should Fail: BAD_OPCODE]: 1 | if 0xe3 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe3 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1181 [Should Fail: BAD_OPCODE]: 1 | if 0xe4 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe4 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1182 [Should Fail: BAD_OPCODE]: 1 | if 0xe5 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe5 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1183 [Should Fail: BAD_OPCODE]: 1 | if 0xe6 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe6 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1184 [Should Fail: BAD_OPCODE]: 1 | if 0xe7 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe7 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1185 [Should Fail: BAD_OPCODE]: 1 | if 0xe8 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe8 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1186 [Should Fail: BAD_OPCODE]: 1 | if 0xe9 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xe9 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1187 [Should Fail: BAD_OPCODE]: 1 | if 0xea else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xea else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1188 [Should Fail: BAD_OPCODE]: 1 | if 0xeb else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xeb else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1189 [Should Fail: BAD_OPCODE]: 1 | if 0xec else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xec else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1190 [Should Fail: BAD_OPCODE]: 1 | if 0xed else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xed else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1191 [Should Fail: BAD_OPCODE]: 1 | if 0xee else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xee else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1192 [Should Fail: BAD_OPCODE]: 1 | if 0xef else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xef else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1193 [Should Fail: BAD_OPCODE]: 1 | if 0xf0 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf0 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1194 [Should Fail: BAD_OPCODE]: 1 | if 0xf1 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf1 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1195 [Should Fail: BAD_OPCODE]: 1 | if 0xf2 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf2 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1196 [Should Fail: BAD_OPCODE]: 1 | if 0xf3 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf3 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1197 [Should Fail: BAD_OPCODE]: 1 | if 0xf4 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf4 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1198 [Should Fail: BAD_OPCODE]: 1 | if 0xf5 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf5 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #1199 [Should Fail: BAD_OPCODE]: 1 | if 0xf6 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 0xf6 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, ""}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE
