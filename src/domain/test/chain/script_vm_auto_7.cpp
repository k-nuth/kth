// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 7 (tests 700 to 799)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #700 [Should Pass]: [0000] | abs drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "abs drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #701 [Should Pass]: [0000] | not drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "not drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #702 [Should Pass]: [0000] | 0notequal drop 1", "[vm][auto]") { run_bchn_test({"[0000]", "0notequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #703 [Should Pass]: 0 [0000] | add drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "add drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #704 [Should Pass]: [0000] 0 | add drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "add drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #705 [Should Pass]: 0 [0000] | sub drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "sub drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #706 [Should Pass]: [0000] 0 | sub drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "sub drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

// KNUTH_OVERRIDE #4: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
TEST_CASE("VM-AUTO #707 [Should Pass]: 0 [0100] | div drop 1", "[vm][auto]") { run_bchn_test({"0 [0100]", "div drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: NONE, expected: OK

// KNUTH_OVERRIDE #5: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
TEST_CASE("VM-AUTO #708 [Should Pass]: [0000] 1 | div drop 1", "[vm][auto]") { run_bchn_test({"[0000] 1", "div drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: NONE, expected: OK

// KNUTH_OVERRIDE #6: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
TEST_CASE("VM-AUTO #709 [Should Pass]: 0 [0100] | mod drop 1", "[vm][auto]") { run_bchn_test({"0 [0100]", "mod drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: NONE, expected: OK

// KNUTH_OVERRIDE #7: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
TEST_CASE("VM-AUTO #710 [Should Pass]: [0000] 1 | mod drop 1", "[vm][auto]") { run_bchn_test({"[0000] 1", "mod drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #711 [Should Pass]: 0 [0000] | booland drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "booland drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #712 [Should Pass]: [0000] 0 | booland drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "booland drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #713 [Should Pass]: 0 [0000] | boolor drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "boolor drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #714 [Should Pass]: [0000] 0 | boolor drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "boolor drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #715 [Should Pass]: 0 [0000] | numequal drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "numequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #716 [Should Pass]: [0000] 1 | numequal drop 1", "[vm][auto]") { run_bchn_test({"[0000] 1", "numequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #717 [Should Pass]: 0 [0000] | numequalverify 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "numequalverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #718 [Should Pass]: [0000] 0 | numequalverify 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "numequalverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #719 [Should Pass]: 0 [0000] | numnotequal drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "numnotequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #720 [Should Pass]: [0000] 0 | numnotequal drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "numnotequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #721 [Should Pass]: 0 [0000] | lessthan drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "lessthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #722 [Should Pass]: [0000] 0 | lessthan drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "lessthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #723 [Should Pass]: 0 [0000] | greaterthan drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "greaterthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #724 [Should Pass]: [0000] 0 | greaterthan drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "greaterthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #725 [Should Pass]: 0 [0000] | lessthanorequal drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "lessthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #726 [Should Pass]: [0000] 0 | lessthanorequal drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "lessthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #727 [Should Pass]: 0 [0000] | greaterthanorequal drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "greaterthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #728 [Should Pass]: [0000] 0 | greaterthanorequal drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "greaterthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #729 [Should Pass]: 0 [0000] | min drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "min drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #730 [Should Pass]: [0000] 0 | min drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "min drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #731 [Should Pass]: 0 [0000] | max drop 1", "[vm][auto]") { run_bchn_test({"0 [0000]", "max drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #732 [Should Pass]: [0000] 0 | max drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0", "max drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #733 [Should Pass]: [0000] 0 0 | within drop 1", "[vm][auto]") { run_bchn_test({"[0000] 0 0", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #734 [Should Pass]: 0 [0000] 0 | within drop 1", "[vm][auto]") { run_bchn_test({"0 [0000] 0", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #735 [Should Pass]: 0 0 [0000] | within drop 1", "[vm][auto]") { run_bchn_test({"0 0 [0000]", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #736 [Should Pass]: 0 0 [0000] | checkmultisig drop 1", "[vm][auto]") { run_bchn_test({"0 0 [0000]", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #737 [Should Pass]: 0 [0000] 0 | checkmultisig drop 1", "[vm][auto]") { run_bchn_test({"0 [0000] 0", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #738 [Should Pass]: 0 [0000] 0 1 | checkmultisig drop 1", "[vm][auto]") { run_bchn_test({"0 [0000] 0 1", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #739 [Should Pass]: 0 0 [0000] | checkmultisigverify 1", "[vm][auto]") { run_bchn_test({"0 0 [0000]", "checkmultisigverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #740 [Should Pass]: 0 [0000] 0 | checkmultisigverify 1", "[vm][auto]") { run_bchn_test({"0 [0000] 0", "checkmultisigverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #741 [Should Pass]: [0000] [0100] | split 2drop 1", "[vm][auto]") { run_bchn_test({"[0000] [0100]", "split 2drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #742 [Should Pass]: [11111111111111111111111111111111111111110000] [1400] | num2bin drop 1", "[vm][auto]") { run_bchn_test({"[11111111111111111111111111111111111111110000] [1400]", "num2bin drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #743 [Should Fail: UNSATISFIED_LOCKTIME]: fails because final sequence, not from nonminimal encoding", "[vm][auto]") { run_bchn_test({"[0000]", "checklocktimeverify drop 1", kth::domain::machine::script_flags::bip65_rule, kth::error::unsatisfied_locktime, "fails because final sequence, not from nonminimal encoding"}); } // flags: CHECKLOCKTIMEVERIFY, expected: UNSATISFIED_LOCKTIME

TEST_CASE("VM-AUTO #744 [Should Fail: UNSATISFIED_LOCKTIME]: fails because tx version < 2, not from nonminimal encoding", "[vm][auto]") { run_bchn_test({"[0000]", "checksequenceverify drop 1", kth::domain::machine::script_flags::bip112_rule, kth::error::unsatisfied_locktime, "fails because tx version < 2, not from nonminimal encoding"}); } // flags: CHECKSEQUENCEVERIFY, expected: UNSATISFIED_LOCKTIME

TEST_CASE("VM-AUTO #745 [Should Pass]: 0 | [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", "[vm][auto]") { run_bchn_test({"0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #746 [Should Pass]: 0 0 | 1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #747 [Should Fail: PUBKEYTYPE]: Even with a null signature, the public key has to be correctly encoded.", "[vm][auto]") { run_bchn_test({"0", "[BEEF] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, "Even with a null signature, the public key has to be correctly encoded."}); } // flags: STRICTENC, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #748 [Should Fail: PUBKEYTYPE]: 0 0 | 1 [BEEF] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""}); } // flags: STRICTENC, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #749 [Should Pass]: Invalid public keys are fine, as long as correctly encoded.", "[vm][auto]") { run_bchn_test({"0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "Invalid public keys are fine, as long as correctly encoded."}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #750 [Should Pass]: 0 0 | 1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #751 [Should Pass]: 0 | [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", "[vm][auto]") { run_bchn_test({"0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #752 [Should Pass]: 0 0 | 1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #753 [Should Fail: PUBKEYTYPE]: 0 | [BEEF] checksig not", "[vm][auto]") { run_bchn_test({"0", "[BEEF] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""}); } // flags: STRICTENC,NULLFAIL, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #754 [Should Fail: PUBKEYTYPE]: 0 0 | 1 [BEEF] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""}); } // flags: STRICTENC,NULLFAIL, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #755 [Should Pass]: 0 | [020000000000000000000000000000000000000000000000000000000000000000] checksig not", "[vm][auto]") { run_bchn_test({"0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #756 [Should Pass]: 0 0 | 1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", "[vm][auto]") { run_bchn_test({"0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""}); } // flags: STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #757 [Should Pass]: 2-of-2 CHECKMULTISIG NOT with the second pubkey invalid, and both signatures validly encoded. Valid pubkey fails, and CHECKMULTISIG exits early, pr...", "[vm][auto]") { run_bchn_test({"0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501] [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 0 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "2-of-2 CHECKMULTISIG NOT with the second pubkey invalid, and both signatures validly encoded. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid pubkey."}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #758 [Should Pass]: 2-of-2 CHECKMULTISIG NOT with both pubkeys valid, but second signature invalid. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluat...", "[vm][auto]") { run_bchn_test({"0 0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "2-of-2 CHECKMULTISIG NOT with both pubkeys valid, but second signature invalid. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid signature."}); } // flags: STRICTENC, expected: OK

TEST_CASE("VM-AUTO #759 [Should Pass]: Nulled 2-of-2 with valid pubkeys", "[vm][auto]") { run_bchn_test({"0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Nulled 2-of-2 with valid pubkeys"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #760 [Should Fail: PUBKEYTYPE]: Nulled 2-of-2 with top pubkey invalidly encoded", "[vm][auto]") { run_bchn_test({"0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [BEEF] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::pubkey_type, "Nulled 2-of-2 with top pubkey invalidly encoded"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE

TEST_CASE("VM-AUTO #761 [Should Pass]: Nulled 2-of-2 with top pubkey validly encoded, but another pubkey is invalidly encoded", "[vm][auto]") { run_bchn_test({"0 0 0", "2 [BEEF] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Nulled 2-of-2 with top pubkey validly encoded, but another pubkey is invalidly encoded"}); } // flags: P2SH,STRICTENC,NULLFAIL, expected: OK

TEST_CASE("VM-AUTO #762 [Should Pass]: Overly long signature is correctly encoded", "[vm][auto]") { run_bchn_test({"[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Overly long signature is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #763 [Should Pass]: Missing S is correctly encoded", "[vm][auto]") { run_bchn_test({"[30220220000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Missing S is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #764 [Should Pass]: S with invalid S length is correctly encoded", "[vm][auto]") { run_bchn_test({"[3024021077777777777777777777777777777777020a7777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "S with invalid S length is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #765 [Should Pass]: Non-integer R is correctly encoded", "[vm][auto]") { run_bchn_test({"[302403107777777777777777777777777777777702107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Non-integer R is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #766 [Should Pass]: Non-integer S is correctly encoded", "[vm][auto]") { run_bchn_test({"[302402107777777777777777777777777777777703107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Non-integer S is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #767 [Should Pass]: Zero-length R is correctly encoded", "[vm][auto]") { run_bchn_test({"[3014020002107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Zero-length R is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #768 [Should Pass]: Zero-length S is correctly encoded for DERSIG", "[vm][auto]") { run_bchn_test({"[3014021077777777777777777777777777777777020001]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Zero-length S is correctly encoded for DERSIG"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #769 [Should Pass]: Negative S is correctly encoded", "[vm][auto]") { run_bchn_test({"[302402107777777777777777777777777777777702108777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Negative S is correctly encoded"}); } // flags: NONE, expected: OK

TEST_CASE("VM-AUTO #770 [Should Pass]: CSV passes if stack top bit 1 << 31 is set", "[vm][auto]") { run_bchn_test({"2147483648", "checksequenceverify", kth::domain::machine::script_flags::bip112_rule, kth::error::success, "CSV passes if stack top bit 1 << 31 is set"}); } // flags: CHECKSEQUENCEVERIFY, expected: OK

TEST_CASE("VM-AUTO #771 [Should Fail: EVAL_FALSE]: Test the test: we should have an empty stack after scriptSig evaluation", "[vm][auto]") { run_bchn_test({"", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "Test the test: we should have an empty stack after scriptSig evaluation"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #772 [Should Fail: EVAL_FALSE]: and multiple spaces should not change that.", "[vm][auto]") { run_bchn_test({"", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "and multiple spaces should not change that."}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #773 [Should Fail: EVAL_FALSE]:  | depth", "[vm][auto]") { run_bchn_test({"", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #774 [Should Fail: EVAL_FALSE]:  | depth", "[vm][auto]") { run_bchn_test({"", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #775 [Should Fail: EVAL_FALSE]:  | ", "[vm][auto]") { run_bchn_test({"", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #776 [Should Fail: EVAL_FALSE]:  | nop", "[vm][auto]") { run_bchn_test({"", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #777 [Should Fail: EVAL_FALSE]:  | nop depth", "[vm][auto]") { run_bchn_test({"", "nop depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #778 [Should Fail: EVAL_FALSE]: nop | ", "[vm][auto]") { run_bchn_test({"nop", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #779 [Should Fail: EVAL_FALSE]: nop | depth", "[vm][auto]") { run_bchn_test({"nop", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #780 [Should Fail: EVAL_FALSE]: nop | nop", "[vm][auto]") { run_bchn_test({"nop", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #781 [Should Fail: EVAL_FALSE]: nop | nop depth", "[vm][auto]") { run_bchn_test({"nop", "nop depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #782 [Should Fail: EVAL_FALSE]: depth | ", "[vm][auto]") { run_bchn_test({"depth", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

// KNUTH_OVERRIDE #148: error=KTH_INVALID_SCRIPT, sig=RAW:4c01, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
TEST_CASE("VM-AUTO #783 [Should Fail: KTH_INVALID_SCRIPT]: PUSHDATA1 with not enough bytes", "[vm][auto]") { run_bchn_test({"raw:4c01", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, "PUSHDATA1 with not enough bytes [KNUTH_OVERRIDE: ERROR: KTH_INVALID_SCRIPT, SIG_REPLACED, PUBKEY_REPLACED]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_SCRIPT

// KNUTH_OVERRIDE #149: sig=RAW:4d0200ff, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
TEST_CASE("VM-AUTO #784 [Should Fail: BAD_OPCODE]: PUSHDATA2 with not enough bytes", "[vm][auto]") { run_bchn_test({"raw:4d0200ff", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "PUSHDATA2 with not enough bytes [KNUTH_OVERRIDE: SIG_REPLACED, PUBKEY_REPLACED]"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

// KNUTH_OVERRIDE #150: sig=RAW:4e03000000ffff, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
TEST_CASE("VM-AUTO #785 [Should Fail: BAD_OPCODE]: PUSHDATA4 with not enough bytes", "[vm][auto]") { run_bchn_test({"raw:4e03000000ffff", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "PUSHDATA4 with not enough bytes [KNUTH_OVERRIDE: SIG_REPLACED, PUBKEY_REPLACED]"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #786 [Should Fail: BAD_OPCODE]: 0x50 is reserved", "[vm][auto]") { run_bchn_test({"1", "if 0x50 endif 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "0x50 is reserved"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #787 [Should Fail: EVAL_FALSE]: 0x51 through 0x60 push 1 through 16 onto stack", "[vm][auto]") { run_bchn_test({"0x52", "0x5f add 0x60 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "0x51 through 0x60 push 1 through 16 onto stack"}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #788 [Should Fail: EVAL_FALSE]: 0 | nop", "[vm][auto]") { run_bchn_test({"0", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""}); } // flags: P2SH,STRICTENC, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #789 [Should Fail: BAD_OPCODE]: VER non-functional", "[vm][auto]") { run_bchn_test({"1", "if ver else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "VER non-functional"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #790 [Should Fail: BAD_OPCODE]: INVOKE not enabled", "[vm][auto]") { run_bchn_test({"1", "if invoke else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "INVOKE not enabled"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #791 [Should Fail: BAD_OPCODE]: DEFINE not enabled", "[vm][auto]") { run_bchn_test({"1", "if define else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "DEFINE not enabled"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #792 [Should Fail: BAD_OPCODE]: BRGIN before activation illegal everywhere", "[vm][auto]") { run_bchn_test({"0", "if begin else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "BRGIN before activation illegal everywhere"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #793 [Should Fail: BAD_OPCODE]: BEGIN before activation illegal everywhere", "[vm][auto]") { run_bchn_test({"0", "if else 1 else begin endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "BEGIN before activation illegal everywhere"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #794 [Should Fail: BAD_OPCODE]: UNTIL before activation illegal everywhere", "[vm][auto]") { run_bchn_test({"0", "if until else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "UNTIL before activation illegal everywhere"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

TEST_CASE("VM-AUTO #795 [Should Fail: BAD_OPCODE]: UNTIL before activation illegal everywhere", "[vm][auto]") { run_bchn_test({"0", "if else 1 else until endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "UNTIL before activation illegal everywhere"}); } // flags: P2SH,STRICTENC, expected: BAD_OPCODE

// KNUTH_OVERRIDE #125: error=KTH_INVALID_STACK_SCOPE, reason=IF in sig, ENDIF in pub: run() returns invalid_stack_scope at end because IF is not closed within same script. BCHN returns UNBALANCED_CONDITIONAL.
TEST_CASE("VM-AUTO #796 [Should Fail: KTH_INVALID_STACK_SCOPE]: IF/ENDIF can't span scriptSig/scriptPubKey", "[vm][auto]") { run_bchn_test({"1 if", "1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_stack_scope, "IF/ENDIF can't span scriptSig/scriptPubKey [KNUTH_OVERRIDE: KTH_INVALID_STACK_SCOPE]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_STACK_SCOPE

TEST_CASE("VM-AUTO #797 [Should Fail: UNBALANCED_CONDITIONAL]: 1 if 0 endif | 1 endif", "[vm][auto]") { run_bchn_test({"1 if 0 endif", "1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

TEST_CASE("VM-AUTO #798 [Should Fail: UNBALANCED_CONDITIONAL]: 1 else 0 endif | 1", "[vm][auto]") { run_bchn_test({"1 else 0 endif", "1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""}); } // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL

// KNUTH_OVERRIDE #126: error=KTH_INVALID_STACK_SCOPE, reason=NOTIF in sig without ENDIF: run() returns invalid_stack_scope at end. BCHN returns UNBALANCED_CONDITIONAL.
TEST_CASE("VM-AUTO #799 [Should Fail: KTH_INVALID_STACK_SCOPE]: 0 notif | 123", "[vm][auto]") { run_bchn_test({"0 notif", "123", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_stack_scope, " [KNUTH_OVERRIDE: KTH_INVALID_STACK_SCOPE]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_STACK_SCOPE
