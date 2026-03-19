// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 0 (tests 0 to 99)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #0 [Should Pass]: Test the test: we should have an empty stack after scriptSig evaluation", "[vm][auto]") { run_bchn_test({"", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Test the test: we should have an empty stack after scriptSig evaluation"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #1 [Should Pass]: and multiple spaces should not change that.", "[vm][auto]") { run_bchn_test({"", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "and multiple spaces should not change that."}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #2 [Should Pass]:  | depth 0 equal", "[vm][auto]") { run_bchn_test({"", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #3 [Should Pass]:  | depth 0 equal", "[vm][auto]") { run_bchn_test({"", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #4 [Should Pass]: Similarly whitespace around and between symbols", "[vm][auto]") { run_bchn_test({"1 2", "2 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Similarly whitespace around and between symbols"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #5 [Should Pass]: 1 2 | 2 equalverify 1 equal", "[vm][auto]") { run_bchn_test({"1 2", "2 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #6 [Should Pass]: 1 2 | 2 equalverify 1 equal", "[vm][auto]") { run_bchn_test({"1 2", "2 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #7 [Should Pass]: 1 2 | 2 equalverify 1 equal", "[vm][auto]") { run_bchn_test({"1 2", "2 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #8 [Should Pass]: 1 2 | 2 equalverify 1 equal", "[vm][auto]") { run_bchn_test({"1 2", "2 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #9 [Should Pass]: 1 | ", "[vm][auto]") { run_bchn_test({"1", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #10 [Should Pass]: all bytes are significant, not only the last one", "[vm][auto]") { run_bchn_test({"[0100]", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "all bytes are significant, not only the last one"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #11 [Should Pass]: equals zero when cast to Int64", "[vm][auto]") { run_bchn_test({"[000000000000000010]", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "equals zero when cast to Int64"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #12 [Should Pass]: push 1 byte", "[vm][auto]") { run_bchn_test({"[0b]", "11 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "push 1 byte"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #13 [Should Pass]: [417a] | 'Az' equal", "[vm][auto]") { run_bchn_test({"[417a]", "'Az' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #14 [Should Pass]: push 75 bytes", "[vm][auto]") { run_bchn_test({"[417a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a]", "'Azzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz' equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "push 75 bytes"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #15 [Should Pass]: [1.07] | 7 equal", "[vm][auto]") { run_bchn_test({"[1.07]", "7 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #16 [Should Pass]: [2.08] | 8 equal", "[vm][auto]") { run_bchn_test({"[2.08]", "8 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #17 [Should Pass]: [4.09] | 9 equal", "[vm][auto]") { run_bchn_test({"[4.09]", "9 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #18 [Should Pass]: pushdata1 0x00 | 0 equal", "[vm][auto]") { run_bchn_test({"pushdata1 0x00", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #19 [Should Pass]: pushdata2 0 0 | 0 equal", "[vm][auto]") { run_bchn_test({"pushdata2 0 0", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #20 [Should Pass]: pushdata4 0 0 0 0 | 0 equal", "[vm][auto]") { run_bchn_test({"pushdata4 0 0 0 0", "0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #21 [Should Pass]: 0x4f 1000 add | 999 equal", "[vm][auto]") { run_bchn_test({"0x4f 1000 add", "999 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #22 [Should Pass]: 0x50 is reserved (ok if not executed)", "[vm][auto]") { run_bchn_test({"0", "if 0x50 endif 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "0x50 is reserved (ok if not executed)"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #23 [Should Pass]: 0x51 through 0x60 push 1 through 16 onto stack", "[vm][auto]") { run_bchn_test({"0x51", "0x5f add 0x60 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "0x51 through 0x60 push 1 through 16 onto stack"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #24 [Should Pass]: 1 | nop", "[vm][auto]") { run_bchn_test({"1", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #25 [Should Pass]: VER non-functional (ok if not executed)", "[vm][auto]") { run_bchn_test({"0", "if ver else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "VER non-functional (ok if not executed)"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #26 [Should Pass]: OP_INVOKE with empty stack (ok if not executed)", "[vm][auto]") { run_bchn_test({"0", "if invoke else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "OP_INVOKE with empty stack (ok if not executed)"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #27 [Should Pass]: OP_DEFINE with empty stack (ok if not executed)", "[vm][auto]") { run_bchn_test({"0", "if define else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "OP_DEFINE with empty stack (ok if not executed)"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #28 [Should Pass]: RESERVED ok in un-executed IF", "[vm][auto]") { run_bchn_test({"0", "if reserved reserved3 reserved4 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "RESERVED ok in un-executed IF"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #29 [Should Pass]: 1 | dup if endif", "[vm][auto]") { run_bchn_test({"1", "dup if endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #30 [Should Pass]: 1 | if 1 endif", "[vm][auto]") { run_bchn_test({"1", "if 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #31 [Should Pass]: 1 | dup if else endif", "[vm][auto]") { run_bchn_test({"1", "dup if else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #32 [Should Pass]: 1 | if 1 else endif", "[vm][auto]") { run_bchn_test({"1", "if 1 else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #33 [Should Pass]: 0 | if else 1 endif", "[vm][auto]") { run_bchn_test({"0", "if else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #34 [Should Pass]: 1 1 | if if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"1 1", "if if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #35 [Should Pass]: 1 0 | if if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"1 0", "if if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #36 [Should Pass]: 1 1 | if if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"1 1", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #37 [Should Pass]: 0 0 | if if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"0 0", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #38 [Should Pass]: 1 0 | notif if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"1 0", "notif if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #39 [Should Pass]: 1 1 | notif if 1 else 0 endif endif", "[vm][auto]") { run_bchn_test({"1 1", "notif if 1 else 0 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #40 [Should Pass]: 1 0 | notif if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"1 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #41 [Should Pass]: 0 1 | notif if 1 else 0 endif else if 0 else 1 endif endif", "[vm][auto]") { run_bchn_test({"0 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #42 [Should Pass]: Multiple ELSE's are valid and executed inverts on each ELSE encountered", "[vm][auto]") { run_bchn_test({"0", "if 0 else 1 else 0 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Multiple ELSE's are valid and executed inverts on each ELSE encountered"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #43 [Should Pass]: 1 | if 1 else 0 else endif", "[vm][auto]") { run_bchn_test({"1", "if 1 else 0 else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #44 [Should Pass]: 1 | if else 0 else 1 endif", "[vm][auto]") { run_bchn_test({"1", "if else 0 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #45 [Should Pass]: 1 | if 1 else 0 else 1 endif add 2 equal", "[vm][auto]") { run_bchn_test({"1", "if 1 else 0 else 1 endif add 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #46 [Should Pass]: '' 1 | if sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sh...", "[vm][auto]") { run_bchn_test({"'' 1", "if sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #47 [Should Pass]: Multiple ELSE's are valid and execution inverts on each ELSE encountered", "[vm][auto]") { run_bchn_test({"1", "notif 0 else 1 else 0 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Multiple ELSE's are valid and execution inverts on each ELSE encountered"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #48 [Should Pass]: 0 | notif 1 else 0 else endif", "[vm][auto]") { run_bchn_test({"0", "notif 1 else 0 else endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #49 [Should Pass]: 0 | notif else 0 else 1 endif", "[vm][auto]") { run_bchn_test({"0", "notif else 0 else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #50 [Should Pass]: 0 | notif 1 else 0 else 1 endif add 2 equal", "[vm][auto]") { run_bchn_test({"0", "notif 1 else 0 else 1 endif add 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #51 [Should Pass]: '' 0 | notif sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else...", "[vm][auto]") { run_bchn_test({"'' 0", "notif sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #52 [Should Pass]: Nested ELSE ELSE", "[vm][auto]") { run_bchn_test({"0", "if 1 if return else return else return endif else 1 if 1 else return else 1 endif else return endif add 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Nested ELSE ELSE"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #53 [Should Pass]: 1 | notif 0 notif return else return else return endif else 0 notif 1 else return else 1 endif else return endif add 2 equal", "[vm][auto]") { run_bchn_test({"1", "notif 0 notif return else return else return endif else 0 notif 1 else return else 1 endif else return endif add 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #54 [Should Pass]: RETURN only works if executed", "[vm][auto]") { run_bchn_test({"0", "if return endif 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "RETURN only works if executed"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #55 [Should Pass]: 1 1 | verify", "[vm][auto]") { run_bchn_test({"1 1", "verify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #56 [Should Pass]: values >4 bytes can be cast to boolean", "[vm][auto]") { run_bchn_test({"1 [0100000000]", "verify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "values >4 bytes can be cast to boolean"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #57 [Should Pass]: negative 0 is false", "[vm][auto]") { run_bchn_test({"1 [80]", "if 0 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "negative 0 is false"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #58 [Should Pass]: 10 0 11 toaltstack drop fromaltstack | add 21 equal", "[vm][auto]") { run_bchn_test({"10 0 11 toaltstack drop fromaltstack", "add 21 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #59 [Should Pass]: 'gavin_was_here' toaltstack 11 fromaltstack | 'gavin_was_here' equalverify 11 equal", "[vm][auto]") { run_bchn_test({"'gavin_was_here' toaltstack 11 fromaltstack", "'gavin_was_here' equalverify 11 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #60 [Should Pass]: 0 ifdup | depth 1 equalverify 0 equal", "[vm][auto]") { run_bchn_test({"0 ifdup", "depth 1 equalverify 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #61 [Should Pass]: 1 ifdup | depth 2 equalverify 1 equalverify 1 equal", "[vm][auto]") { run_bchn_test({"1 ifdup", "depth 2 equalverify 1 equalverify 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #62 [Should Pass]: IFDUP dups non ints", "[vm][auto]") { run_bchn_test({"[0100000000] ifdup", "depth 2 equalverify [0100000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "IFDUP dups non ints"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #63 [Should Pass]: 0 drop | depth 0 equal", "[vm][auto]") { run_bchn_test({"0 drop", "depth 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #64 [Should Pass]: 0 | dup 1 add 1 equalverify 0 equal", "[vm][auto]") { run_bchn_test({"0", "dup 1 add 1 equalverify 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #65 [Should Pass]: 0 1 | nip", "[vm][auto]") { run_bchn_test({"0 1", "nip", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #66 [Should Pass]: 1 0 | over depth 3 equalverify", "[vm][auto]") { run_bchn_test({"1 0", "over depth 3 equalverify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #67 [Should Pass]: 22 21 20 | 0 pick 20 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "0 pick 20 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #68 [Should Pass]: 22 21 20 | 1 pick 21 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "1 pick 21 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #69 [Should Pass]: 22 21 20 | 2 pick 22 equalverify depth 3 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "2 pick 22 equalverify depth 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #70 [Should Pass]: 22 21 20 | 0 roll 20 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "0 roll 20 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #71 [Should Pass]: 22 21 20 | 1 roll 21 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "1 roll 21 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #72 [Should Pass]: 22 21 20 | 2 roll 22 equalverify depth 2 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "2 roll 22 equalverify depth 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #73 [Should Pass]: 22 21 20 | rot 22 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "rot 22 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #74 [Should Pass]: 22 21 20 | rot drop 20 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "rot drop 20 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #75 [Should Pass]: 22 21 20 | rot drop drop 21 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "rot drop drop 21 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #76 [Should Pass]: 22 21 20 | rot rot 21 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "rot rot 21 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #77 [Should Pass]: 22 21 20 | rot rot rot 20 equal", "[vm][auto]") { run_bchn_test({"22 21 20", "rot rot rot 20 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #78 [Should Pass]: 25 24 23 22 21 20 | 2rot 24 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 24 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #79 [Should Pass]: 25 24 23 22 21 20 | 2rot drop 25 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot drop 25 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #80 [Should Pass]: 25 24 23 22 21 20 | 2rot 2drop 20 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2drop 20 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #81 [Should Pass]: 25 24 23 22 21 20 | 2rot 2drop drop 21 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2drop drop 21 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #82 [Should Pass]: 25 24 23 22 21 20 | 2rot 2drop 2drop 22 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2drop 2drop 22 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #83 [Should Pass]: 25 24 23 22 21 20 | 2rot 2drop 2drop drop 23 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2drop 2drop drop 23 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #84 [Should Pass]: 25 24 23 22 21 20 | 2rot 2rot 22 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2rot 22 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #85 [Should Pass]: 25 24 23 22 21 20 | 2rot 2rot 2rot 20 equal", "[vm][auto]") { run_bchn_test({"25 24 23 22 21 20", "2rot 2rot 2rot 20 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #86 [Should Pass]: 1 0 | swap 1 equalverify 0 equal", "[vm][auto]") { run_bchn_test({"1 0", "swap 1 equalverify 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #87 [Should Pass]: 0 1 | tuck depth 3 equalverify swap 2drop", "[vm][auto]") { run_bchn_test({"0 1", "tuck depth 3 equalverify swap 2drop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #88 [Should Pass]: 13 14 | 2dup rot equalverify equal", "[vm][auto]") { run_bchn_test({"13 14", "2dup rot equalverify equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #89 [Should Pass]: -1 0 1 2 | 3dup depth 7 equalverify add add 3 equalverify 2drop 0 equalverify", "[vm][auto]") { run_bchn_test({"-1 0 1 2", "3dup depth 7 equalverify add add 3 equalverify 2drop 0 equalverify", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #90 [Should Pass]: 1 2 3 5 | 2over add add 8 equalverify add add 6 equal", "[vm][auto]") { run_bchn_test({"1 2 3 5", "2over add add 8 equalverify add add 6 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #91 [Should Pass]: 1 3 5 7 | 2swap add 4 equalverify add 12 equal", "[vm][auto]") { run_bchn_test({"1 3 5 7", "2swap add 4 equalverify add 12 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #92 [Should Pass]: 0 | size 0 equal", "[vm][auto]") { run_bchn_test({"0", "size 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #93 [Should Pass]: 1 | size 1 equal", "[vm][auto]") { run_bchn_test({"1", "size 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #94 [Should Pass]: 127 | size 1 equal", "[vm][auto]") { run_bchn_test({"127", "size 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #95 [Should Pass]: 128 | size 2 equal", "[vm][auto]") { run_bchn_test({"128", "size 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #96 [Should Pass]: 32767 | size 2 equal", "[vm][auto]") { run_bchn_test({"32767", "size 2 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #97 [Should Pass]: 32768 | size 3 equal", "[vm][auto]") { run_bchn_test({"32768", "size 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #98 [Should Pass]: 8388607 | size 3 equal", "[vm][auto]") { run_bchn_test({"8388607", "size 3 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #99 [Should Pass]: 8388608 | size 4 equal", "[vm][auto]") { run_bchn_test({"8388608", "size 4 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK
