// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 2 (tests 200 to 299)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #200 [Should Pass]: 1 0notequal | 1 equal", "[vm][auto]") { run_bchn_test({"1 0notequal", "1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #201 [Should Pass]: 111 0notequal | 1 equal", "[vm][auto]") { run_bchn_test({"111 0notequal", "1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #202 [Should Pass]: -111 0notequal | 1 equal", "[vm][auto]") { run_bchn_test({"-111 0notequal", "1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #203 [Should Pass]: 1 1 booland | nop", "[vm][auto]") { run_bchn_test({"1 1 booland", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #204 [Should Pass]: 1 0 booland | not", "[vm][auto]") { run_bchn_test({"1 0 booland", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #205 [Should Pass]: 0 1 booland | not", "[vm][auto]") { run_bchn_test({"0 1 booland", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #206 [Should Pass]: 0 0 booland | not", "[vm][auto]") { run_bchn_test({"0 0 booland", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #207 [Should Pass]: 16 17 booland | nop", "[vm][auto]") { run_bchn_test({"16 17 booland", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #208 [Should Pass]: 1 1 boolor | nop", "[vm][auto]") { run_bchn_test({"1 1 boolor", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #209 [Should Pass]: 1 0 boolor | nop", "[vm][auto]") { run_bchn_test({"1 0 boolor", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #210 [Should Pass]: 0 1 boolor | nop", "[vm][auto]") { run_bchn_test({"0 1 boolor", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #211 [Should Pass]: 0 0 boolor | not", "[vm][auto]") { run_bchn_test({"0 0 boolor", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #212 [Should Fail: EVAL_FALSE]: negative-0 negative-0 BOOLOR", "[vm][auto]") { run_bchn_test({"[80]", "dup boolor", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "negative-0 negative-0 BOOLOR"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #213 [Should Fail: EVAL_FALSE]: non-minimal-0  non-minimal-0 BOOLOR", "[vm][auto]") { run_bchn_test({"[00]", "dup boolor", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, " non-minimal-0  non-minimal-0 BOOLOR"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #214 [Should Pass]: -1 -1 BOOLOR", "[vm][auto]") { run_bchn_test({"[81]", "dup boolor", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "-1 -1 BOOLOR"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #215 [Should Fail: EVAL_FALSE]: negative-0 negative-0 BOOLAND", "[vm][auto]") { run_bchn_test({"[80]", "dup booland", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "negative-0 negative-0 BOOLAND"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #216 [Should Fail: EVAL_FALSE]: non-minimal-0  non-minimal-0 BOOLAND", "[vm][auto]") { run_bchn_test({"[00]", "dup booland", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, " non-minimal-0  non-minimal-0 BOOLAND"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #217 [Should Pass]: -1 -1 BOOLAND", "[vm][auto]") { run_bchn_test({"[81]", "dup booland", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "-1 -1 BOOLAND"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #218 [Should Pass]: non-minimal-0 NOT", "[vm][auto]") { run_bchn_test({"[00]", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "non-minimal-0 NOT"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #219 [Should Pass]: negative-0 NOT", "[vm][auto]") { run_bchn_test({"[80]", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "negative-0 NOT"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #220 [Should Fail: EVAL_FALSE]: negative 1 NOT", "[vm][auto]") { run_bchn_test({"[81]", "not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "negative 1 NOT"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #221 [Should Pass]: -0 0 NUMEQUAL", "[vm][auto]") { run_bchn_test({"[80] 0", "numequal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "-0 0 NUMEQUAL"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #222 [Should Pass]: non-minimal-0 0 NUMEQUAL", "[vm][auto]") { run_bchn_test({"[00] 0", "numequal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "non-minimal-0 0 NUMEQUAL"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #223 [Should Pass]: non-minimal-0 0 NUMEQUAL", "[vm][auto]") { run_bchn_test({"[0000] 0", "numequal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "non-minimal-0 0 NUMEQUAL"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #224 [Should Pass]: 16 17 boolor | nop", "[vm][auto]") { run_bchn_test({"16 17 boolor", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #225 [Should Pass]: 11 10 1 add | numequal", "[vm][auto]") { run_bchn_test({"11 10 1 add", "numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #226 [Should Pass]: 11 10 1 add | numequalverify 1", "[vm][auto]") { run_bchn_test({"11 10 1 add", "numequalverify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #227 [Should Pass]: 11 10 1 add | numnotequal not", "[vm][auto]") { run_bchn_test({"11 10 1 add", "numnotequal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #228 [Should Pass]: 111 10 1 add | numnotequal", "[vm][auto]") { run_bchn_test({"111 10 1 add", "numnotequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #229 [Should Pass]: 11 10 | lessthan not", "[vm][auto]") { run_bchn_test({"11 10", "lessthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #230 [Should Pass]: 4 4 | lessthan not", "[vm][auto]") { run_bchn_test({"4 4", "lessthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #231 [Should Pass]: 10 11 | lessthan", "[vm][auto]") { run_bchn_test({"10 11", "lessthan", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #232 [Should Pass]: -11 11 | lessthan", "[vm][auto]") { run_bchn_test({"-11 11", "lessthan", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #233 [Should Pass]: -11 -10 | lessthan", "[vm][auto]") { run_bchn_test({"-11 -10", "lessthan", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #234 [Should Pass]: 11 10 | greaterthan", "[vm][auto]") { run_bchn_test({"11 10", "greaterthan", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #235 [Should Pass]: 4 4 | greaterthan not", "[vm][auto]") { run_bchn_test({"4 4", "greaterthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #236 [Should Pass]: 10 11 | greaterthan not", "[vm][auto]") { run_bchn_test({"10 11", "greaterthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #237 [Should Pass]: -11 11 | greaterthan not", "[vm][auto]") { run_bchn_test({"-11 11", "greaterthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #238 [Should Pass]: -11 -10 | greaterthan not", "[vm][auto]") { run_bchn_test({"-11 -10", "greaterthan not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #239 [Should Pass]: 11 10 | lessthanorequal not", "[vm][auto]") { run_bchn_test({"11 10", "lessthanorequal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #240 [Should Pass]: 4 4 | lessthanorequal", "[vm][auto]") { run_bchn_test({"4 4", "lessthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #241 [Should Pass]: 10 11 | lessthanorequal", "[vm][auto]") { run_bchn_test({"10 11", "lessthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #242 [Should Pass]: -11 11 | lessthanorequal", "[vm][auto]") { run_bchn_test({"-11 11", "lessthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #243 [Should Pass]: -11 -10 | lessthanorequal", "[vm][auto]") { run_bchn_test({"-11 -10", "lessthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #244 [Should Pass]: 11 10 | greaterthanorequal", "[vm][auto]") { run_bchn_test({"11 10", "greaterthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #245 [Should Pass]: 4 4 | greaterthanorequal", "[vm][auto]") { run_bchn_test({"4 4", "greaterthanorequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #246 [Should Pass]: 10 11 | greaterthanorequal not", "[vm][auto]") { run_bchn_test({"10 11", "greaterthanorequal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #247 [Should Pass]: -11 11 | greaterthanorequal not", "[vm][auto]") { run_bchn_test({"-11 11", "greaterthanorequal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #248 [Should Pass]: -11 -10 | greaterthanorequal not", "[vm][auto]") { run_bchn_test({"-11 -10", "greaterthanorequal not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #249 [Should Pass]: 1 0 min | 0 numequal", "[vm][auto]") { run_bchn_test({"1 0 min", "0 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #250 [Should Pass]: 0 1 min | 0 numequal", "[vm][auto]") { run_bchn_test({"0 1 min", "0 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #251 [Should Pass]: -1 0 min | -1 numequal", "[vm][auto]") { run_bchn_test({"-1 0 min", "-1 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #252 [Should Pass]: 0 -2147483647 min | -2147483647 numequal", "[vm][auto]") { run_bchn_test({"0 -2147483647 min", "-2147483647 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #253 [Should Pass]: 0 -9223372036854775807 min | -9223372036854775807 numequal", "[vm][auto]") { run_bchn_test({"0 -9223372036854775807 min", "-9223372036854775807 numequal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #254 [Should Pass]: 2147483647 0 max | 2147483647 numequal", "[vm][auto]") { run_bchn_test({"2147483647 0 max", "2147483647 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #255 [Should Pass]: 9223372036854775807 0 max | 9223372036854775807 numequal", "[vm][auto]") { run_bchn_test({"9223372036854775807 0 max", "9223372036854775807 numequal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #256 [Should Pass]: 0 100 max | 100 numequal", "[vm][auto]") { run_bchn_test({"0 100 max", "100 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #257 [Should Pass]: -100 0 max | 0 numequal", "[vm][auto]") { run_bchn_test({"-100 0 max", "0 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #258 [Should Pass]: 0 -2147483647 max | 0 numequal", "[vm][auto]") { run_bchn_test({"0 -2147483647 max", "0 numequal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #259 [Should Pass]: 0 -9223372036854775807 max | 0 numequal", "[vm][auto]") { run_bchn_test({"0 -9223372036854775807 max", "0 numequal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #260 [Should Pass]: 0 0 1 | within", "[vm][auto]") { run_bchn_test({"0 0 1", "within", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #261 [Should Pass]: 1 0 1 | within not", "[vm][auto]") { run_bchn_test({"1 0 1", "within not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #262 [Should Pass]: 0 -2147483647 2147483647 | within", "[vm][auto]") { run_bchn_test({"0 -2147483647 2147483647", "within", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #263 [Should Pass]: 0 -9223372036854775807 9223372036854775807 | within", "[vm][auto]") { run_bchn_test({"0 -9223372036854775807 9223372036854775807", "within", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #264 [Should Pass]: -1 -100 100 | within", "[vm][auto]") { run_bchn_test({"-1 -100 100", "within", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #265 [Should Pass]: 11 -100 100 | within", "[vm][auto]") { run_bchn_test({"11 -100 100", "within", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #266 [Should Pass]: -2147483647 -100 100 | within not", "[vm][auto]") { run_bchn_test({"-2147483647 -100 100", "within not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #267 [Should Pass]: 2147483647 -100 100 | within not", "[vm][auto]") { run_bchn_test({"2147483647 -100 100", "within not", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #268 [Should Pass]: -9223372036854775807 -100 100 | within not", "[vm][auto]") { run_bchn_test({"-9223372036854775807 -100 100", "within not", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #269 [Should Pass]: 9223372036854775807 -100 100 | within not", "[vm][auto]") { run_bchn_test({"9223372036854775807 -100 100", "within not", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #270 [Should Pass]: 2147483647 2147483647 sub | 0 equal", "[vm][auto]") { run_bchn_test({"2147483647 2147483647 sub", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #271 [Should Pass]: >32 bit EQUAL is valid", "[vm][auto]") { run_bchn_test({"2147483647 dup add", "4294967294 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ">32 bit EQUAL is valid"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #272 [Should Pass]: 2147483647 negate dup add | -4294967294 equal", "[vm][auto]") { run_bchn_test({"2147483647 negate dup add", "-4294967294 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #273 [Should Pass]: 9223372036854775807 9223372036854775807 sub | 0 equal", "[vm][auto]") { run_bchn_test({"9223372036854775807 9223372036854775807 sub", "0 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

