// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 8 (tests 800 to 899)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #800 [Should Fail: EVAL_FALSE]: 0 | dup if endif", "[vm][auto]") { run_bchn_test({"0", "dup if endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #801 [Should Fail: EVAL_FALSE]: 0 | if 1 endif", "[vm][auto]") { run_bchn_test({"0", "if 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #802 [Should Fail: EVAL_FALSE]: 0 | dup if else endif", "[vm][auto]") { run_bchn_test({"0", "dup if else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #803 [Should Fail: EVAL_FALSE]: 0 | if 1 else endif", "[vm][auto]") { run_bchn_test({"0", "if 1 else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #804 [Should Fail: EVAL_FALSE]: 0 | notif else 1 endif", "[vm][auto]") { run_bchn_test({"0", "notif else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #805 [Should Fail: EVAL_FALSE]: 0 1 | if if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"0 1", "if if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #806 [Should Fail: EVAL_FALSE]: 0 0 | if if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"0 0", "if if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #807 [Should Fail: EVAL_FALSE]: 1 0 | if if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"1 0", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #808 [Should Fail: EVAL_FALSE]: 0 1 | if if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"0 1", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #809 [Should Fail: EVAL_FALSE]: 0 0 | notif if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"0 0", "notif if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #810 [Should Fail: EVAL_FALSE]: 0 1 | notif if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"0 1", "notif if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #811 [Should Fail: EVAL_FALSE]: 1 1 | notif if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"1 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #812 [Should Fail: EVAL_FALSE]: 0 0 | notif if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"0 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #813 [Should Fail: OP_RETURN]: Multiple ELSEs", "[vm][auto]") { run_bchn_test({"1", "if return else else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_return, "Multiple ELSEs"}); } // flags: P2SH,STRICTENC, expected: OP_RETURN

TEST_CASE("VM-AUTO #814 [Should Fail: OP_RETURN]: 1 | if 1 else else return endif", "[vm][auto]") { run_bchn_test({"1", "if 1 else else return endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_return, ""}); } // flags: P2SH,STRICTENC, expected: OP_RETURN

TEST_CASE("VM-AUTO #815 [Should Fail: UNBALANCED_CONDITIONAL]: Malformed IF/ELSE/ENDIF sequence", "[vm][auto]") { run_bchn_test({"1", "endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, "Malformed IF/ELSE/ENDIF sequence"}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #816 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | else endif", "[vm][auto]") { run_bchn_test({"1", "else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #817 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | endif else", "[vm][auto]") { run_bchn_test({"1", "endif else", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #818 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | endif else if", "[vm][auto]") { run_bchn_test({"1", "endif else if", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #819 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | if else endif else", "[vm][auto]") { run_bchn_test({"1", "if else endif else", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #820 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | if else endif else endif", "[vm][auto]") { run_bchn_test({"1", "if else endif else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #821 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | if endif endif", "[vm][auto]") { run_bchn_test({"1", "if endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #822 [Should Fail: UNBALANCED_CONDITIONAL]: 1 | if else else endif endif", "[vm][auto]") { run_bchn_test({"1", "if else else endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

// KNUTH_OVERRIDE #18: error=KTH_INVALID_SCRIPT, reason=BCHN executes script and returns OP_RETURN error, but Knuth detects unspendable script early and returns invalid_script
TEST_CASE("VM-AUTO #823 [Should Fail: KTH_INVALID_SCRIPT]: 1 | return", "[vm][auto]") { run_bchn_test({"1", "return", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, " [KNUTH_OVERRIDE: KTH_INVALID_SCRIPT]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_SCRIPT

TEST_CASE("VM-AUTO #824 [Should Fail: OP_RETURN]: 1 | dup if return endif", "[vm][auto]") { run_bchn_test({"1", "dup if return endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_return, ""}); } // flags: P2SH,STRICTENC, expected: OP_RETURN

// KNUTH_OVERRIDE #19: error=KTH_INVALID_SCRIPT, reason=BCHN executes script and returns OP_RETURN error, but Knuth detects unspendable script early and returns invalid_script
TEST_CASE("VM-AUTO #825 [Should Fail: KTH_INVALID_SCRIPT]: canonical prunable txout format", "[vm][auto]") { run_bchn_test({"1", "return 'data'", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, "canonical prunable txout format [KNUTH_OVERRIDE: KTH_INVALID_SCRIPT]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_SCRIPT

// KNUTH_OVERRIDE #127: error=KTH_INVALID_STACK_SCOPE, reason=IF in sig, ENDIF in pub: run() returns invalid_stack_scope. BCHN returns UNBALANCED_CONDITIONAL.
TEST_CASE("VM-AUTO #826 [Should Fail: KTH_INVALID_STACK_SCOPE]: still prunable because IF/ENDIF can't span scriptSig/scriptPubKey", "[vm][auto]") { run_bchn_test({"0 if", "return endif 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_stack_scope, "still prunable because IF/ENDIF can't span scriptSig/scriptPubKey [KNUTH_OVERRIDE: KTH_INVALID_STACK_SCOPE]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_STACK_SCOPE

TEST_CASE("VM-AUTO #827 [Should Fail: VERIFY]: 0 | verify 1", "[vm][auto]") { run_bchn_test({"0", "verify 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: VERIFY

TEST_CASE("VM-AUTO #828 [Should Fail: EVAL_FALSE]: 1 | verify", "[vm][auto]") { run_bchn_test({"1", "verify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #829 [Should Fail: EVAL_FALSE]: 1 | verify 0", "[vm][auto]") { run_bchn_test({"1", "verify 0", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #830 [Should Fail: INVALID_ALTSTACK_OPERATION]: alt stack not shared between sig/pubkey", "[vm][auto]") { run_bchn_test({"1 toaltstack", "fromaltstack 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_alt_stack, "alt stack not shared between sig/pubkey"}); } // flags: P2SH,STRICTENC, expected: INVALID_ALTSTACK_OPERATION

TEST_CASE("VM-AUTO #831 [Should Fail: INVALID_STACK_OPERATION]: ifdup | depth 0 equal", "[vm][auto]") { run_bchn_test({"ifdup", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #832 [Should Fail: INVALID_STACK_OPERATION]: drop | depth 0 equal", "[vm][auto]") { run_bchn_test({"drop", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #833 [Should Fail: INVALID_STACK_OPERATION]: dup | depth 0 equal", "[vm][auto]") { run_bchn_test({"dup", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #834 [Should Fail: EVAL_FALSE]: 1 | dup 1 add 2 equalverify 0 equal", "[vm][auto]") { run_bchn_test({"1", "dup 1 add 2 equalverify 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #835 [Should Fail: INVALID_STACK_OPERATION]: nop | nip", "[vm][auto]") { run_bchn_test({"nop", "nip", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #836 [Should Fail: INVALID_STACK_OPERATION]: nop | 1 nip", "[vm][auto]") { run_bchn_test({"nop", "1 nip", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #837 [Should Fail: EVAL_FALSE]: nop | 1 0 nip", "[vm][auto]") { run_bchn_test({"nop", "1 0 nip", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #838 [Should Fail: INVALID_STACK_OPERATION]: nop | over 1", "[vm][auto]") { run_bchn_test({"nop", "over 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #839 [Should Fail: INVALID_STACK_OPERATION]: 1 | over", "[vm][auto]") { run_bchn_test({"1", "over", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #840 [Should Fail: EVAL_FALSE]: 0 1 | over depth 3 equalverify", "[vm][auto]") { run_bchn_test({"0 1", "over depth 3 equalverify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #841 [Should Fail: INVALID_STACK_OPERATION]: 19 20 21 | pick 19 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "pick 19 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #842 [Should Fail: INVALID_STACK_OPERATION]: nop | 0 pick", "[vm][auto]") { run_bchn_test({"nop", "0 pick", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #843 [Should Fail: INVALID_STACK_OPERATION]: 1 | -1 pick", "[vm][auto]") { run_bchn_test({"1", "-1 pick", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #844 [Should Fail: EQUALVERIFY]: 19 20 21 | 0 pick 20 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "0 pick 20 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #845 [Should Fail: EQUALVERIFY]: 19 20 21 | 1 pick 21 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "1 pick 21 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #846 [Should Fail: EQUALVERIFY]: 19 20 21 | 2 pick 22 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "2 pick 22 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #847 [Should Fail: INVALID_STACK_OPERATION]: nop | 0 roll", "[vm][auto]") { run_bchn_test({"nop", "0 roll", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #848 [Should Fail: INVALID_STACK_OPERATION]: 1 | -1 roll", "[vm][auto]") { run_bchn_test({"1", "-1 roll", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #849 [Should Fail: EQUALVERIFY]: 19 20 21 | 0 roll 20 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "0 roll 20 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #850 [Should Fail: EQUALVERIFY]: 19 20 21 | 1 roll 21 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "1 roll 21 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #851 [Should Fail: EQUALVERIFY]: 19 20 21 | 2 roll 22 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"19 20 21", "2 roll 22 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #852 [Should Fail: INVALID_STACK_OPERATION]: nop | rot 1", "[vm][auto]") { run_bchn_test({"nop", "rot 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #853 [Should Fail: INVALID_STACK_OPERATION]: nop | 1 rot 1", "[vm][auto]") { run_bchn_test({"nop", "1 rot 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #854 [Should Fail: INVALID_STACK_OPERATION]: nop | 1 2 rot 1", "[vm][auto]") { run_bchn_test({"nop", "1 2 rot 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #855 [Should Fail: EVAL_FALSE]: nop | 0 1 2 rot", "[vm][auto]") { run_bchn_test({"nop", "0 1 2 rot", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #856 [Should Fail: INVALID_STACK_OPERATION]: nop | swap 1", "[vm][auto]") { run_bchn_test({"nop", "swap 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #857 [Should Fail: INVALID_STACK_OPERATION]: 1 | swap 1", "[vm][auto]") { run_bchn_test({"1", "swap 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #858 [Should Fail: EQUALVERIFY]: 0 1 | swap 1 equalverify", "[vm][auto]") { run_bchn_test({"0 1", "swap 1 equalverify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::verify_failed, ""}); } // flags: P2SH,STRICTENC, expected: EQUALVERIFY

TEST_CASE("VM-AUTO #859 [Should Fail: INVALID_STACK_OPERATION]: nop | tuck 1", "[vm][auto]") { run_bchn_test({"nop", "tuck 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #860 [Should Fail: INVALID_STACK_OPERATION]: 1 | tuck 1", "[vm][auto]") { run_bchn_test({"1", "tuck 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #861 [Should Fail: EVAL_FALSE]: 1 0 | tuck depth 3 equalverify swap 2drop", "[vm][auto]") { run_bchn_test({"1 0", "tuck depth 3 equalverify swap 2drop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #862 [Should Fail: INVALID_STACK_OPERATION]: nop | 2dup 1", "[vm][auto]") { run_bchn_test({"nop", "2dup 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #863 [Should Fail: INVALID_STACK_OPERATION]: 1 | 2dup 1", "[vm][auto]") { run_bchn_test({"1", "2dup 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #864 [Should Fail: INVALID_STACK_OPERATION]: nop | 3dup 1", "[vm][auto]") { run_bchn_test({"nop", "3dup 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #865 [Should Fail: INVALID_STACK_OPERATION]: 1 | 3dup 1", "[vm][auto]") { run_bchn_test({"1", "3dup 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #866 [Should Fail: INVALID_STACK_OPERATION]: 1 2 | 3dup 1", "[vm][auto]") { run_bchn_test({"1 2", "3dup 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #867 [Should Fail: INVALID_STACK_OPERATION]: nop | 2over 1", "[vm][auto]") { run_bchn_test({"nop", "2over 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #868 [Should Fail: INVALID_STACK_OPERATION]: 1 | 2 3 2over 1", "[vm][auto]") { run_bchn_test({"1", "2 3 2over 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #869 [Should Fail: INVALID_STACK_OPERATION]: nop | 2swap 1", "[vm][auto]") { run_bchn_test({"nop", "2swap 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #870 [Should Fail: INVALID_STACK_OPERATION]: 1 | 2 3 2swap 1", "[vm][auto]") { run_bchn_test({"1", "2 3 2swap 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #871 [Should Fail: INVALID_STACK_OPERATION]: CAT, empty stack", "[vm][auto]") { run_bchn_test({"", "cat", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "CAT, empty stack"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #872 [Should Fail: INVALID_STACK_OPERATION]: CAT, one parameter", "[vm][auto]") { run_bchn_test({"'a'", "cat", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "CAT, one parameter"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #873 [Should Pass]: 'abcd' 'efgh' | cat 'abcdefgh' equal", "[vm][auto]") { run_bchn_test({"'abcd' 'efgh'", "cat 'abcdefgh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #874 [Should Pass]: CAT two empty strings", "[vm][auto]") { run_bchn_test({"'' ''", "cat '' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT two empty strings"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #875 [Should Pass]: CAT with empty string", "[vm][auto]") { run_bchn_test({"'abc' ''", "cat 'abc' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #876 [Should Pass]: CAT with empty string", "[vm][auto]") { run_bchn_test({"'' 'def'", "cat 'def' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #877 [Should Pass]: CAT, maximum length", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxh' 'ataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT, maximum length"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #878 [Should Pass]: CAT, maximum length with empty string", "[vm][auto]") { run_bchn_test({"'' 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT, maximum length with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #879 [Should Pass]: CAT, maximum length with empty string", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' ''", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "CAT, maximum length with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #880 [Should Fail: PUSH_SIZE]: CAT oversized result", "[vm][auto]") { run_bchn_test({"'a' 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_push_data_size, "CAT oversized result"}); } // flags: P2SH,STRICTENC, expected: PUSH_SIZE

TEST_CASE("VM-AUTO #881 [Should Fail: INVALID_STACK_OPERATION]: SPLIT, empty stack", "[vm][auto]") { run_bchn_test({"", "split", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "SPLIT, empty stack"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #882 [Should Fail: INVALID_STACK_OPERATION]: SPLIT, one parameter", "[vm][auto]") { run_bchn_test({"'a'", "split", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "SPLIT, one parameter"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #883 [Should Pass]: 'abcdef' 3 | split 'def' equalverify 'abc' equal", "[vm][auto]") { run_bchn_test({"'abcdef' 3", "split 'def' equalverify 'abc' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #884 [Should Pass]: SPLIT, empty string", "[vm][auto]") { run_bchn_test({"'' 0", "split '' equalverify '' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #885 [Should Pass]: SPLIT, boundary condition", "[vm][auto]") { run_bchn_test({"'abc' 0", "split 'abc' equalverify '' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, boundary condition"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #886 [Should Pass]: SPLIT, boundary condition", "[vm][auto]") { run_bchn_test({"'abc' 3", "split '' equalverify 'abc' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, boundary condition"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #887 [Should Fail: SPLIT_RANGE]: SPLIT, out of bounds", "[vm][auto]") { run_bchn_test({"'abc' 4", "split", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_split_range, "SPLIT, out of bounds"}); } // flags: P2SH,STRICTENC, expected: SPLIT_RANGE

TEST_CASE("VM-AUTO #888 [Should Fail: SPLIT_RANGE]: SPLIT, out of bounds", "[vm][auto]") { run_bchn_test({"'abc' -1", "split", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_split_range, "SPLIT, out of bounds"}); } // flags: P2SH,STRICTENC, expected: SPLIT_RANGE

TEST_CASE("VM-AUTO #889 [Should Pass]: SPLIT, maximum length", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "145 split 'ataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equalverify 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, maximum length"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #890 [Should Pass]: SPLIT, maximum length with empty string", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "0 split 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equalverify '' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, maximum length with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #891 [Should Pass]: SPLIT, maximum length with empty string", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "520 split '' equalverify 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "SPLIT, maximum length with empty string"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #892 [Should Fail: INVALID_STACK_OPERATION]: REVERSEBYTES, empty stack", "[vm][auto]") { run_bchn_test({"", "reversebytes 0 equal", kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "REVERSEBYTES, empty stack"}); } // flags: P2SH, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #893 [Should Pass]: REVERSEBYTES, empty data", "[vm][auto]") { run_bchn_test({"0", "reversebytes 0 equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, empty data"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #894 [Should Pass]: REVERSEBYTES, 1 byte", "[vm][auto]") { run_bchn_test({"[99]", "reversebytes [99] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 1 byte"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #895 [Should Pass]: REVERSEBYTES, 2 bytes", "[vm][auto]") { run_bchn_test({"[beef]", "reversebytes [efbe] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 2 bytes"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #896 [Should Pass]: REVERSEBYTES, 3 bytes", "[vm][auto]") { run_bchn_test({"[deada1]", "reversebytes [a1adde] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #897 [Should Pass]: REVERSEBYTES, 4 bytes", "[vm][auto]") { run_bchn_test({"[deadbeef]", "reversebytes [efbeadde] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 4 bytes"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #898 [Should Pass]: REVERSEBYTES, reversed pubkey", "[vm][auto]") { run_bchn_test({"[1052a02f76cf6c3bc43e4671af4c6065f6d918c79f58ad7f90a49c4e2603a4728fc6d81d49087dd97bc43ae184da55dc5195553526ce076df092be27a398b68f41]", "[a8f459fbe84e0568ff03454d438183b0464e0c18ff73658ec0d2619b20ff29e502] reversebytes checksig", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_sighash_forkid | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "REVERSEBYTES, reversed pubkey"}); } // flags: NULLFAIL,STRICTENC,SIGHASH_FORKID, expected: OK

TEST_CASE("VM-AUTO #899 [Should Pass]: REVERSEBYTES, whitepaper title", "[vm][auto]") { run_bchn_test({"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "reversebytes 'metsys_hsac_cinortcele_reep-ot-reep_A_:nioctiB' equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, whitepaper title"}); } // flags: P2SH, expected: OK
