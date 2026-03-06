// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 6 (tests 600 to 699)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #600 [Should Pass]: OP_9  pushes 0x09", "[vm][auto]") { run_bchn_test({"[09]", "0x59 equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_9  pushes 0x09"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #601 [Should Pass]: OP_10 pushes 0x0a", "[vm][auto]") { run_bchn_test({"[0a]", "0x5a equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_10 pushes 0x0a"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #602 [Should Pass]: OP_11 pushes 0x0b", "[vm][auto]") { run_bchn_test({"[0b]", "0x5b equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_11 pushes 0x0b"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #603 [Should Pass]: OP_12 pushes 0x0c", "[vm][auto]") { run_bchn_test({"[0c]", "0x5c equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_12 pushes 0x0c"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #604 [Should Pass]: OP_13 pushes 0x0d", "[vm][auto]") { run_bchn_test({"[0d]", "0x5d equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_13 pushes 0x0d"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #605 [Should Pass]: OP_14 pushes 0x0e", "[vm][auto]") { run_bchn_test({"[0e]", "0x5e equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_14 pushes 0x0e"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #606 [Should Pass]: OP_15 pushes 0x0f", "[vm][auto]") { run_bchn_test({"[0f]", "0x5f equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_15 pushes 0x0f"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #607 [Should Pass]: OP_16 pushes 0x10", "[vm][auto]") { run_bchn_test({"[10]", "0x60 equal", kth::domain::machine::script_flags::no_rules, kth::error::success, "OP_16 pushes 0x10"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #608 [Should Pass]: 0x8000 equals 128", "[vm][auto]") { run_bchn_test({"[8000]", "128 numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, "0x8000 equals 128"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #609 [Should Pass]: 0x00 numequals 0", "[vm][auto]") { run_bchn_test({"[00]", "0 numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, "0x00 numequals 0"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #610 [Should Pass]: 0x80 (negative zero) numequals 0", "[vm][auto]") { run_bchn_test({"[80]", "0 numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, "0x80 (negative zero) numequals 0"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #611 [Should Pass]: 0x0080 numequals 0", "[vm][auto]") { run_bchn_test({"[0080]", "0 numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, "0x0080 numequals 0"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #612 [Should Pass]: 0x0500 numequals 5", "[vm][auto]") { run_bchn_test({"[0500]", "5 numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, "0x0500 numequals 5"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #613 [Should Pass]: [ff7f80] | [ffff] numequal", "[vm][auto]") { run_bchn_test({"[ff7f80]", "[ffff] numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #614 [Should Pass]: [ff7f00] | [ff7f] numequal", "[vm][auto]") { run_bchn_test({"[ff7f00]", "[ff7f] numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #615 [Should Pass]: [ffff7f80] | [ffffff] numequal", "[vm][auto]") { run_bchn_test({"[ffff7f80]", "[ffffff] numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #616 [Should Pass]: [ffff7f00] | [ffff7f] numequal", "[vm][auto]") { run_bchn_test({"[ffff7f00]", "[ffff7f] numequal", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #617 [Should Pass]: non-minimal PUSHDATA1 ignored", "[vm][auto]") { run_bchn_test({"0 if pushdata1 0x00 endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA1 ignored"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #618 [Should Pass]: non-minimal PUSHDATA1 ignored (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if pushdata1 0x00 endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA1 ignored (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #619 [Should Pass]: non-minimal PUSHDATA2 ignored", "[vm][auto]") { run_bchn_test({"0 if pushdata2 0 0 endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA2 ignored"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #620 [Should Pass]: non-minimal PUSHDATA2 ignored (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if pushdata2 0 0 endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA2 ignored (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #621 [Should Pass]: non-minimal PUSHDATA4 ignored", "[vm][auto]") { run_bchn_test({"0 if pushdata4 0 0 0 0 endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA4 ignored"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #622 [Should Pass]: non-minimal PUSHDATA4 ignored (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if pushdata4 0 0 0 0 endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "non-minimal PUSHDATA4 ignored (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #623 [Should Pass]: 1NEGATE equiv", "[vm][auto]") { run_bchn_test({"0 if [81] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "1NEGATE equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #624 [Should Pass]: 1NEGATE equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [81] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "1NEGATE equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #625 [Should Pass]: OP_1  equiv", "[vm][auto]") { run_bchn_test({"0 if [01] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_1  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #626 [Should Pass]: OP_1  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [01] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_1  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #627 [Should Pass]: OP_2  equiv", "[vm][auto]") { run_bchn_test({"0 if [02] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_2  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #628 [Should Pass]: OP_2  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [02] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_2  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #629 [Should Pass]: OP_3  equiv", "[vm][auto]") { run_bchn_test({"0 if [03] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_3  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #630 [Should Pass]: OP_3  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [03] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_3  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #631 [Should Pass]: OP_4  equiv", "[vm][auto]") { run_bchn_test({"0 if [04] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_4  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #632 [Should Pass]: OP_4  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [04] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_4  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #633 [Should Pass]: OP_5  equiv", "[vm][auto]") { run_bchn_test({"0 if [05] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_5  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #634 [Should Pass]: OP_5  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [05] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_5  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #635 [Should Pass]: OP_6  equiv", "[vm][auto]") { run_bchn_test({"0 if [06] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_6  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #636 [Should Pass]: OP_6  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [06] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_6  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #637 [Should Pass]: OP_7  equiv", "[vm][auto]") { run_bchn_test({"0 if [07] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_7  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #638 [Should Pass]: OP_7  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [07] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_7  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #639 [Should Pass]: OP_8  equiv", "[vm][auto]") { run_bchn_test({"0 if [08] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_8  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #640 [Should Pass]: OP_8  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [08] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_8  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #641 [Should Pass]: OP_9  equiv", "[vm][auto]") { run_bchn_test({"0 if [09] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_9  equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #642 [Should Pass]: OP_9  equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [09] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_9  equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #643 [Should Pass]: OP_10 equiv", "[vm][auto]") { run_bchn_test({"0 if [0a] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_10 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #644 [Should Pass]: OP_10 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0a] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_10 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #645 [Should Pass]: OP_11 equiv", "[vm][auto]") { run_bchn_test({"0 if [0b] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_11 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #646 [Should Pass]: OP_11 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0b] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_11 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #647 [Should Pass]: OP_12 equiv", "[vm][auto]") { run_bchn_test({"0 if [0c] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_12 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #648 [Should Pass]: OP_12 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0c] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_12 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #649 [Should Pass]: OP_13 equiv", "[vm][auto]") { run_bchn_test({"0 if [0d] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_13 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #650 [Should Pass]: OP_13 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0d] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_13 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #651 [Should Pass]: OP_14 equiv", "[vm][auto]") { run_bchn_test({"0 if [0e] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_14 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #652 [Should Pass]: OP_14 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0e] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_14 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #653 [Should Pass]: OP_15 equiv", "[vm][auto]") { run_bchn_test({"0 if [0f] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_15 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #654 [Should Pass]: OP_15 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [0f] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_15 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #655 [Should Pass]: OP_16 equiv", "[vm][auto]") { run_bchn_test({"0 if [10] endif 1", "", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_16 equiv"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #656 [Should Pass]: OP_16 equiv (scriptPubKey)", "[vm][auto]") { run_bchn_test({"", "0 if [10] endif 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, "OP_16 equiv (scriptPubKey)"}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #657 [Should Pass]: [00] | 1", "[vm][auto]") { run_bchn_test({"[00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #658 [Should Pass]:  | [00] 1", "[vm][auto]") { run_bchn_test({"", "[00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #659 [Should Pass]: [80] | 1", "[vm][auto]") { run_bchn_test({"[80]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #660 [Should Pass]:  | [80] 1", "[vm][auto]") { run_bchn_test({"", "[80] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #661 [Should Pass]: [0180] | 1", "[vm][auto]") { run_bchn_test({"[0180]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #662 [Should Pass]:  | [0180] 1", "[vm][auto]") { run_bchn_test({"", "[0180] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #663 [Should Pass]: [0100] | 1", "[vm][auto]") { run_bchn_test({"[0100]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #664 [Should Pass]:  | [0100] 1", "[vm][auto]") { run_bchn_test({"", "[0100] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #665 [Should Pass]: [0200] | 1", "[vm][auto]") { run_bchn_test({"[0200]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #666 [Should Pass]:  | [0200] 1", "[vm][auto]") { run_bchn_test({"", "[0200] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #667 [Should Pass]: [0300] | 1", "[vm][auto]") { run_bchn_test({"[0300]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #668 [Should Pass]:  | [0300] 1", "[vm][auto]") { run_bchn_test({"", "[0300] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #669 [Should Pass]: [0400] | 1", "[vm][auto]") { run_bchn_test({"[0400]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #670 [Should Pass]:  | [0400] 1", "[vm][auto]") { run_bchn_test({"", "[0400] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #671 [Should Pass]: [0500] | 1", "[vm][auto]") { run_bchn_test({"[0500]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #672 [Should Pass]:  | [0500] 1", "[vm][auto]") { run_bchn_test({"", "[0500] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #673 [Should Pass]: [0600] | 1", "[vm][auto]") { run_bchn_test({"[0600]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #674 [Should Pass]:  | [0600] 1", "[vm][auto]") { run_bchn_test({"", "[0600] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #675 [Should Pass]: [0700] | 1", "[vm][auto]") { run_bchn_test({"[0700]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #676 [Should Pass]:  | [0700] 1", "[vm][auto]") { run_bchn_test({"", "[0700] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #677 [Should Pass]: [0800] | 1", "[vm][auto]") { run_bchn_test({"[0800]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #678 [Should Pass]:  | [0800] 1", "[vm][auto]") { run_bchn_test({"", "[0800] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #679 [Should Pass]: [0900] | 1", "[vm][auto]") { run_bchn_test({"[0900]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #680 [Should Pass]:  | [0900] 1", "[vm][auto]") { run_bchn_test({"", "[0900] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #681 [Should Pass]: [0a00] | 1", "[vm][auto]") { run_bchn_test({"[0a00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #682 [Should Pass]:  | [0a00] 1", "[vm][auto]") { run_bchn_test({"", "[0a00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #683 [Should Pass]: [0b00] | 1", "[vm][auto]") { run_bchn_test({"[0b00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #684 [Should Pass]:  | [0b00] 1", "[vm][auto]") { run_bchn_test({"", "[0b00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #685 [Should Pass]: [0c00] | 1", "[vm][auto]") { run_bchn_test({"[0c00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #686 [Should Pass]:  | [0c00] 1", "[vm][auto]") { run_bchn_test({"", "[0c00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #687 [Should Pass]: [0d00] | 1", "[vm][auto]") { run_bchn_test({"[0d00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #688 [Should Pass]:  | [0d00] 1", "[vm][auto]") { run_bchn_test({"", "[0d00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #689 [Should Pass]: [0e00] | 1", "[vm][auto]") { run_bchn_test({"[0e00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #690 [Should Pass]:  | [0e00] 1", "[vm][auto]") { run_bchn_test({"", "[0e00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #691 [Should Pass]: [0f00] | 1", "[vm][auto]") { run_bchn_test({"[0f00]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #692 [Should Pass]:  | [0f00] 1", "[vm][auto]") { run_bchn_test({"", "[0f00] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #693 [Should Pass]: [1000] | 1", "[vm][auto]") { run_bchn_test({"[1000]", "1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #694 [Should Pass]:  | [1000] 1", "[vm][auto]") { run_bchn_test({"", "[1000] 1", kth::domain::machine::script_flags::bch_minimaldata, kth::error::success, ""}); } // flags: MINIMALDATA, expected: OK

TEST_CASE("VM-AUTO #695 [Should Pass]: 1 [0000] | pick drop", "[vm][auto]") { run_bchn_test({"1 [0000]", "pick drop", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #696 [Should Pass]: 1 [0000] | roll drop 1", "[vm][auto]") { run_bchn_test({"1 [0000]", "roll drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #697 [Should Pass]: [0000] | add1 drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "add1 drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #698 [Should Pass]: [0000] | sub1 drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "sub1 drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #699 [Should Pass]: [0000] | negate drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "negate drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK
