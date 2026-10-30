// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "bchn_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_bchn_bchn_15 {
// Source: script_vm_auto_7.cpp
// KNUTH_OVERRIDE #4: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
// KNUTH_OVERRIDE #5: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
// KNUTH_OVERRIDE #6: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
// KNUTH_OVERRIDE #7: fork=KTH_PYTHAGORAS, reason=These reactivated opcodes (div/mod/and/or/xor) need bch_2018_may.
// KNUTH_OVERRIDE #148: error=KTH_INVALID_SCRIPT, sig=RAW:4c01, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
// KNUTH_OVERRIDE #149: sig=RAW:4d0200ff, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
// KNUTH_OVERRIDE #150: sig=RAW:4e03000000ffff, pub=RAW:0161, reason=Malformed PUSHDATA scriptSig + truncated push scriptPubKey. Use RAW: to inject exact bytes since from_string cannot represent these malformed scripts.
// KNUTH_OVERRIDE #125: error=KTH_INVALID_STACK_SCOPE, reason=IF in sig, ENDIF in pub: run() returns invalid_stack_scope at end because IF is not closed within same script. BCHN returns UNBALANCED_CONDITIONAL.
// KNUTH_OVERRIDE #126: error=KTH_INVALID_STACK_SCOPE, reason=NOTIF in sig without ENDIF: run() returns invalid_stack_scope at end. BCHN returns UNBALANCED_CONDITIONAL.
static constexpr bchn_vector rows_0[] = {
    {700, "[0000]", "abs drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {701, "[0000]", "not drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {702, "[0000]", "0notequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {703, "0 [0000]", "add drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {704, "[0000] 0", "add drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {705, "0 [0000]", "sub drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {706, "[0000] 0", "sub drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {707, "0 [0100]", "div drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {708, "[0000] 1", "div drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {709, "0 [0100]", "mod drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {710, "[0000] 1", "mod drop 1", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {711, "0 [0000]", "booland drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {712, "[0000] 0", "booland drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {713, "0 [0000]", "boolor drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {714, "[0000] 0", "boolor drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {715, "0 [0000]", "numequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {716, "[0000] 1", "numequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {717, "0 [0000]", "numequalverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {718, "[0000] 0", "numequalverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {719, "0 [0000]", "numnotequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {720, "[0000] 0", "numnotequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {721, "0 [0000]", "lessthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {722, "[0000] 0", "lessthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {723, "0 [0000]", "greaterthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {724, "[0000] 0", "greaterthan drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {725, "0 [0000]", "lessthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {726, "[0000] 0", "lessthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {727, "0 [0000]", "greaterthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {728, "[0000] 0", "greaterthanorequal drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {729, "0 [0000]", "min drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {730, "[0000] 0", "min drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {731, "0 [0000]", "max drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {732, "[0000] 0", "max drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {733, "[0000] 0 0", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {734, "0 [0000] 0", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {735, "0 0 [0000]", "within drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {736, "0 0 [0000]", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {737, "0 [0000] 0", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {738, "0 [0000] 0 1", "checkmultisig drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {739, "0 0 [0000]", "checkmultisigverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {740, "0 [0000] 0", "checkmultisigverify 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {741, "[0000] [0100]", "split 2drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {742, "[11111111111111111111111111111111111111110000] [1400]", "num2bin drop 1", kth::domain::machine::script_flags::no_rules, kth::error::success, ""},
    {743, "[0000]", "checklocktimeverify drop 1", kth::domain::machine::script_flags::bip65_rule, kth::error::unsatisfied_locktime, "fails because final sequence, not from nonminimal encoding"},
    {744, "[0000]", "checksequenceverify drop 1", kth::domain::machine::script_flags::bip112_rule, kth::error::unsatisfied_locktime, "fails because tx version < 2, not from nonminimal encoding"},
    {745, "0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {746, "0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {747, "0", "[BEEF] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, "Even with a null signature, the public key has to be correctly encoded."},
    {748, "0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""},
    {749, "0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "Invalid public keys are fine, as long as correctly encoded."},
    {750, "0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {751, "0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {752, "0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {753, "0", "[BEEF] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""},
    {754, "0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::pubkey_type, ""},
    {755, "0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {756, "0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc, kth::error::success, ""},
    {757, "0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501] [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 0 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "2-of-2 CHECKMULTISIG NOT with the second pubkey invalid, and both signatures validly encoded. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid pubkey."},
    {758, "0 0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_strictenc, kth::error::success, "2-of-2 CHECKMULTISIG NOT with both pubkeys valid, but second signature invalid. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid signature."},
    {759, "0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Nulled 2-of-2 with valid pubkeys"},
    {760, "0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [BEEF] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::pubkey_type, "Nulled 2-of-2 with top pubkey invalidly encoded"},
    {761, "0 0 0", "2 [BEEF] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::script_flags::bch_nullfail | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Nulled 2-of-2 with top pubkey validly encoded, but another pubkey is invalidly encoded"},
    {762, "[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Overly long signature is correctly encoded"},
    {763, "[30220220000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Missing S is correctly encoded"},
    {764, "[3024021077777777777777777777777777777777020a7777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "S with invalid S length is correctly encoded"},
    {765, "[302403107777777777777777777777777777777702107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Non-integer R is correctly encoded"},
    {766, "[302402107777777777777777777777777777777703107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Non-integer S is correctly encoded"},
    {767, "[3014020002107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Zero-length R is correctly encoded"},
    {768, "[3014021077777777777777777777777777777777020001]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Zero-length S is correctly encoded for DERSIG"},
    {769, "[302402107777777777777777777777777777777702108777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::script_flags::no_rules, kth::error::success, "Negative S is correctly encoded"},
    {770, "2147483648", "checksequenceverify", kth::domain::machine::script_flags::bip112_rule, kth::error::success, "CSV passes if stack top bit 1 << 31 is set"},
    {771, "", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "Test the test: we should have an empty stack after scriptSig evaluation"},
    {772, "", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "and multiple spaces should not change that."},
    {773, "", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {774, "", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {775, "", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {776, "", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {777, "", "nop depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {778, "nop", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {779, "nop", "depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {780, "nop", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {781, "nop", "nop depth", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {782, "depth", "", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {783, "raw:4c01", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_script, "PUSHDATA1 with not enough bytes [KNUTH_OVERRIDE: ERROR: KTH_INVALID_SCRIPT, SIG_REPLACED, PUBKEY_REPLACED]"},
    {784, "raw:4d0200ff", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "PUSHDATA2 with not enough bytes [KNUTH_OVERRIDE: SIG_REPLACED, PUBKEY_REPLACED]"},
    {785, "raw:4e03000000ffff", "raw:0161", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "PUSHDATA4 with not enough bytes [KNUTH_OVERRIDE: SIG_REPLACED, PUBKEY_REPLACED]"},
    {786, "1", "if 0x50 endif 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "0x50 is reserved"},
    {787, "0x52", "0x5f add 0x60 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "0x51 through 0x60 push 1 through 16 onto stack"},
    {788, "0", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, ""},
    {789, "1", "if ver else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "VER non-functional"},
    {790, "1", "if invoke else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "INVOKE not enabled"},
    {791, "1", "if define else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "DEFINE not enabled"},
    {792, "0", "if begin else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "BRGIN before activation illegal everywhere"},
    {793, "0", "if else 1 else begin endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "BEGIN before activation illegal everywhere"},
    {794, "0", "if until else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "UNTIL before activation illegal everywhere"},
    {795, "0", "if else 1 else until endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_reserved, "UNTIL before activation illegal everywhere"},
    {796, "1 if", "1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_stack_scope, "IF/ENDIF can't span scriptSig/scriptPubKey [KNUTH_OVERRIDE: KTH_INVALID_STACK_SCOPE]"},
    {797, "1 if 0 endif", "1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""},
    {798, "1 else 0 endif", "1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::unbalanced_conditional, ""},
    {799, "0 notif", "123", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_stack_scope, " [KNUTH_OVERRIDE: KTH_INVALID_STACK_SCOPE]"},
};
static constexpr bchn_vector_group group = {
    "script_vm_auto_7.cpp", rows_0
};
} // namespace kth::test::data_bchn_bchn_15