// KNUTH_OVERRIDE #146: error=OVERFLOW, reason=Knuth returns overflow for 64-bit arithmetic result exceeding max size, BCHN returns INVALID_NUMBER_RANGE_64_BIT
TEST_CASE("VM-AUTO #274 [Should Fail: OVERFLOW]: >64-bit is not valid", "[vm][auto]") { run_bchn_test({"9223372036854775807 dup add", "4294967294 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::overflow, ">64-bit is not valid [KNUTH_OVERRIDE: OVERFLOW]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OVERFLOW

// KNUTH_OVERRIDE #147: error=OVERFLOW, reason=Knuth returns overflow for 64-bit arithmetic result exceeding max size, BCHN returns INVALID_NUMBER_RANGE_64_BIT
TEST_CASE("VM-AUTO #275 [Should Fail: OVERFLOW]: <-2^63 is not valid", "[vm][auto]") { run_bchn_test({"9223372036854775807 negate dup add", "-4294967294 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::overflow, "<-2^63 is not valid [KNUTH_OVERRIDE: OVERFLOW]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OVERFLOW

TEST_CASE("VM-AUTO #276 [Should Pass]: '' | ripemd160 [9c1185a5c5e9fc54612808977ee8f548b2258d31] equal", "[vm][auto]") { run_bchn_test({"''", "ripemd160 [9c1185a5c5e9fc54612808977ee8f548b2258d31] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #277 [Should Pass]: 'a' | ripemd160 [0bdc9d2d256b3ee9daae347be6f4dc835a467ffe] equal", "[vm][auto]") { run_bchn_test({"'a'", "ripemd160 [0bdc9d2d256b3ee9daae347be6f4dc835a467ffe] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #278 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | ripemd160 [f71c27109c692c1b56bbdceb5b9d2865b3708dbc] equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "ripemd160 [f71c27109c692c1b56bbdceb5b9d2865b3708dbc] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #279 [Should Pass]: '' | sha1 [da39a3ee5e6b4b0d3255bfef95601890afd80709] equal", "[vm][auto]") { run_bchn_test({"''", "sha1 [da39a3ee5e6b4b0d3255bfef95601890afd80709] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #280 [Should Pass]: 'a' | sha1 [86f7e437faa5a7fce15d1ddcb9eaeaea377667b8] equal", "[vm][auto]") { run_bchn_test({"'a'", "sha1 [86f7e437faa5a7fce15d1ddcb9eaeaea377667b8] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #281 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | sha1 [32d10c7b8cf96570ca04ce37f2a19d84240d3a89] equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "sha1 [32d10c7b8cf96570ca04ce37f2a19d84240d3a89] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #282 [Should Pass]: '' | sha256 [e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855] equal", "[vm][auto]") { run_bchn_test({"''", "sha256 [e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #283 [Should Pass]: 'a' | sha256 [ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb] equal", "[vm][auto]") { run_bchn_test({"'a'", "sha256 [ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #284 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | sha256 [71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73] equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "sha256 [71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #285 [Should Pass]: '' | dup hash160 swap sha256 ripemd160 equal", "[vm][auto]") { run_bchn_test({"''", "dup hash160 swap sha256 ripemd160 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #286 [Should Pass]: '' | dup hash256 swap sha256 sha256 equal", "[vm][auto]") { run_bchn_test({"''", "dup hash256 swap sha256 sha256 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #287 [Should Pass]: '' | nop hash160 [b472a266d0bd89c13706a4132ccfb16f7c3b9fcb] equal", "[vm][auto]") { run_bchn_test({"''", "nop hash160 [b472a266d0bd89c13706a4132ccfb16f7c3b9fcb] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #288 [Should Pass]: 'a' | hash160 nop [994355199e516ff76c4fa4aab39337b9d84cf12b] equal", "[vm][auto]") { run_bchn_test({"'a'", "hash160 nop [994355199e516ff76c4fa4aab39337b9d84cf12b] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #289 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | hash160 [1.c286a1af0947f58d1ad787385b1c2c4a976f9e71] equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "hash160 [1.c286a1af0947f58d1ad787385b1c2c4a976f9e71] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #290 [Should Pass]: '' | hash256 [5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456] equal", "[vm][auto]") { run_bchn_test({"''", "hash256 [5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #291 [Should Pass]: 'a' | hash256 [bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8] equal", "[vm][auto]") { run_bchn_test({"'a'", "hash256 [bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #292 [Should Pass]: 'abcdefghijklmnopqrstuvwxyz' | hash256 [1.ca139bc10c2f660da42666f72e89a225936fc60f193c161124a672050c434671] equal", "[vm][auto]") { run_bchn_test({"'abcdefghijklmnopqrstuvwxyz'", "hash256 [1.ca139bc10c2f660da42666f72e89a225936fc60f193c161124a672050c434671] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #8: fork=P2SH, reason=????
TEST_CASE("VM-AUTO #293 [Should Pass]: 1 | nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 1 equal", "[vm][auto]") { run_bchn_test({"1", "nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 1 equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, " [KNUTH_OVERRIDE: FORK: P2SH]"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #294 [Should Pass]: 'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 | 'NOP_1_to_10' equal", "[vm][auto]") { run_bchn_test({"'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'NOP_1_to_10' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #295 [Should Pass]: Discourage NOPx flag allows OP_NOP", "[vm][auto]") { run_bchn_test({"1", "nop", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Discourage NOPx flag allows OP_NOP"}); } // flags: P2SH,STRICTENC,DISCOURAGE_UPGRADABLE_NOPS, expected: OK

TEST_CASE("VM-AUTO #296 [Should Pass]: Discouraged NOPs are allowed if not executed", "[vm][auto]") { run_bchn_test({"0", "if nop10 endif 1", kth::domain::machine::script_flags::bch_discourage_upgradable_nops | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Discouraged NOPs are allowed if not executed"}); } // flags: P2SH,STRICTENC,DISCOURAGE_UPGRADABLE_NOPS, expected: OK

TEST_CASE("VM-AUTO #297 [Should Pass]: 0 | if checkdatasig else 1 endif", "[vm][auto]") { run_bchn_test({"0", "if checkdatasig else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #298 [Should Pass]: 0 | if checkdatasigverify else 1 endif", "[vm][auto]") { run_bchn_test({"0", "if checkdatasigverify else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #299 [Should Pass]: 0 | if reversebytes else 1 endif", "[vm][auto]") { run_bchn_test({"0", "if reversebytes else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK
