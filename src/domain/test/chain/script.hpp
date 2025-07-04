// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_TEST_SCRIPT_HPP
#define KTH_TEST_SCRIPT_HPP

#include <cstdint>
#include <string>
#include <vector>


/**
 * Structure representing a single script test case.
 */
struct script_test {
    std::string input;
    std::string output;
    std::string description;
    uint32_t input_sequence;
    uint32_t locktime;
    uint32_t version;
};

/**
 * Structure representing a single script test case for BCHN.
 * This includes additional fields for forks and expected error codes.
 */
struct bchn_script_test {
    std::string script_sig;         ///< The scriptSig (input script)
    std::string script_pub_key;     ///< The scriptPubKey (output script)  
    uint32_t forks;                 ///< Active fork rules (OR of kth::domain::machine::rule_fork values)
    kth::error::error_code_t expected_error; ///< Expected error code
    std::string comment;            ///< Test description/comment
};

using script_test_list = std::vector<script_test>;
using bchn_script_test_list = std::vector<bchn_script_test>;

// bip16
//------------------------------------------------------------------------------

// These are valid prior to and after BIP16 activation.
script_test_list const valid_bip16_scripts{
    {"0 [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", "trivial p2sh"},
    {"[1.] [0.51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", "basic p2sh"}};

// These are invalid prior to and after BIP16 activation.
script_test_list const invalid_bip16_scripts{};

// These are valid prior to BIP16 activation and invalid after.
script_test_list const invalidated_bip16_scripts{
    {"nop [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", "is_push_only fails under bip16"},
    {"nop1 [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", "is_push_only fails under bip16"},
    {"0 [50]", "hash160 [ece424a6bb6ddf4db592c0faed60685047a361b1] equal", "op_reserved as p2sh serialized script fails"},
    {"0 [62]", "hash160 [0f4d7845db968f2a81b530b6f3c1d6246d4c7e01] equal", "op_ver as p2sh serialized script fails"}};

// bip65
//------------------------------------------------------------------------------

// These are valid prior to and after BIP65 activation.
script_test_list const valid_bip65_scripts{
    {"42", "checklocktimeverify", "valid cltv, true return", 42, 99},
    {"42", "nop1 nop2 nop4 nop5 nop6 nop7 nop8 nop9 nop10 42 equal", "bip112 would fail nop3", 42, 99}};

// These are invalid prior to and after BIP65 activation.
script_test_list const invalid_bip65_scripts{
    {"", "nop2", "empty nop2", 42, 99},
    {"", "checklocktimeverify", "empty cltv", 42, 99}};

// These are valid prior to BIP65 activation and invalid after.
script_test_list const invalidated_bip65_scripts{
    {"1 -1", "checklocktimeverify", "negative cltv", 42, 99},
    {"1 100", "checklocktimeverify", "exceeded cltv", 42, 99},
    {"1 500000000", "checklocktimeverify", "mismatched cltv", 42, 99},
    {"'nop_1_to_10' nop1 nop2 nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'nop_1_to_10' equal", "bip112 would fail nop3", 42, 99},
    {"nop", "nop2 1", "", 42, 99}};

// bip112
//------------------------------------------------------------------------------

// These are valid prior to and after BIP112 activation.
script_test_list const valid_bip112_scripts{
    {"[0100408000]", "checksequenceverify", "csv stack disabled, true"},
    {"[0100400000]", "checksequenceverify", "csv stack enabled, satisfied locktime", 0x00400001, 0, 2}};

// These are invalid prior to and after BIP112 activation.
script_test_list const invalid_bip112_scripts{
    {"", "nop3", "nop3 empty stack"},
    {"", "checksequenceverify", "csv empty stack"}};

// These are valid prior to BIP112 activation and invalid after.
script_test_list const invalidated_bip112_scripts{
    {"", "checksequenceverify 42", "csv stack empty"},
    {"-42", "checksequenceverify", "csv stack top negative"},
    {"[0100000000]", "checksequenceverify", "csv stack enabled, tx version < 2", 0x00000001, 0, 1},
    {"[0100000000]", "checksequenceverify", "csv stack enabled, tx sequence disabled", 0x80000001, 0, 2},
    {"[0100400000]", "checksequenceverify", "csv stack enabled, mismatched lock type", 0x00000001, 0, 3},
    {"[0100400000]", "checksequenceverify", "csv stack enabled, unsatisfied locktime", 0x00400000, 0, 4}};


// bch_pythagoras - May 2018
//------------------------------------------------------------------------------

// These are valid prior to bch_pythagoras activation and invalid after.
script_test_list const invalidated_bch_pythagoras_scripts {
};

// These are invalid prior to bch_pythagoras activation and valid after (64-bit arithmetic).
script_test_list const validated_bch_pythagoras_scripts{
    // Arithmetic operations: div, mod (re-enabled after pythagoras/monolith upgrade, May 2018)
    {"10 2", "div 5 equal", "div: 10 / 2 = 5"},
    {"15 3", "div 5 equal", "div: 15 / 3 = 5"},
    {"7 3", "div 2 equal", "div: 7 / 3 = 2 (integer division)"},
    {"10 3", "mod 1 equal", "mod: 10 % 3 = 1"},
    {"15 4", "mod 3 equal", "mod: 15 % 4 = 3"},
    {"8 4", "mod 0 equal", "mod: 8 % 4 = 0"},
    {"10 5 1", "if div 2 equal else 1 endif", "div in conditional: true branch"},
    {"7 3 1", "if mod 1 equal else 1 endif", "mod in conditional: true branch"},

    // Bitwise operations - AND, OR, XOR (re-enabled after pythagoras/monolith upgrade, May 2018)
    {"[ff] [0f]", "and [0f] equal", "and: 0xff & 0x0f = 0x0f"},
    {"[aa] [55]", "and [00] equal", "and: 0xaa & 0x55 = 0x00"},
    {"[ff00] [0f0f]", "and [0f00] equal", "and with multi-byte: 0xff00 & 0x0f0f = 0x0f00"},
    {"[00] [ff]", "or [ff] equal", "or: 0x00 | 0xff = 0xff"},
    {"[aa] [55]", "or [ff] equal", "or: 0xaa | 0x55 = 0xff"},
    {"[f0f0] [0f0f]", "or [ffff] equal", "or with multi-byte: 0xf0f0 | 0x0f0f = 0xffff"},
    {"[ff] [ff]", "xor [00] equal", "xor: 0xff ^ 0xff = 0x00"},
    {"[aa] [55]", "xor [ff] equal", "xor: 0xaa ^ 0x55 = 0xff"},
    {"[f0f0] [0ff0]", "xor [ff00] equal", "xor with multi-byte: 0xf0f0 ^ 0x0ff0 = 0xff00"},
    {"[12] [34] 1", "if and [10] equal else 1 endif", "and in conditional: true branch"},
    {"[12] [34] 0", "if and [10] equal else 1 endif 1 equal", "and in conditional: false branch"},
    {"[ff] [00] 1", "if or [ff] equal else 1 endif", "or in conditional: true branch"},
    {"[aa] [55] 1", "if xor [ff] equal else 1 endif", "xor in conditional: true branch"},
};


// bch_gauss - May 2022
//------------------------------------------------------------------------------

// These are valid prior to bch_gauss activation and invalid after.
script_test_list const invalidated_bch_gauss_scripts{
    {"nop [51]", "hash256 [953ccfa596a6c6d39e5980194539124fdcff116a571455a212baed811f585ee0] equal", "is_push_only fails under bip16"},
    {"nop1 [51]", "hash256 [953ccfa596a6c6d39e5980194539124fdcff116a571455a212baed811f585ee0] equal", "is_push_only fails under bip16"},
    {"0 [50]", "hash256 [e2a6aae5db4329c9b78b937e86d427bce671bbda7a202d31fab821d501dbe113] equal", "op_reserved as p2sh serialized script fails"},
    {"0 [62]", "hash256 [39361160903c6695c6804b7157c7bd10013e9ba89b1f954243bc8e3990b08db9] equal", "op_ver as p2sh serialized script fails"}
};

// These are invalid prior to bch_gauss activation and valid after (64-bit arithmetic).
script_test_list const validated_bch_gauss_scripts{
    {"2147483648 0", "add 2147483648 equal", "32-bit overflow: 2^31 + 0 invalid before GAUSS, valid after"},
    {"-2147483648 0", "add -2147483648 equal", "32-bit underflow: -2^31 + 0 invalid before GAUSS, valid after"},

    // {"2147483647 dup", "add 4294967294 equal", "32-bit overflow: (2^31-1) * 2 invalid before GAUSS, valid after"},

    // These test were in the valid set, I think incorrectly, but it would be good to validate why they were there.
    // {"2147483647 dup add", "4294967294 equal", "32-bit overflow: (2^31-1) * 2 invalid before GAUSS, valid after"},
    // {"2147483647 negate dup add", "-4294967294 equal", ""},

    {"4294967295 1", "add 4294967296 equal", "32-bit limit: 2^32-1 + 1 invalid before GAUSS, valid after"},
    {"9223372036854775807 0", "add 9223372036854775807 equal", "64-bit max: 2^63-1 + 0 invalid before GAUSS, valid after"},

    // Additional arithmetic operations with 32-bit limits (now valid with 64-bit arithmetic)
    // {"2147483647", "add1 2147483648 equal", "32-bit boundary: 2^31-1 + 1 = 2^31 invalid before GAUSS, valid after"},
    {"2147483648", "add1 2147483649 equal", "32-bit overflow with add1: 2^31 + 1 invalid before GAUSS, valid after"},
    {"2147483648", "negate -2147483648 equal", "32-bit negate: -2^31 invalid before GAUSS, valid after"},
    {"-2147483648", "add1 -2147483647 equal", "32-bit underflow with add1: -2^31 + 1 invalid before GAUSS, valid after"},
    {"2147483647", "add1 sub1 2147483647 equal", "32-bit chain operations: (2^31-1) + 1 - 1 invalid before GAUSS, valid after"},
    {"2147483648", "sub1 2147483647 equal", "32-bit overflow with sub1: 2^31 - 1 invalid before GAUSS, valid after"},

    // Arithmetic operations: mul (re-enabled after gauss/upgrade8 upgrade, May 2022)
    {"2 3", "mul 6 equal", "mul: 2 * 3 = 6"},
    {"12 4", "mul 48 equal", "mul: 12 * 4 = 48"},
    {"0 5", "mul 0 equal", "mul: 0 * 5 = 0"},
    {"3 2 1", "if mul 6 equal else 1 endif", "mul in conditional: true branch"},

    // Integer size limit tests: invalid before GAUSS (4-byte limit), valid after GAUSS (8-byte limit)
    {"'abcdef' not", "0 equal", "6-byte string as arithmetic operand: invalid before GAUSS (4-byte limit), valid after GAUSS (8-byte limit)"}
};

// bch_galois - May 2025
//------------------------------------------------------------------------------

// These are valid prior to bch_galois activation and invalid after.
script_test_list const invalidated_bch_galois_scripts{
    // Currently no tests that become invalid after Galois
};

// These are invalid prior to bch_galois activation and valid after (10k byte stack element limit).
script_test_list const validated_bch_galois_scripts{
    // Stack element size limit tests: invalid before GALOIS (520-byte limit), valid after GALOIS (10k-byte limit)
    {"nop", "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'", ">520 byte push: invalid before GALOIS (520-byte limit), valid after GALOIS (10k-byte limit)"},
    {"0", "if 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' endif 1", ">520 byte push in non-executed if branch: invalid before GALOIS (520-byte limit), valid after GALOIS (10k-byte limit)"},

};


// context free: multisig
//------------------------------------------------------------------------------

// These are always valid.
script_test_list const valid_multisig_scripts{
    {"", "0 0 0 checkmultisig verify depth 0 equal", "checkmultisig is allowed to have zero keys and/or sigs"}
    // {"", "0 0 0 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 0 1 checkmultisig verify depth 0 equal", "zero sigs means no sigs are checked"},
    // {"", "0 0 0 1 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 2 checkmultisig verify depth 0 equal", "test from up to 20 pubkeys, all not checked"},
    // {"", "0 0 'a' 'b' 'c' 3 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 4 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 5 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 6 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 7 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 8 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 9 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 10 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 11 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 12 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 13 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 14 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 15 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 16 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 17 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 18 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 19 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig verify depth 0 equal", ""},
    // {"", "0 0 'a' 1 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 2 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 3 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 4 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 5 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 6 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 7 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 8 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 9 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 10 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 11 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 12 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 13 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 14 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 15 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 16 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 17 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 18 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 19 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify depth 0 equal", ""},
    // {"", "0 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig", "nopcount is incremented by the number of keys evaluated in addition to the usual one op per op. in this case we have zero keys, so we can execute 201 checkmultisigs"},
    // {"1", "0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify", ""},
    // {"", "nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig", "even though there are no signatures being checked nopcount is incremented by the number of keys."},
    // {"1", "nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify", ""},
};

// These are always invalid.
script_test_list const invalid_multisig_scripts{
    {"", "0 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig", "202 checkmultisigs, fails due to 201 op limit"},
    {"1", "0 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify", ""},
    {"", "nop nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig", "fails due to 201 sig op limit"},
    {"1", "nop nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify", ""},
};

// context free: other
//------------------------------------------------------------------------------

// These are always valid.
script_test_list const valid_context_free_scripts {
    {"", "depth 0 equal", "test the test: we should have an empty stack after scriptsig evaluation"},
    {"  ", "depth 0 equal", "and multiple spaces should not change that."},
    {"   ", "depth 0 equal", ""},
    {"    ", "depth 0 equal", ""},
    {"1 2", "2 equalverify 1 equal", "similarly whitespace around and between symbols"},
    {"1  2", "2 equalverify 1 equal", ""},
    {"  1  2", "2 equalverify 1 equal", ""},
    {"1  2  ", "2 equalverify 1 equal", ""},
    {"  1  2  ", "2 equalverify 1 equal", ""},
    {"1", "", ""},
    {"[0b]", "11 equal", "push 1 byte"},
    {"[417a]", "'Az' equal", "push 2 bytes"},
    {"[417a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a]", "'Azzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz' equal", "push 75 bytes"},
    {"[1.07]", "7 equal", "non-minimal op_pushdata1"},
    {"[2.08]", "8 equal", "non-minimal op_pushdata2"},
    {"[4.09]", "9 equal", "non-minimal op_pushdata4"},
    {"0x4c", "0 equal", "0x4c is op_pushdata1"},
    {"0x4d", "0 equal", "0x4d is op_pushdata2"},
    {"0x4e", "0 equal", "0x4f is op_pushdata4"},
    {"0x4f 1000 add", "999 equal", "1000 - 1 = 999"},
    {"0", "if 0x50 endif 1", "0x50 is reserved (ok if not executed)"},
    {"0x51", "0x5f add 0x60 equal", "0x51 through 0x60 push 1 through 16 onto stack"},
    {"1", "nop", ""},
    {"0", "if ver else 1 endif", "ver non-functional (ok if not executed)"},
    {"0", "if reserved reserved1 reserved2 else 1 endif", "reserved ok in un-executed if"},
    {"1", "dup if endif", ""},
    {"1", "if 1 endif", ""},
    {"1", "dup if else endif", ""},
    {"1", "if 1 else endif", ""},
    {"0", "if else 1 endif", ""},
    {"1 1", "if if 1 else 0 endif endif", ""},
    {"1 0", "if if 1 else 0 endif endif", ""},
    {"1 1", "if if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0 0", "if if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"1 0", "notif if 1 else 0 endif endif", ""},
    {"1 1", "notif if 1 else 0 endif endif", ""},
    {"1 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0", "if 0 else 1 else 0 endif", "multiple else's are valid and executed inverts on each else encountered"},
    {"1", "if 1 else 0 else endif", ""},
    {"1", "if else 0 else 1 endif", ""},
    {"1", "if 1 else 0 else 1 endif add 2 equal", ""},
    {"'' 1", "if sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", ""},
    {"1", "notif 0 else 1 else 0 endif", "multiple else's are valid and execution inverts on each else encountered"},
    {"0", "notif 1 else 0 else endif", ""},
    {"0", "notif else 0 else 1 endif", ""},
    {"0", "notif 1 else 0 else 1 endif add 2 equal", ""},
    {"'' 0", "notif sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", ""},
    {"0", "if 1 if return else return else return endif else 1 if 1 else return else 1 endif else return endif add 2 equal", "nested else else"},
    {"1", "notif 0 notif return else return else return endif else 0 notif 1 else return else 1 endif else return endif add 2 equal", ""},
    {"0", "if return endif 1", "return only works if executed"},
    {"1 1", "verify", ""},
    {"10 0 11 toaltstack drop fromaltstack", "add 21 equal", ""},
    {"'gavin_was_here' toaltstack 11 fromaltstack", "'gavin_was_here' equalverify 11 equal", ""},
    {"0 ifdup", "depth 1 equalverify 0 equal", ""},
    {"1 ifdup", "depth 2 equalverify 1 equalverify 1 equal", ""},
    {"0 drop", "depth 0 equal", ""},
    {"0", "dup 1 add 1 equalverify 0 equal", ""},
    {"0 1", "nip", ""},
    {"1 0", "over depth 3 equalverify", ""},
    {"22 21 20", "0 pick 20 equalverify depth 3 equal", ""},
    {"22 21 20", "1 pick 21 equalverify depth 3 equal", ""},
    {"22 21 20", "2 pick 22 equalverify depth 3 equal", ""},
    {"22 21 20", "0 roll 20 equalverify depth 2 equal", ""},
    {"22 21 20", "1 roll 21 equalverify depth 2 equal", ""},
    {"22 21 20", "2 roll 22 equalverify depth 2 equal", ""},
    {"22 21 20", "rot 22 equal", ""},
    {"22 21 20", "rot drop 20 equal", ""},
    {"22 21 20", "rot drop drop 21 equal", ""},
    {"22 21 20", "rot rot 21 equal", ""},
    {"22 21 20", "rot rot rot 20 equal", ""},
    {"25 24 23 22 21 20", "rot2 24 equal", ""},
    {"25 24 23 22 21 20", "rot2 drop 25 equal", ""},
    {"25 24 23 22 21 20", "rot2 drop2 20 equal", ""},
    {"25 24 23 22 21 20", "rot2 drop2 drop 21 equal", ""},
    {"25 24 23 22 21 20", "rot2 drop2 drop2 22 equal", ""},
    {"25 24 23 22 21 20", "rot2 drop2 drop2 drop 23 equal", ""},
    {"25 24 23 22 21 20", "rot2 rot2 22 equal", ""},
    {"25 24 23 22 21 20", "rot2 rot2 rot2 20 equal", ""},
    {"1 0", "swap 1 equalverify 0 equal", ""},
    {"0 1", "tuck depth 3 equalverify swap drop2", ""},
    {"13 14", "dup2 rot equalverify equal", ""},
    {"-1 0 1 2", "dup3 depth 7 equalverify add add 3 equalverify drop2 0 equalverify", ""},
    {"1 2 3 5", "over2 add add 8 equalverify add add 6 equal", ""},
    {"1 3 5 7", "swap2 add 4 equalverify add 12 equal", ""},
    {"0", "size 0 equal", ""},
    {"1", "size 1 equal", ""},
    {"127", "size 1 equal", ""},
    {"128", "size 2 equal", ""},
    {"32767", "size 2 equal", ""},
    {"32768", "size 3 equal", ""},
    {"8388607", "size 3 equal", ""},
    {"8388608", "size 4 equal", ""},
    {"2147483647", "size 4 equal", ""},
    {"2147483648", "size 5 equal", ""},
    {"549755813887", "size 5 equal", ""},
    {"549755813888", "size 6 equal", ""},
    {"9223372036854775807", "size 8 equal", ""},
    {"-1", "size 1 equal", ""},
    {"-127", "size 1 equal", ""},
    {"-128", "size 2 equal", ""},
    {"-32767", "size 2 equal", ""},
    {"-32768", "size 3 equal", ""},
    {"-8388607", "size 3 equal", ""},
    {"-8388608", "size 4 equal", ""},
    {"-2147483647", "size 4 equal", ""},
    {"-2147483648", "size 5 equal", ""},
    {"-549755813887", "size 5 equal", ""},
    {"-549755813888", "size 6 equal", ""},
    {"-9223372036854775807", "size 8 equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "size 26 equal", ""},
    {"42", "size 1 equalverify 42 equal", "size does not consume argument"},
    {"'a' 'b'", "cat 'ab' equal", "cat concatenates two strings"},
    {"'a' 'b' 0", "if cat else 1 endif 1 equal", "cat in if/else: false condition executes else branch"},
    {"'a' 'b' 1", "if cat else 1 endif 'ab' equal", "cat in if/else: true condition executes cat"},
    {"2 -2 add", "0 equal", ""},
    {"2147483647 -2147483647 add", "0 equal", ""},
    {"-1 -1 add", "-2 equal", ""},
    {"0 0", "equal", ""},
    {"1 1 add", "2 equal", ""},
    {"1 add1", "2 equal", ""},
    {"111 sub1", "110 equal", ""},
    {"111 1 add 12 sub", "100 equal", ""},
    {"0 abs", "0 equal", ""},
    {"16 abs", "16 equal", ""},
    {"-16 abs", "-16 negate equal", ""},
    {"0 not", "nop", ""},
    {"1 not", "0 equal", ""},
    {"11 not", "0 equal", ""},
    {"0 nonzero", "0 equal", ""},
    {"1 nonzero", "1 equal", ""},
    {"111 nonzero", "1 equal", ""},
    {"-111 nonzero", "1 equal", ""},
    {"1 1 booland", "nop", ""},
    {"1 0 booland", "not", ""},
    {"0 1 booland", "not", ""},
    {"0 0 booland", "not", ""},
    {"16 17 booland", "nop", ""},
    {"1 1 boolor", "nop", ""},
    {"1 0 boolor", "nop", ""},
    {"0 1 boolor", "nop", ""},
    {"0 0 boolor", "not", ""},
    {"16 17 boolor", "nop", ""},
    {"11 10 1 add", "numequal", ""},
    {"11 10 1 add", "numequalverify 1", ""},
    {"11 10 1 add", "numnotequal not", ""},
    {"111 10 1 add", "numnotequal", ""},
    {"11 10", "lessthan not", ""},
    {"4 4", "lessthan not", ""},
    {"10 11", "lessthan", ""},
    {"-11 11", "lessthan", ""},
    {"-11 -10", "lessthan", ""},
    {"11 10", "greaterthan", ""},
    {"4 4", "greaterthan not", ""},
    {"10 11", "greaterthan not", ""},
    {"-11 11", "greaterthan not", ""},
    {"-11 -10", "greaterthan not", ""},
    {"11 10", "lessthanorequal not", ""},
    {"4 4", "lessthanorequal", ""},
    {"10 11", "lessthanorequal", ""},
    {"-11 11", "lessthanorequal", ""},
    {"-11 -10", "lessthanorequal", ""},
    {"11 10", "greaterthanorequal", ""},
    {"4 4", "greaterthanorequal", ""},
    {"10 11", "greaterthanorequal not", ""},
    {"-11 11", "greaterthanorequal not", ""},
    {"-11 -10", "greaterthanorequal not", ""},
    {"1 0 min", "0 numequal", ""},
    {"0 1 min", "0 numequal", ""},
    {"-1 0 min", "-1 numequal", ""},
    {"0 -2147483647 min", "-2147483647 numequal", ""},
    {"2147483647 0 max", "2147483647 numequal", ""},
    {"0 100 max", "100 numequal", ""},
    {"-100 0 max", "0 numequal", ""},
    {"0 -2147483647 max", "0 numequal", ""},
    {"0 0 1", "within", ""},
    {"1 0 1", "within not", ""},
    {"0 -2147483647 2147483647", "within", ""},
    {"-1 -100 100", "within", ""},
    {"11 -100 100", "within", ""},
    {"-2147483647 -100 100", "within not", ""},
    {"2147483647 -100 100", "within not", ""},
    {"2147483647 2147483647 sub", "0 equal", ""},    
    {"2147483647 dup add", "4294967294 equal", ">32 bit equal is valid"},
    {"2147483647 negate dup add", "-4294967294 equal", ""},
    {"''", "ripemd160 [9c1185a5c5e9fc54612808977ee8f548b2258d31] equal", ""},
    {"'a'", "ripemd160 [0bdc9d2d256b3ee9daae347be6f4dc835a467ffe] equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "ripemd160 [f71c27109c692c1b56bbdceb5b9d2865b3708dbc] equal", ""},
    {"''", "sha1 [da39a3ee5e6b4b0d3255bfef95601890afd80709] equal", ""},
    {"'a'", "sha1 [86f7e437faa5a7fce15d1ddcb9eaeaea377667b8] equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "sha1 [32d10c7b8cf96570ca04ce37f2a19d84240d3a89] equal", ""},
    {"''", "sha256 [e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855] equal", ""},
    {"'a'", "sha256 [ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb] equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "sha256 [71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73] equal", ""},
    {"''", "dup hash160 swap sha256 ripemd160 equal", ""},
    {"''", "dup hash256 swap sha256 sha256 equal", ""},
    {"''", "nop hash160 [b472a266d0bd89c13706a4132ccfb16f7c3b9fcb] equal", ""},
    {"'a'", "hash160 nop [994355199e516ff76c4fa4aab39337b9d84cf12b] equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "hash160 [1.c286a1af0947f58d1ad787385b1c2c4a976f9e71] equal", ""},
    {"''", "hash256 nop [5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456] equal", ""},
    {"'a'", "hash256 nop [bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8] equal", ""},
    {"'abcdefghijklmnopqrstuvwxyz'", "hash256 nop [1.ca139bc10c2f660da42666f72e89a225936fc60f193c161124a672050c434671] equal", ""},
    {"1", "nop1 nop4 nop5 nop6 nop7 nop8 nop9 nop10 1 equal", ""},

    {"'nop_1_to_10' nop1 nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'nop_1_to_10' equal", ""},
    {"0", "if 0xba else 1 endif", "opcodes above nop10 invalid if executed"},
    {"0", "if 0xbb else 1 endif", ""},
    {"0", "if 0xbc else 1 endif", ""},
    {"0", "if 0xbd else 1 endif", ""},
    {"0", "if 0xbe else 1 endif", ""},
    {"0", "if 0xbf else 1 endif", ""},
    {"0", "if 0xc0 else 1 endif", ""},
    {"0", "if 0xc1 else 1 endif", ""},
    {"0", "if 0xc2 else 1 endif", ""},
    {"0", "if 0xc3 else 1 endif", ""},
    {"0", "if 0xc4 else 1 endif", ""},
    {"0", "if 0xc5 else 1 endif", ""},
    {"0", "if 0xc6 else 1 endif", ""},
    {"0", "if 0xc7 else 1 endif", ""},
    {"0", "if 0xc8 else 1 endif", ""},
    {"0", "if 0xc9 else 1 endif", ""},
    {"0", "if 0xca else 1 endif", ""},
    {"0", "if 0xcb else 1 endif", ""},
    {"0", "if 0xcc else 1 endif", ""},
    {"0", "if 0xcd else 1 endif", ""},
    {"0", "if 0xce else 1 endif", ""},
    {"0", "if 0xcf else 1 endif", ""},
    {"0", "if 0xd0 else 1 endif", ""},
    {"0", "if 0xd1 else 1 endif", ""},
    {"0", "if 0xd2 else 1 endif", ""},
    {"0", "if 0xd3 else 1 endif", ""},
    {"0", "if 0xd4 else 1 endif", ""},
    {"0", "if 0xd5 else 1 endif", ""},
    {"0", "if 0xd6 else 1 endif", ""},
    {"0", "if 0xd7 else 1 endif", ""},
    {"0", "if 0xd8 else 1 endif", ""},
    {"0", "if 0xd9 else 1 endif", ""},
    {"0", "if 0xda else 1 endif", ""},
    {"0", "if 0xdb else 1 endif", ""},
    {"0", "if 0xdc else 1 endif", ""},
    {"0", "if 0xdd else 1 endif", ""},
    {"0", "if 0xde else 1 endif", ""},
    {"0", "if 0xdf else 1 endif", ""},
    {"0", "if 0xe0 else 1 endif", ""},
    {"0", "if 0xe1 else 1 endif", ""},
    {"0", "if 0xe2 else 1 endif", ""},
    {"0", "if 0xe3 else 1 endif", ""},
    {"0", "if 0xe4 else 1 endif", ""},
    {"0", "if 0xe5 else 1 endif", ""},
    {"0", "if 0xe6 else 1 endif", ""},
    {"0", "if 0xe7 else 1 endif", ""},
    {"0", "if 0xe8 else 1 endif", ""},
    {"0", "if 0xe9 else 1 endif", ""},
    {"0", "if 0xea else 1 endif", ""},
    {"0", "if 0xeb else 1 endif", ""},
    {"0", "if 0xec else 1 endif", ""},
    {"0", "if 0xed else 1 endif", ""},
    {"0", "if 0xee else 1 endif", ""},
    {"0", "if 0xef else 1 endif", ""},
    {"0", "if 0xf0 else 1 endif", ""},
    {"0", "if 0xf1 else 1 endif", ""},
    {"0", "if 0xf2 else 1 endif", ""},
    {"0", "if 0xf3 else 1 endif", ""},
    {"0", "if 0xf4 else 1 endif", ""},
    {"0", "if 0xf5 else 1 endif", ""},
    {"0", "if 0xf6 else 1 endif", ""},
    {"0", "if 0xf7 else 1 endif", ""},
    {"0", "if 0xf8 else 1 endif", ""},
    {"0", "if 0xf9 else 1 endif", ""},
    {"0", "if 0xfa else 1 endif", ""},
    {"0", "if 0xfb else 1 endif", ""},
    {"0", "if 0xfc else 1 endif", ""},
    {"0", "if 0xfd else 1 endif", ""},
    {"0", "if 0xfe else 1 endif", ""},
    {"0", "if 0xff else 1 endif", ""},
    {"nop", "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'", "520 byte push"},
    {"1", "0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61  0x61", "201 opcodes executed. 0x61 is nop"},
    {"1 2 3 4 5 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1 2 3 4 5 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1,000 stack size (0x6f is dup3)"},
    {"1 toaltstack 2 toaltstack 3 4 5 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1 2 3 4 5 6 7 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1,000 stack size (altstack cleared between scriptsig/scriptpubkey)"},
    {"'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f dup2 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61", "max-size (10,000-byte), max-push(520 bytes), max-opcodes(201), max stack size(1,000 items). 0x6f is dup3, 0x61 is nop"},
    {"0", "if 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 0x50 endif 1", ">201 opcodes, but reserved (0x50) doesn't count towards opcode limit."},
    {"nop", "1", ""},
    {"1", "[01] equal", "the following is useful for checking implementations of bn_bn2mpi"},
    {"127", "[7f] equal", ""},
    {"128", "[8000] equal", "leave room for the sign bit"},
    {"32767", "[ff7f] equal", ""},
    {"32768", "[008000] equal", ""},
    {"8388607", "[ffff7f] equal", ""},
    {"8388608", "[00008000] equal", ""},
    {"2147483647", "[ffffff7f] equal", ""},
    {"2147483648", "[0000008000] equal", ""},
    {"549755813887", "[ffffffff7f] equal", ""},
    {"549755813888", "[000000008000] equal", ""},
    {"9223372036854775807", "[ffffffffffffff7f] equal", ""},
    {"-1", "[81] equal", "numbers are little-endian with the msb being a sign bit"},
    {"-127", "[ff] equal", ""},
    {"-128", "[8080] equal", ""},
    {"-32767", "[ffff] equal", ""},
    {"-32768", "[008080] equal", ""},
    {"-8388607", "[ffffff] equal", ""},
    {"-8388608", "[00008080] equal", ""},
    {"-2147483647", "[ffffffff] equal", ""},
    {"-2147483648", "[0000008080] equal", ""},
    {"-4294967295", "[ffffffff80] equal", ""},
    {"-549755813887", "[ffffffffff] equal", ""},
    {"-549755813888", "[000000008080] equal", ""},
    {"-9223372036854775807", "[ffffffffffffffff] equal", ""},
    {"2147483647", "add1 2147483648 equal", "we can do math on 4-byte integers, and compare 5-byte ones"},
    {"2147483647", "add1 1", ""},
    {"-2147483647", "add1 1", ""},
    {"1", "[0100] equal not", "not the same byte array..."},
    {"1", "[0100] numequal", "... but they are numerically equal"},
    {"11", "[1.0b0000] numequal", ""},
    {"0", "[80] equal not", ""},
    {"0", "[80] numequal", "zero numerically equals negative zero"},
    {"0", "[0080] numequal", ""},
    {"[000080]", "[00000080] numequal", ""},
    {"[100080]", "[10000080] numequal", ""},
    {"[100000]", "[10000000] numequal", ""},
    {"nop", "nop 1", "the following tests check the if(stack.size() < n) tests in each opcode"},
    {"1", "if 1 endif", "they are here to catch copy-and-paste errors"},
    {"0", "notif 1 endif", "most of them are duplicated elsewhere,"},
    {"1", "verify 1", "but, hey, more is always better, right?"},
    {"0", "toaltstack 1", ""},
    {"1", "toaltstack fromaltstack", ""},
    {"0 0", "drop2 1", ""},
    {"0 1", "dup2", ""},
    {"0 0 1", "dup3", ""},
    {"0 1 0 0", "over2", ""},
    {"0 1 0 0 0 0", "rot2", ""},
    {"0 1 0 0", "swap2", ""},
    {"1", "ifdup", ""},
    {"nop", "depth 1", ""},
    {"0", "drop 1", ""},
    {"1", "dup", ""},
    {"0 1", "nip", ""},
    {"1 0", "over", ""},
    {"1 0 0 0 3", "pick", ""},
    {"1 0", "pick", ""},
    {"1 0 0 0 3", "roll", ""},
    {"1 0", "roll", ""},
    {"1 0 0", "rot", ""},
    {"1 0", "swap", ""},
    {"0 1", "tuck", ""},
    {"1", "size", ""},
    {"0 0", "equal", ""},
    {"0 0", "equalverify 1", ""},
    {"0", "add1", ""},
    {"2", "sub1", ""},
    {"-1", "negate", ""},
    {"-1", "abs", ""},
    {"0", "not", ""},
    {"-1", "nonzero", ""},
    {"1 0", "add", ""},
    {"1 0", "sub", ""},
    {"-1 -1", "booland", ""},
    {"-1 0", "boolor", ""},
    {"0 0", "numequal", ""},
    {"0 0", "numequalverify 1", ""},
    {"-1 0", "numnotequal", ""},
    {"-1 0", "lessthan", ""},
    {"1 0", "greaterthan", ""},
    {"0 0", "lessthanorequal", ""},
    {"0 0", "greaterthanorequal", ""},
    {"-1 0", "min", ""},
    {"1 0", "max", ""},
    {"-1 -1 0", "within", ""},
    {"0", "ripemd160", ""},
    {"0", "sha1", ""},
    {"0", "sha256", ""},
    {"0", "hash160", ""},
    {"0", "hash256", ""},
    {"nop", "codeseparator 1", ""},
    {"nop", "nop1 1", ""},
    {"nop", "nop4 1", ""},
    {"nop", "nop5 1", ""},
    {"nop", "nop6 1", ""},
    {"nop", "nop7 1", ""},
    {"nop", "nop8 1", ""},
    {"nop", "nop9 1", ""},
    {"nop", "nop10 1", ""},

    {"'abcdef' 0", "split 'abcdef' equalverify '' equal", "split at position 0: all goes to second part"},
    {"'abcdef' 3", "split 'def' equalverify 'abc' equal", "split at position 3: splits string into 'abc' and 'def'"},
    {"'abcdef' 6", "split '' equalverify 'abcdef' equal", "split at end: first part gets all, second is empty"},
    {"'hello' 2", "split 'llo' equalverify 'he' equal", "split 'hello' at position 2"},
    {"'abc' 'def' cat 3", "split 'def' equalverify 'abc' equal", "split concatenated string"},
    {"'abcdef' 3 1", "if split 'def' equalverify 'abc' equal else 1 endif", "split in if/else: true condition executes split"},
    {"'abcdef' 3 0", "if split 'dev' equalverify 'abc' equal else 1 endif 1 equal", "split in if/else: false condition executes else branch"},

    {"[42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242]", "[2.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242] equal", "basic push signedness check"},
    {"[1.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242]", "[2.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242] equal", "basic pushdata1 signedness check"},
    {"0x00", "size 0 equal", "basic op_0 execution"},

    // P2SH32 tests (valid before and after bch_gauss activation)
    {"0 [51]", "hash256 [953ccfa596a6c6d39e5980194539124fdcff116a571455a212baed811f585ee0] equal", "trivial p2sh32"},
    {"[1.] [0.51]", "hash256 [953ccfa596a6c6d39e5980194539124fdcff116a571455a212baed811f585ee0] equal", "basic p2sh32"}

};

// These are always invalid.
script_test_list const invalid_context_free_scripts{
    {"", "depth", "test the test: we should have an empty stack after scriptsig evaluation"},
    {"  ", "depth", "and multiple spaces should not change that."},
    {"   ", "depth", ""},
    {"    ", "depth", ""},
    {"", "", ""},
    {"", "nop", ""},
    {"", "nop depth", ""},
    {"nop", "", ""},
    {"nop", "depth", ""},
    {"nop", "nop", ""},
    {"nop", "nop depth", ""},
    {"depth", "", ""},

    // Reserved.
    {"1", "if 0x50 endif 1", "0x50 is reserved"},

    {"0x52", "0x5f add 0x60 equal", "0x51 through 0x60 push 1 through 16 onto stack"},
    {"0", "nop", ""},

    // Reserved.
    {"1", "if ver else 1 endif", "ver is reserved"},
    {"0", "if verif else 1 endif", "verif illegal everywhere (but also reserved?)"},
    {"0", "if else 1 else verif endif", "verif illegal everywhere (but also reserved?)"},
    {"0", "if vernotif else 1 endif", "vernotif illegal everywhere (but also reserved?)"},
    {"0", "if else 1 else vernotif endif", "vernotif illegal everywhere (but also reserved?)"},

    {"1 if", "1 endif", "if/endif can't span scriptsig/scriptpubkey"},
    {"1 if 0 endif", "1 endif", ""},
    {"1 else 0 endif", "1", ""},
    {"0 notif", "123", ""},
    {"0", "dup if endif", ""},
    {"0", "if 1 endif", ""},
    {"0", "dup if else endif", ""},
    {"0", "if 1 else endif", ""},
    {"0", "notif else 1 endif", ""},
    {"0 1", "if if 1 else 0 endif endif", ""},
    {"0 0", "if if 1 else 0 endif endif", ""},
    {"1 0", "if if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0 1", "if if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0 0", "notif if 1 else 0 endif endif", ""},
    {"0 1", "notif if 1 else 0 endif endif", ""},
    {"1 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"0 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", ""},
    {"1", "if return else else 1 endif", "multiple elses"},
    {"1", "if 1 else else return endif", ""},
    {"1", "endif", "malformed if/else/endif sequence"},
    {"1", "else endif", ""},
    {"1", "endif else", ""},
    {"1", "endif else if", ""},
    {"1", "if else endif else", ""},
    {"1", "if else endif else endif", ""},
    {"1", "if endif endif", ""},
    {"1", "if else else endif endif", ""},
    {"1", "return", ""},
    {"1", "dup if return endif", ""},
    {"1", "return 'data'", "canonical prunable txout format"},
    {"0 if", "return endif 1", "still prunable because if/endif can't span scriptsig/scriptpubkey"},
    {"0", "verify 1", ""},
    {"1", "verify", ""},
    {"1", "verify 0", ""},
    {"1 toaltstack", "fromaltstack 1", "alt stack not shared between sig/pubkey"},
    {"ifdup", "depth 0 equal", ""},
    {"drop", "depth 0 equal", ""},
    {"dup", "depth 0 equal", ""},
    {"1", "dup 1 add 2 equalverify 0 equal", ""},
    {"nop", "nip", ""},
    {"nop", "1 nip", ""},
    {"nop", "1 0 nip", ""},
    {"nop", "over 1", ""},
    {"1", "over", ""},
    {"0 1", "over depth 3 equalverify", ""},
    {"19 20 21", "pick 19 equalverify depth 2 equal", ""},
    {"nop", "0 pick", ""},
    {"1", "-1 pick", ""},
    {"19 20 21", "0 pick 20 equalverify depth 3 equal", ""},
    {"19 20 21", "1 pick 21 equalverify depth 3 equal", ""},
    {"19 20 21", "2 pick 22 equalverify depth 3 equal", ""},
    {"nop", "0 roll", ""},
    {"1", "-1 roll", ""},
    {"19 20 21", "0 roll 20 equalverify depth 2 equal", ""},
    {"19 20 21", "1 roll 21 equalverify depth 2 equal", ""},
    {"19 20 21", "2 roll 22 equalverify depth 2 equal", ""},
    {"nop", "rot 1", ""},
    {"nop", "1 rot 1", ""},
    {"nop", "1 2 rot 1", ""},
    {"nop", "0 1 2 rot", ""},
    {"nop", "swap 1", ""},
    {"1", "swap 1", ""},
    {"0 1", "swap 1 equalverify", ""},
    {"nop", "tuck 1", ""},
    {"1", "tuck 1", ""},
    {"1 0", "tuck depth 3 equalverify swap drop2", ""},
    {"nop", "dup2 1", ""},
    {"1", "dup2 1", ""},
    {"nop", "dup3 1", ""},
    {"1", "dup3 1", ""},
    {"1 2", "dup3 1", ""},
    {"nop", "over2 1", ""},
    {"1", "2 3 over2 1", ""},
    {"nop", "swap2 1", ""},
    {"1", "2 3 swap2 1", ""},

    {"nop", "size 1", ""},

    // Disabled.
    {"'abc'", "if invert else 1 endif", "invert disabled"},
    {"2 0 if 2mul else 1 endif", "nop", "2mul disabled"},
    {"2 0 if 2div else 1 endif", "nop", "2div disabled"},
    {"2 2 0 if lshift else 1 endif", "nop", "lshift disabled"},
    {"2 2 0 if rshift else 1 endif", "nop", "rshift disabled"},
    // Note: and, or, xor were re-enabled after pythagoras/monolith upgrade (May 2018) - moved to valid section
    // Note: div, mod were re-enabled after pythagoras/monolith upgrade (May 2018) - moved to valid section
    // Note: mul was also re-enabled after gauss/upgrade8 upgrade (May 2022)

    {"0 1", "equal", ""},
    {"1 1 add", "0 equal", ""},
    {"11 1 add 12 sub", "11 equal", ""},        
    // {"2147483648 0 add", "nop", "arithmetic operands must be in range [-2^31...2^31] "},
    // {"-2147483648 0 add", "nop", "arithmetic operands must be in range [-2^31...2^31] "},
    // {"2147483647 dup add", "4294967294 numequal", "numequal must be in numeric range"},
    // {"'abcdef' not", "0 equal", "not is an arithmetic operand"},

    // Disabled.
    {"2 2mul", "4 equal", "2mul disabled"},
    {"2 2div", "1 equal", "2div disabled"},
    {"2 2 lshift", "8 equal", "lshift disabled"},
    {"2 1 rshift", "1 equal", "rshift disabled"},

    {"1", "nop1 nop2 nop3 nop4 nop5 nop6 nop7 nop8 nop9 nop10 2 equal", ""},
    {"'nop_1_to_10' nop1 nop2 nop3 nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'nop_1_to_11' equal", ""},

    // Reserved.
    {"0x50", "1", "opcode 0x50 is reserved"},

    {"1", "if 0xba else 1 endif", "opcode 0xba invalid if executed"},
    {"1", "if 0xbb else 1 endif", "opcode 0xbb invalid if executed"},
    {"1", "if 0xbc else 1 endif", "opcode 0xbc invalid if executed"},
    {"1", "if 0xbd else 1 endif", "opcode 0xbd invalid if executed"},
    {"1", "if 0xbe else 1 endif", "opcode 0xbe invalid if executed"},
    {"1", "if 0xbf else 1 endif", "opcode 0xbf invalid if executed"},
    {"1", "if 0xc0 else 1 endif", "opcode 0xc0 invalid if executed"},
    {"1", "if 0xc1 else 1 endif", "opcode 0xc1 invalid if executed"},
    {"1", "if 0xc2 else 1 endif", "opcode 0xc2 invalid if executed"},
    {"1", "if 0xc3 else 1 endif", "opcode 0xc3 invalid if executed"},
    {"1", "if 0xc4 else 1 endif", "opcode 0xc4 invalid if executed"},
    {"1", "if 0xc5 else 1 endif", "opcode 0xc5 invalid if executed"},
    {"1", "if 0xc6 else 1 endif", "opcode 0xc6 invalid if executed"},
    {"1", "if 0xc7 else 1 endif", "opcode 0xc7 invalid if executed"},
    {"1", "if 0xc8 else 1 endif", "opcode 0xc8 invalid if executed"},
    {"1", "if 0xc9 else 1 endif", "opcode 0xc9 invalid if executed"},
    {"1", "if 0xca else 1 endif", "opcode 0xca invalid if executed"},
    {"1", "if 0xcb else 1 endif", "opcode 0xcb invalid if executed"},
    {"1", "if 0xcc else 1 endif", "opcode 0xcc invalid if executed"},
    {"1", "if 0xcd else 1 endif", "opcode 0xcd invalid if executed"},
    {"1", "if 0xce else 1 endif", "opcode 0xce invalid if executed"},
    {"1", "if 0xcf else 1 endif", "opcode 0xcf invalid if executed"},
    {"1", "if 0xd0 else 1 endif", "opcode 0xd0 invalid if executed"},
    {"1", "if 0xd1 else 1 endif", "opcode 0xd1 invalid if executed"},
    {"1", "if 0xd2 else 1 endif", "opcode 0xd2 invalid if executed"},
    {"1", "if 0xd3 else 1 endif", "opcode 0xd3 invalid if executed"},
    {"1", "if 0xd4 else 1 endif", "opcode 0xd4 invalid if executed"},
    {"1", "if 0xd5 else 1 endif", "opcode 0xd5 invalid if executed"},
    {"1", "if 0xd6 else 1 endif", "opcode 0xd6 invalid if executed"},
    {"1", "if 0xd7 else 1 endif", "opcode 0xd7 invalid if executed"},
    {"1", "if 0xd8 else 1 endif", "opcode 0xd8 invalid if executed"},
    {"1", "if 0xd9 else 1 endif", "opcode 0xd9 invalid if executed"},
    {"1", "if 0xda else 1 endif", "opcode 0xda invalid if executed"},
    {"1", "if 0xdb else 1 endif", "opcode 0xdb invalid if executed"},
    {"1", "if 0xdc else 1 endif", "opcode 0xdc invalid if executed"},
    {"1", "if 0xdd else 1 endif", "opcode 0xdd invalid if executed"},
    {"1", "if 0xde else 1 endif", "opcode 0xde invalid if executed"},
    {"1", "if 0xdf else 1 endif", "opcode 0xdf invalid if executed"},
    {"1", "if 0xe0 else 1 endif", "opcode 0xe0 invalid if executed"},
    {"1", "if 0xe1 else 1 endif", "opcode 0xe1 invalid if executed"},
    {"1", "if 0xe2 else 1 endif", "opcode 0xe2 invalid if executed"},
    {"1", "if 0xe3 else 1 endif", "opcode 0xe3 invalid if executed"},
    {"1", "if 0xe4 else 1 endif", "opcode 0xe4 invalid if executed"},
    {"1", "if 0xe5 else 1 endif", "opcode 0xe5 invalid if executed"},
    {"1", "if 0xe6 else 1 endif", "opcode 0xe6 invalid if executed"},
    {"1", "if 0xe7 else 1 endif", "opcode 0xe7 invalid if executed"},
    {"1", "if 0xe8 else 1 endif", "opcode 0xe8 invalid if executed"},
    {"1", "if 0xe9 else 1 endif", "opcode 0xe9 invalid if executed"},
    {"1", "if 0xea else 1 endif", "opcode 0xea invalid if executed"},
    {"1", "if 0xeb else 1 endif", "opcode 0xeb invalid if executed"},
    {"1", "if 0xec else 1 endif", "opcode 0xec invalid if executed"},
    {"1", "if 0xed else 1 endif", "opcode 0xed invalid if executed"},
    {"1", "if 0xee else 1 endif", "opcode 0xee invalid if executed"},
    {"1", "if 0xef else 1 endif", "opcode 0xef invalid if executed"},
    {"1", "if 0xf0 else 1 endif", "opcode 0xf0 invalid if executed"},
    {"1", "if 0xf1 else 1 endif", "opcode 0xf1 invalid if executed"},
    {"1", "if 0xf2 else 1 endif", "opcode 0xf2 invalid if executed"},
    {"1", "if 0xf3 else 1 endif", "opcode 0xf3 invalid if executed"},
    {"1", "if 0xf4 else 1 endif", "opcode 0xf4 invalid if executed"},
    {"1", "if 0xf5 else 1 endif", "opcode 0xf5 invalid if executed"},
    {"1", "if 0xf6 else 1 endif", "opcode 0xf6 invalid if executed"},
    {"1", "if 0xf7 else 1 endif", "opcode 0xf7 invalid if executed"},
    {"1", "if 0xf8 else 1 endif", "opcode 0xf8 invalid if executed"},
    {"1", "if 0xf9 else 1 endif", "opcode 0xf9 invalid if executed"},
    {"1", "if 0xfa else 1 endif", "opcode 0xfa invalid if executed"},
    {"1", "if 0xfb else 1 endif", "opcode 0xfb invalid if executed"},
    {"1", "if 0xfc else 1 endif", "opcode 0xfc invalid if executed"},
    {"1", "if 0xfd else 1 endif", "opcode 0xfd invalid if executed"},
    {"1", "if 0xfe else 1 endif", "opcode 0xfe invalid if executed"},
    {"1", "if 0xff else 1 endif", "opcode 0xff invalid if executed"},
    {"1 if 1 else", "0xff endif", "invalid because scriptsig and scriptpubkey are processed separately"},
    {"nop", "ripemd160", ""},
    {"nop", "sha1", ""},
    {"nop", "sha256", ""},
    {"nop", "hash160", ""},
    {"nop", "hash256", ""},
    {"1", "0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61", ">201 opcodes executed. 0x61 is nop"},
    {"0", "if 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 endif 1", ">201 opcodes including non-executed if branch. 0x61 is nop"},
    {"1 2 3 4 5 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1 2 3 4 5 6 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", ">1,000 stack size (0x6f is dup3)"},
    {"1 2 3 4 5 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", "1 toaltstack 2 toaltstack 3 4 5 6 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f", ">1,000 stack+altstack size"},
    {"nop", "0 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f 0x6f dup2 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61 0x61", "10,001-byte scriptpubkey"},
    {"nop1", "nop10", ""},

    // Reserved.
    {"1", "ver", "op_ver is reserved"},
    {"1", "verif", "op_verif is reserved"},
    {"1", "vernotif", "op_vernotif is reserved"},
    {"1", "reserved", "op_reserved is reserved"},
    {"1", "reserved1", "op_reserved1 is reserved"},
    {"1", "reserved2", "op_reserved2 is reserved"},

    {"1", "0xba", "0xba == op_nop10 + 1"},

    // 64-bit arithmetic limits (after GAUSS upgrade, May 2022)
    {"9223372036854775807", "add1 sub1 1", "we cannot do math on 9-byte integers, even if the result is 8-bytes"},

    {"1", "1 endif", "endif without if"},
    {"1", "if 1", "if without endif"},
    {"1 if 1", "endif", "ifs don't carry over"},
    {"nop", "if 1 endif", "the following tests check the if(stack.size() < n) tests in each opcode"},
    {"nop", "notif 1 endif", "they are here to catch copy-and-paste errors"},
    {"nop", "verify 1", "most of them are duplicated elsewhere,"},
    {"nop", "toaltstack 1", "but, hey, more is always better, right?"},
    {"1", "fromaltstack", ""},
    {"1", "drop2 1", ""},
    {"1", "dup2", ""},
    {"1 1", "dup3", ""},
    {"1 1 1", "over2", ""},
    {"1 1 1 1 1", "rot2", ""},
    {"1 1 1", "swap2", ""},
    {"nop", "ifdup 1", ""},
    {"nop", "drop 1", ""},
    {"nop", "dup 1", ""},
    {"1", "nip", ""},
    {"1", "over", ""},
    {"1 1 1 3", "pick", ""},
    {"0", "pick 1", ""},
    {"1 1 1 3", "roll", ""},
    {"0", "roll 1", ""},
    {"1 1", "rot", ""},
    {"1", "swap", ""},
    {"1", "tuck", ""},
    {"nop", "size 1", ""},
    {"1", "equal 1", ""},
    {"1", "equalverify 1", ""},
    {"nop", "add1 1", ""},
    {"nop", "sub1 1", ""},
    {"nop", "negate 1", ""},
    {"nop", "abs 1", ""},
    {"nop", "not 1", ""},
    {"nop", "nonzero 1", ""},
    {"1", "add", ""},
    {"1", "sub", ""},
    {"1", "booland", ""},
    {"1", "boolor", ""},
    {"1", "numequal", ""},
    {"1", "numequalverify 1", ""},
    {"1", "numnotequal", ""},
    {"1", "lessthan", ""},
    {"1", "greaterthan", ""},
    {"1", "lessthanorequal", ""},
    {"1", "greaterthanorequal", ""},
    {"1", "min", ""},
    {"1", "max", ""},
    {"1 1", "within", ""},
    {"nop", "ripemd160 1", ""},
    {"nop", "sha1 1", ""},
    {"nop", "sha256 1", ""},
    {"nop", "hash160 1", ""},
    {"nop", "hash256 1", ""},
    {"0x00", "'00' equal", "basic op_0 execution"},

    // 64-bit arithmetic overflow/underflow (invalid before and after GAUSS activation)
    {"9223372036854775807 1", "add nop", "64-bit overflow: 2^63-1 + 1 = 2^63 exceeds range"},
};


// TODO: move these to operation tests. These are always invalid due to parsing.

// Tests that fail during transaction/script construction (before verification)
// These decimal numbers are unrepresentable in int64_t, causing construction-time failures
// The script parser cannot construct these scripts due to numeric parsing limits
script_test_list const invalid_construction_scripts {
    // Decimal numbers larger than int64_t max (9223372036854775807) fail at construction
    {"9223372036854775808 0", "add nop", "2^63 exceeds int64_t max - construction failure"},
    {"-9223372036854775809 0", "add nop", "-2^63-1 exceeds int64_t min - construction failure"},
    {"18446744073709551615 0", "add nop", "2^64-1 exceeds int64_t max - construction failure"},

    // Single operand decimal numbers that fail at construction
    {"9223372036854775808", "add1 1", "2^63 cannot be represented in int64_t - construction failure"},
    {"9223372036854775808", "negate 1", "2^63 cannot be represented in int64_t - construction failure"},
    {"-9223372036854775809", "add1 1", "-2^63-1 cannot be represented in int64_t - construction failure"},
    {"9223372036854775808", "sub1 1", "2^63 cannot be represented in int64_t - construction failure"},
    {"-9223372036854775808 0", "add -9223372036854775808 equal", "2^63 cannot be represented in int64_t - construction failure"},

    {"-9223372036854775808 -1", "add nop", "64-bit underflow: -2^63 + -1 = -2^63-1 exceeds range"},
    {"-9223372036854775808 1", "sub nop", "64-bit underflow: -2^63 - 1 = -2^63-1 exceeds range"}
};

//// TODO: move these to operation tests.
//// These are always invalid due to parsing.
//script_test_list const invalid_parse_scripts
//{
//    { "0x4c01", "0x01 NOP", "PUSHDATA1 with not enough bytes" },
//    { "0x4d0200ff", "0x01 NOP", "PUSHDATA2 with not enough bytes" },
//    { "0x4e03000000ffff", "0x01 NOP", "PUSHDATA4 with not enough bytes" }
//};


/**
 * List of all script test cases from script_tests.json
 */


// BEGIN AUTO-GENERATED SCRIPT TESTS - DO NOT EDIT
/**
 * Script test chunk 0 (tests 0 to 99)
 */
bchn_script_test_list const script_tests_from_json_0{
    {"", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Test the test: we should have an empty stack after scriptSig evaluation"}, // flags: P2SH,STRICTENC, expected: OK
    {"", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "and multiple spaces should not change that."}, // flags: P2SH,STRICTENC, expected: OK
    {"", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2", "2 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Similarly whitespace around and between symbols"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2", "2 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2", "2 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2", "2 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2", "2 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[0100]", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "all bytes are significant, not only the last one"}, // flags: P2SH,STRICTENC, expected: OK
    {"[000000000000000010]", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "equals zero when cast to Int64"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0b]", "11 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "push 1 byte"}, // flags: P2SH,STRICTENC, expected: OK
    {"[417a]", "'Az' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[417a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a7a]", "'Azzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "push 75 bytes"}, // flags: P2SH,STRICTENC, expected: OK
    {"[1.07]", "7 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[2.08]", "8 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[4.09]", "9 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"pushdata1 0x00", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"pushdata2 0x0000", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"pushdata4 0x00000000", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0x4f 1000 add", "999 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0x50 endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "0x50 is reserved (ok if not executed)"}, // flags: P2SH,STRICTENC, expected: OK
    {"0x51", "0x5f add 0x60 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "0x51 through 0x60 push 1 through 16 onto stack"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if ver else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "VER non-functional (ok if not executed)"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if reserved reserved1 reserved2 reserved3 reserved4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "RESERVED ok in un-executed IF"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "dup if endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "dup if else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if 1 else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "if if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "if if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "notif if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "notif if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0 else 1 else 0 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Multiple ELSE's are valid and executed inverts on each ELSE encountered"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if 1 else 0 else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if else 0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if 1 else 0 else 1 endif add 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'' 1", "if sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "notif 0 else 1 else 0 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Multiple ELSE's are valid and execution inverts on each ELSE encountered"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "notif 1 else 0 else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "notif else 0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "notif 1 else 0 else 1 endif add 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'' 0", "notif sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 else else sha1 endif [68ca4fec736264c13b859bac43d5173df6871682] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 1 if return else return else return endif else 1 if 1 else return else 1 endif else return endif add 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Nested ELSE ELSE"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "notif 0 notif return else return else return endif else 0 notif 1 else return else 1 endif else return endif add 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if return endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "RETURN only works if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "verify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [0100000000]", "verify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "values >4 bytes can be cast to boolean"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [80]", "if 0 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "negative 0 is false"}, // flags: P2SH,STRICTENC, expected: OK
    {"10 0 11 toaltstack drop fromaltstack", "add 21 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'gavin_was_here' toaltstack 11 fromaltstack", "'gavin_was_here' equalverify 11 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 ifdup", "depth 1 equalverify 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 ifdup", "depth 2 equalverify 1 equalverify 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[0100000000] ifdup", "depth 2 equalverify [0100000000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "IFDUP dups non ints"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 drop", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "dup 1 add 1 equalverify 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "over depth 3 equalverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "0 pick 20 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "1 pick 21 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "2 pick 22 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "0 roll 20 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "1 roll 21 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "2 roll 22 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "rot 22 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "rot drop 20 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "rot drop drop 21 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "rot rot 21 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"22 21 20", "rot rot rot 20 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 24 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot drop 25 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2drop 20 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2drop drop 21 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2drop 2drop 22 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2drop 2drop drop 23 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2rot 22 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"25 24 23 22 21 20", "2rot 2rot 2rot 20 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "swap 1 equalverify 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "tuck depth 3 equalverify swap 2drop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"13 14", "2dup rot equalverify equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0 1 2", "3dup depth 7 equalverify add add 3 equalverify 2drop 0 equalverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2 3 5", "2over add add 8 equalverify add add 6 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 3 5 7", "2swap add 4 equalverify add 12 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "size 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "size 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"127", "size 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"128", "size 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"32767", "size 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"32768", "size 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"8388607", "size 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"8388608", "size 4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647", "size 4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483648", "size 5 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""} // flags: P2SH,STRICTENC, expected: OK
};

/**
 * Script test chunk 1 (tests 100 to 199)
 */
bchn_script_test_list const script_tests_from_json_1{
    {"549755813887", "size 5 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"549755813888", "size 6 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"9223372036854775807", "size 8 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "size 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"3", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"126", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"127", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"128", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"129", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"130", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"255", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32766", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32767", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32768", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32769", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32770", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"65535", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388606", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388607", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388608", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388609", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388610", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"16777215", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483646", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483648", "size 5 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483649", "size 5 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"549755813887", "size 5 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"549755813888", "size 6 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"140737488355327", "size 6 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"140737488355328", "size 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"36028797018963967", "size 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"36028797018963968", "size 8 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807", "size 8 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1", "size 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-127", "size 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-128", "size 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-32767", "size 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-32768", "size 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-8388607", "size 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-8388608", "size 4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647", "size 4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483648", "size 5 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-549755813887", "size 5 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-549755813888", "size 6 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-9223372036854775807", "size 8 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-127", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-125", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-126", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-127", "size 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-128", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-129", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-255", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32765", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32766", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32767", "size 2 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32768", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32769", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-65535", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388605", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388606", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388607", "size 3 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388608", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388609", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483645", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483646", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483647", "size 4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648", "size 5 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-549755813887", "size 5 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-549755813888", "size 6 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-140737488355327", "size 6 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-140737488355328", "size 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-36028797018963967", "size 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-36028797018963968", "size 8 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775806", "size 8 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807", "size 8 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "size 26 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"42", "size 1 equalverify 42 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SIZE does not consume argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"2 -2 add", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -2147483647 add", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"9223372036854775807 -9223372036854775807 add", "0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1 -1 add", "-2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1 add", "2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 add1", "2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"111 sub1", "110 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"111 1 add 12 sub", "100 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 abs", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"16 abs", "16 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-16 abs", "-16 negate equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 not", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 not", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 not", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0notequal", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0notequal", "1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"111 0notequal", "1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""} // flags: P2SH,STRICTENC, expected: OK
};

/**
 * Script test chunk 2 (tests 200 to 299)
 */
bchn_script_test_list const script_tests_from_json_2{
    {"-111 0notequal", "1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1 booland", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 booland", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 booland", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0 booland", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"16 17 booland", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1 boolor", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 boolor", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 boolor", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0 boolor", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[80]", "dup boolor", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "negative-0 negative-0 BOOLOR"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[00]", "dup boolor", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, " non-minimal-0  non-minimal-0 BOOLOR"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[81]", "dup boolor", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "-1 -1 BOOLOR"}, // flags: P2SH,STRICTENC, expected: OK
    {"[80]", "dup booland", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "negative-0 negative-0 BOOLAND"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[00]", "dup booland", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, " non-minimal-0  non-minimal-0 BOOLAND"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[81]", "dup booland", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "-1 -1 BOOLAND"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00]", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "non-minimal-0 NOT"}, // flags: P2SH,STRICTENC, expected: OK
    {"[80]", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "negative-0 NOT"}, // flags: P2SH,STRICTENC, expected: OK
    {"[81]", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "negative 1 NOT"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[80] 0", "numequal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "-0 0 NUMEQUAL"}, // flags: P2SH, expected: OK
    {"[00] 0", "numequal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "non-minimal-0 0 NUMEQUAL"}, // flags: P2SH, expected: OK
    {"[0000] 0", "numequal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "non-minimal-0 0 NUMEQUAL"}, // flags: P2SH, expected: OK
    {"16 17 boolor", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10 1 add", "numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10 1 add", "numequalverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10 1 add", "numnotequal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"111 10 1 add", "numnotequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10", "lessthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"4 4", "lessthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"10 11", "lessthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 11", "lessthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 -10", "lessthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10", "greaterthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"4 4", "greaterthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"10 11", "greaterthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 11", "greaterthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 -10", "greaterthan not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10", "lessthanorequal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"4 4", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"10 11", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 11", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 -10", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 10", "greaterthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"4 4", "greaterthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"10 11", "greaterthanorequal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 11", "greaterthanorequal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-11 -10", "greaterthanorequal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 min", "0 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 min", "0 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0 min", "-1 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -2147483647 min", "-2147483647 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -9223372036854775807 min", "-9223372036854775807 numequal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647 0 max", "2147483647 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"9223372036854775807 0 max", "9223372036854775807 numequal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"0 100 max", "100 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-100 0 max", "0 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -2147483647 max", "0 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -9223372036854775807 max", "0 numequal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"0 0 1", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 1", "within not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -2147483647 2147483647", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 -9223372036854775807 9223372036854775807", "within", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1 -100 100", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"11 -100 100", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 -100 100", "within not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -100 100", "within not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-9223372036854775807 -100 100", "within not", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 -100 100", "within not", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647 2147483647 sub", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 dup add", "4294967294 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ">32 bit EQUAL is valid"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 negate dup add", "-4294967294 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"9223372036854775807 9223372036854775807 sub", "0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    // {"9223372036854775807 dup add", "4294967294 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_add, ">64-bit is not valid"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    // {"9223372036854775807 negate dup add", "-4294967294 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_add, "<-2^63 is not valid"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    {"''", "ripemd160 [9c1185a5c5e9fc54612808977ee8f548b2258d31] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'a'", "ripemd160 [0bdc9d2d256b3ee9daae347be6f4dc835a467ffe] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "ripemd160 [f71c27109c692c1b56bbdceb5b9d2865b3708dbc] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "sha1 [da39a3ee5e6b4b0d3255bfef95601890afd80709] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'a'", "sha1 [86f7e437faa5a7fce15d1ddcb9eaeaea377667b8] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "sha1 [32d10c7b8cf96570ca04ce37f2a19d84240d3a89] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "sha256 [e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'a'", "sha256 [ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "sha256 [71c480df93d6ae2f1efad1447c66c9525e316218cf51fc8d9ed832f2daf18b73] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "dup hash160 swap sha256 ripemd160 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "dup hash256 swap sha256 sha256 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "nop hash160 [b472a266d0bd89c13706a4132ccfb16f7c3b9fcb] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'a'", "hash160 nop [994355199e516ff76c4fa4aab39337b9d84cf12b] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "hash160 [1.c286a1af0947f58d1ad787385b1c2c4a976f9e71] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"''", "hash256 [5df6e0e2761359d30a8275058e299fcc0381534545f55cf43e41983f5d4c9456] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'a'", "hash256 [bf5d3affb73efd2ec6c36ad3112dd933efed63c4e1cbffcfa88e2759c144f2d8] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'abcdefghijklmnopqrstuvwxyz'", "hash256 [1.ca139bc10c2f660da42666f72e89a225936fc60f193c161124a672050c434671] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 1 equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, " [KNUTH_OVERRIDE: FORK: P2SH]"}, // flags: P2SH,STRICTENC, expected: OK
    // {"'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'NOP_1_to_10' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Discourage NOPx flag allows OP_NOP"}, // flags: P2SH,STRICTENC,DISCOURAGE_UPGRADABLE_NOPS, expected: OK
    {"0", "if nop10 endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Discouraged NOPs are allowed if not executed"}, // flags: P2SH,STRICTENC,DISCOURAGE_UPGRADABLE_NOPS, expected: OK
    {"0", "if checkdatasig else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if checkdatasigverify else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if reversebytes else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xbd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "available codepoint, invalid if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xbe else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""} // flags: P2SH,STRICTENC, expected: OK
};

/**
 * Script test chunk 3 (tests 300 to 399)
 */
bchn_script_test_list const script_tests_from_json_3{
    {"0", "if 0xbf else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xef else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "TOKEN_PREFIX; invalid if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if inputindex else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "opcodes for native introspection (not activated here)"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if activebytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if txversion else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if txinputcount else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if txoutputcount else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if txlocktime else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if utxovalue else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if utxobytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if outpointtxhash else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if outpointindex else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if inputbytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if inputsequencenumber else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if outputvalue else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if outputbytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if reserved3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "OP_RESERVED3, invalid if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if reserved4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "OP_RESERVED4, invalid if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xd6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "opcodes >= FIRST_UNDEFINED_OP_VALUE invalid if executed"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xd7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xd8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xd9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xda else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xdb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xdc else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xdd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xde else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xdf else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe1 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe2 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe5 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xe9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xea else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xeb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xec else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xed else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xee else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xef else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf1 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf2 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf5 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xf9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xfa else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xfb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xfc else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xfd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xfe else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0xff else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "520 byte push"}, // flags: P2SH,STRICTENC, expected: OK
    // {"1", "0x616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "201 opcodes executed. 0x61 is NOP"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2 3 4 5 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f", "1 2 3 4 5 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "1,000 stack size (0x6f is 3DUP) [KNUTH_OVERRIDE: EVAL_FALSE]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 toaltstack 2 toaltstack 3 4 5 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f", "1 2 3 4 5 6 7 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "1,000 stack size (altstack cleared between scriptSig/scriptPubKey) [KNUTH_OVERRIDE: EVAL_FALSE]"}, // flags: P2SH,STRICTENC, expected: OK
    {"'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f", "'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f 2dup 0x616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "Max-size (10,000-byte), max-push(520 bytes), max-opcodes(201), max stack size(1,000 items). 0x6f is 3DUP, 0x61 is NOP [KNUTH_OVERRIDE: EVAL_FALSE]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "if 0x5050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050505050 endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ">201 opcodes, but RESERVED (0x50) doesn't count towards opcode limit."}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "[01] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "The following is useful for checking implementations of BN_bn2mpi"}, // flags: P2SH,STRICTENC, expected: OK
    {"127", "[7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"128", "[8000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Leave room for the sign bit"}, // flags: P2SH,STRICTENC, expected: OK
    {"32767", "[FF7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"32768", "[008000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"8388607", "[FFFF7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"8388608", "[00008000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647", "[FFFFFF7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483648", "[0000008000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"549755813887", "[FFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"549755813888", "[000000008000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"9223372036854775807", "[FFFFFFFFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "[01] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "The following is useful for checking implementations of BN_bn2mpi"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2", "[02] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"3", "[03] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"126", "[7E] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"127", "[7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"128", "[8000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Leave room for the sign bit"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"129", "[8100] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"130", "[8200] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"255", "[FF00] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32766", "[FE7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32767", "[FF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32768", "[008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32769", "[018000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"32770", "[028000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"65535", "[FFFF00] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388606", "[FEFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388607", "[FFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388608", "[00008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388609", "[01008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"8388610", "[02008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483646", "[FEFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""} // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
};

/**
 * Script test chunk 4 (tests 400 to 499)
 */
bchn_script_test_list const script_tests_from_json_4{
    {"2147483647", "[FFFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483648", "[0000008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483649", "[0100008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"549755813887", "[FFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"549755813888", "[000000008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"140737488355327", "[FFFFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"140737488355328", "[00000000008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"36028797018963967", "[FFFFFFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"36028797018963968", "[0000000000008000] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807", "[FFFFFFFFFFFFFF7F] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1", "[81] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Numbers are little-endian with the MSB being a sign bit"}, // flags: P2SH,STRICTENC, expected: OK
    {"-127", "[FF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-128", "[8080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-32767", "[FFFF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-32768", "[008080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-8388607", "[FFFFFF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-8388608", "[00008080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647", "[FFFFFFFF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483648", "[0000008080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-4294967295", "[FFFFFFFF80] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-549755813887", "[FFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-549755813888", "[000000008080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-9223372036854775807", "[FFFFFFFFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1", "[81] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Numbers are little-endian with the MSB being a sign bit"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2", "[82] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-125", "[FD] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-126", "[FE] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-127", "[FF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-128", "[8080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-129", "[8180] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-255", "[FF80] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32765", "[FDFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32766", "[FEFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32767", "[FFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32768", "[008080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-32769", "[018080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-65535", "[FFFF80] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388605", "[FDFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388606", "[FEFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388607", "[FFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388608", "[00008080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-8388609", "[01008080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483645", "[FDFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483646", "[FEFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483647", "[FFFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648", "[0000008080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-4294967295", "[FFFFFFFF80] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-549755813887", "[FFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-549755813888", "[000000008080] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-140737488355327", "[FFFFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-36028797018963967", "[FFFFFFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807", "[FFFFFFFFFFFFFFFF] equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647", "add1 2147483648 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "We can do math on 4-byte integers, and compare 5-byte ones"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647", "add1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647", "add1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647", "add1 2147483648 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 8-byte integers"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483647", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    // {"9223372036854775807", "add1 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_add, "64-bit integer overflow detected"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    // {"9223372036854775807", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_add, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    {"-9223372036854775807", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1", "[0100] equal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Not the same byte array..."}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "[0100] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "... but they are numerically equal"}, // flags: P2SH,STRICTENC, expected: OK
    {"11", "[1.0b0000] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "[80] equal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "[80] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Zero numerically equals negative zero"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "[0080] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[000080]", "[00000080] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[100080]", "[10000080] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[100000]", "[10000000] numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "The following tests check the if(stack.size() < N) tests in each opcode"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "if 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "They are here to catch copy-and-paste errors"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "notif 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Most of them are duplicated elsewhere,"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "verify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "but, hey, more is always better, right?"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "toaltstack 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "toaltstack fromaltstack", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "2drop 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "2dup", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0 1", "3dup", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 0 0", "2over", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 0 0 0 0", "2rot", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1 0 0", "2swap", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "ifdup", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "depth 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "drop 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "dup", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "over", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 0 0 3", "pick", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "pick", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 0 0 3", "roll", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "roll", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0 0", "rot", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "swap", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "tuck", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "size", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "equalverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0 1", "equal equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "OP_0 and bools must have identical byte representations"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "add1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""} // flags: P2SH,STRICTENC, expected: OK
};

/**
 * Script test chunk 5 (tests 500 to 599)
 */
bchn_script_test_list const script_tests_from_json_5{
    {"2", "sub1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1", "negate", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1", "abs", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1", "0notequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "add", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "sub", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 -1", "booland", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0", "boolor", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "numequalverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0", "numnotequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0", "lessthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "greaterthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "greaterthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 0", "min", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "max", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 -1 0", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "ripemd160", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "sha1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "sha256", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "hash160", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "hash256", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "codeseparator 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "checklocktimeverify 1", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, " [KNUTH_OVERRIDE: FORK: P2SH]"}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "checksequenceverify 1", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, " [KNUTH_OVERRIDE: FORK: P2SH]"}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop4 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop5 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop6 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop7 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop8 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop9 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"nop", "nop10 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 0 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKMULTISIG is allowed to have zero keys and/or sigs"}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 0 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 0 1 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Zero sigs means no sigs are checked"}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 0 1 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 2 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Test from up to 20 pubkeys, all not checked"}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 3 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 4 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 5 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 6 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 7 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 8 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 9 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 10 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 11 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 12 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 13 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 14 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 15 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 16 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 17 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 18 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 19 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig verify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 1 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 2 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 3 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 4 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 5 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 6 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 7 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 8 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 9 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 10 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 11 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 12 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 13 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 14 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 15 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 16 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 17 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 18 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 19 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "0 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "nOpCount is incremented by the number of keys evaluated in addition to the usual one op per op. In this case we have zero keys, so we can execute 201 CHECKMULTISIGS"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify 0 0 0 checkmultisigverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"", "nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Even though there are no signatures being checked nOpCount is incremented by the number of keys."}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"0 [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Very basic P2SH [KNUTH_OVERRIDE: SIG_REPLACED]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[1.] [0.51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, " [KNUTH_OVERRIDE: SIG_REPLACED]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242]", "[2.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Basic PUSH signedness check"}, // flags: P2SH,STRICTENC, expected: OK
    {"[1.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242]", "[2.42424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242424242] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Basic PUSHDATA1 signedness check"}, // flags: P2SH,STRICTENC, expected: OK
    {"[1.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111]", "[111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111] equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "PUSHDATA1 of 75 bytes equals direct push of it"}, // flags: NONE, expected: OK
    {"[2.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111]", "[1.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111] equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "PUSHDATA2 of 255 bytes equals PUSHDATA1 of it"}, // flags: NONE, expected: OK
    {"0x00", "size 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Basic OP_0 execution"}, // flags: P2SH,STRICTENC, expected: OK
    {"[81]", "0x4f equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP1_NEGATE pushes 0x81"}, // flags: NONE, expected: OK
    {"[01]", "0x51 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_1  pushes 0x01"}, // flags: NONE, expected: OK
    {"[02]", "0x52 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_2  pushes 0x02"}, // flags: NONE, expected: OK
    {"[03]", "0x53 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_3  pushes 0x03"}, // flags: NONE, expected: OK
    {"[04]", "0x54 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_4  pushes 0x04"}, // flags: NONE, expected: OK
    {"[05]", "0x55 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_5  pushes 0x05"}, // flags: NONE, expected: OK
    {"[06]", "0x56 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_6  pushes 0x06"}, // flags: NONE, expected: OK
    {"[07]", "0x57 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_7  pushes 0x07"}, // flags: NONE, expected: OK
    {"[08]", "0x58 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_8  pushes 0x08"}, // flags: NONE, expected: OK
    {"[09]", "0x59 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_9  pushes 0x09"}, // flags: NONE, expected: OK
    {"[0a]", "0x5a equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_10 pushes 0x0a"} // flags: NONE, expected: OK
};

/**
 * Script test chunk 6 (tests 600 to 699)
 */
bchn_script_test_list const script_tests_from_json_6{
    {"[0b]", "0x5b equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_11 pushes 0x0b"}, // flags: NONE, expected: OK
    {"[0c]", "0x5c equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_12 pushes 0x0c"}, // flags: NONE, expected: OK
    {"[0d]", "0x5d equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_13 pushes 0x0d"}, // flags: NONE, expected: OK
    {"[0e]", "0x5e equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_14 pushes 0x0e"}, // flags: NONE, expected: OK
    {"[0f]", "0x5f equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_15 pushes 0x0f"}, // flags: NONE, expected: OK
    {"[10]", "0x60 equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "OP_16 pushes 0x10"}, // flags: NONE, expected: OK
    {"[8000]", "128 numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "0x8000 equals 128"}, // flags: NONE, expected: OK
    {"[00]", "0 numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "0x00 numequals 0"}, // flags: NONE, expected: OK
    {"[80]", "0 numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "0x80 (negative zero) numequals 0"}, // flags: NONE, expected: OK
    {"[0080]", "0 numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "0x0080 numequals 0"}, // flags: NONE, expected: OK
    {"[0500]", "5 numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "0x0500 numequals 5"}, // flags: NONE, expected: OK
    {"[ff7f80]", "[ffff] numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[ff7f00]", "[ff7f] numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[ffff7f80]", "[ffffff] numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[ffff7f00]", "[ffff7f] numequal", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 if pushdata1 0x00 endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA1 ignored"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if pushdata1 0x00 endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA1 ignored (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if pushdata2 0x0000 endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA2 ignored"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if pushdata2 0x0000 endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA2 ignored (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if pushdata4 0x00000000 endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA4 ignored"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if pushdata4 0x00000000 endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal PUSHDATA4 ignored (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [81] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "1NEGATE equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [81] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "1NEGATE equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [01] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_1  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [01] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_1  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [02] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_2  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [02] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_2  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [03] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_3  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [03] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_3  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [04] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_4  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [04] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_4  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [05] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_5  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [05] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_5  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [06] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_6  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [06] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_6  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [07] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_7  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [07] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_7  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [08] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_8  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [08] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_8  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [09] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_9  equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [09] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_9  equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0a] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_10 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0a] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_10 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0b] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_11 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0b] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_11 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0c] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_12 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0c] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_12 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0d] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_13 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0d] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_13 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0e] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_14 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0e] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_14 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [0f] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_15 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [0f] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_15 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"0 if [10] endif 1", "", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_16 equiv"}, // flags: MINIMALDATA, expected: OK
    {"", "0 if [10] endif 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_16 equiv (scriptPubKey)"}, // flags: MINIMALDATA, expected: OK
    {"[00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[80]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[80] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0180]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0180] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0100]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0100] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0200]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0200] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0300]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0300] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0400]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0400] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0500]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0500] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0600]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0600] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0700]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0700] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0800]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0800] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0900]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0900] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0a00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0a00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0b00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0b00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0c00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0c00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0d00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0d00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0e00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0e00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[0f00]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[0f00] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"[1000]", "1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"", "[1000] 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, ""}, // flags: MINIMALDATA, expected: OK
    {"1 [0000]", "pick drop", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"1 [0000]", "roll drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "add1 drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "sub1 drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "negate drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "abs drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "not drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""} // flags: NONE, expected: OK
};

/**
 * Script test chunk 7 (tests 700 to 799)
 */
bchn_script_test_list const script_tests_from_json_7{
    {"[0000]", "0notequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "add drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "add drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "sub drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "sub drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0100]", "div drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: NONE, expected: OK
    {"[0000] 1", "div drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: NONE, expected: OK
    {"0 [0100]", "mod drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: NONE, expected: OK
    {"[0000] 1", "mod drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: NONE, expected: OK
    {"0 [0000]", "booland drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "booland drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "boolor drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "boolor drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "numequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 1", "numequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "numequalverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "numequalverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "numnotequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "numnotequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "lessthan drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "lessthan drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "greaterthan drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "greaterthan drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "lessthanorequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "lessthanorequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "greaterthanorequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "greaterthanorequal drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "min drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "min drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000]", "max drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0", "max drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] 0 0", "within drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000] 0", "within drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 0 [0000]", "within drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 0 [0000]", "checkmultisig drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000] 0", "checkmultisig drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000] 0 1", "checkmultisig drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 0 [0000]", "checkmultisigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [0000] 0", "checkmultisigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000] [0100]", "split 2drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[11111111111111111111111111111111111111110000] [1400]", "num2bin drop 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000]", "checklocktimeverify drop 1", kth::domain::machine::rule_fork::bip65_rule, kth::error::unsatisfied_locktime, "fails because final sequence, not from nonminimal encoding"}, // flags: CHECKLOCKTIMEVERIFY, expected: UNSATISFIED_LOCKTIME
    {"[0000]", "checksequenceverify drop 1", kth::domain::machine::rule_fork::bip112_rule, kth::error::unsatisfied_locktime, "fails because tx version < 2, not from nonminimal encoding"}, // flags: CHECKSEQUENCEVERIFY, expected: UNSATISFIED_LOCKTIME
    {"0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: STRICTENC, expected: OK
    {"0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: STRICTENC, expected: OK
    {"0", "[BEEF] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "Even with a null signature, the public key has to be correctly encoded."}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, ""}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Invalid public keys are fine, as long as correctly encoded."}, // flags: STRICTENC, expected: OK
    {"0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: STRICTENC, expected: OK
    {"0", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, ""}, // flags: STRICTENC,NULLFAIL, expected: OK
    {"0 0", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, ""}, // flags: STRICTENC,NULLFAIL, expected: OK
    {"0", "[BEEF] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, ""}, // flags: STRICTENC,NULLFAIL, expected: PUBKEYTYPE
    {"0 0", "1 [BEEF] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, ""}, // flags: STRICTENC,NULLFAIL, expected: PUBKEYTYPE
    {"0", "[020000000000000000000000000000000000000000000000000000000000000000] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, ""}, // flags: STRICTENC,NULLFAIL, expected: OK
    {"0 0", "1 [020000000000000000000000000000000000000000000000000000000000000000] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, ""}, // flags: STRICTENC,NULLFAIL, expected: OK
    {"0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501] [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 0 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "2-of-2 CHECKMULTISIG NOT with the second pubkey invalid, and both signatures validly encoded. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid pubkey."}, // flags: STRICTENC, expected: OK
    {"0 0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "2-of-2 CHECKMULTISIG NOT with both pubkeys valid, but second signature invalid. Valid pubkey fails, and CHECKMULTISIG exits early, prior to evaluation of second invalid signature."}, // flags: STRICTENC, expected: OK
    {"0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "Nulled 2-of-2 with valid pubkeys"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: OK
    {"0 0 0", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [BEEF] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, "Nulled 2-of-2 with top pubkey invalidly encoded"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE
    {"0 0 0", "2 [BEEF] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "Nulled 2-of-2 with top pubkey validly encoded, but another pubkey is invalidly encoded"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: OK
    {"[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Overly long signature is correctly encoded"}, // flags: NONE, expected: OK
    {"[30220220000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Missing S is correctly encoded"}, // flags: NONE, expected: OK
    {"[3024021077777777777777777777777777777777020a7777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "S with invalid S length is correctly encoded"}, // flags: NONE, expected: OK
    {"[302403107777777777777777777777777777777702107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Non-integer R is correctly encoded"}, // flags: NONE, expected: OK
    {"[302402107777777777777777777777777777777703107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Non-integer S is correctly encoded"}, // flags: NONE, expected: OK
    {"[3014020002107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Zero-length R is correctly encoded"}, // flags: NONE, expected: OK
    {"[3014021077777777777777777777777777777777020001]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Zero-length S is correctly encoded for DERSIG"}, // flags: NONE, expected: OK
    {"[302402107777777777777777777777777777777702108777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Negative S is correctly encoded"}, // flags: NONE, expected: OK
    {"2147483648", "checksequenceverify", kth::domain::machine::rule_fork::bip112_rule, kth::error::success, "CSV passes if stack top bit 1 << 31 is set"}, // flags: CHECKSEQUENCEVERIFY, expected: OK
    {"", "depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "Test the test: we should have an empty stack after scriptSig evaluation"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "and multiple spaces should not change that."}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "nop depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "nop depth", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"depth", "", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1", "if 0x50 endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "0x50 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0x52", "0x5f add 0x60 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "0x51 through 0x60 push 1 through 16 onto stack"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1", "if ver else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "VER non-functional"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0", "if verif else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "VERIF illegal everywhere [KNUTH_OVERRIDE: DISABLED_OPCODE]"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0", "if else 1 else verif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "VERIF illegal everywhere [KNUTH_OVERRIDE: DISABLED_OPCODE]"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0", "if vernotif else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "VERNOTIF illegal everywhere [KNUTH_OVERRIDE: DISABLED_OPCODE]"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0", "if else 1 else vernotif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "VERNOTIF illegal everywhere [KNUTH_OVERRIDE: DISABLED_OPCODE]"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1 if", "1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "IF/ENDIF can't span scriptSig/scriptPubKey"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1 if 0 endif", "1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, " [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1 else 0 endif", "1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_else, " [KNUTH_OVERRIDE: KTH_OP_ELSE]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"0 notif", "123", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, ""}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"0", "dup if endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0", "if 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0", "dup if else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0", "if 1 else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0", "notif else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 1", "if if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 0", "if if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""} // flags: P2SH,STRICTENC, expected: EVAL_FALSE
};

/**
 * Script test chunk 8 (tests 800 to 899)
 */
bchn_script_test_list const script_tests_from_json_8{
    {"1 0", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 1", "if if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 0", "notif if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 1", "notif if 1 else 0 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1 1", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0 0", "notif if 1 else 0 endif else if 0 else 1 endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1", "if return else else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_return, "Multiple ELSEs"}, // flags: P2SH,STRICTENC, expected: OP_RETURN
    {"1", "if 1 else else return endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_return, ""}, // flags: P2SH,STRICTENC, expected: OP_RETURN
    {"1", "endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, "Malformed IF/ELSE/ENDIF sequence [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_else, " [KNUTH_OVERRIDE: KTH_OP_ELSE]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "endif else", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, " [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "endif else if", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, " [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "if else endif else", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_else, " [KNUTH_OVERRIDE: KTH_OP_ELSE]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "if else endif else endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_else, " [KNUTH_OVERRIDE: KTH_OP_ELSE]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "if endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, " [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "if else else endif endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_endif, " [KNUTH_OVERRIDE: KTH_OP_ENDIF]"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "return", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, " [KNUTH_OVERRIDE: KTH_INVALID_SCRIPT]"}, // flags: P2SH,STRICTENC, expected: OP_RETURN
    {"1", "dup if return endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_return, ""}, // flags: P2SH,STRICTENC, expected: OP_RETURN
    {"1", "return 'data'", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "canonical prunable txout format [KNUTH_OVERRIDE: KTH_INVALID_SCRIPT]"}, // flags: P2SH,STRICTENC, expected: OP_RETURN
    {"0 if", "return endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "still prunable because IF/ENDIF can't span scriptSig/scriptPubKey"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"0", "verify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: VERIFY
    {"1", "verify", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1", "verify 0", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1 toaltstack", "fromaltstack 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_from_alt_stack, "alt stack not shared between sig/pubkey"}, // flags: P2SH,STRICTENC, expected: INVALID_ALTSTACK_OPERATION
    {"ifdup", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_if_dup, " [KNUTH_OVERRIDE: KTH_OP_IF_DUP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"drop", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_drop, " [KNUTH_OVERRIDE: KTH_OP_DROP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"dup", "depth 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup, " [KNUTH_OVERRIDE: KTH_OP_DUP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "dup 1 add 2 equalverify 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_nip, " [KNUTH_OVERRIDE: KTH_OP_NIP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "1 nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_nip, " [KNUTH_OVERRIDE: KTH_OP_NIP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "1 0 nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "over 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_over, " [KNUTH_OVERRIDE: KTH_OP_OVER]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "over", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_over, " [KNUTH_OVERRIDE: KTH_OP_OVER]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "over depth 3 equalverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"19 20 21", "pick 19 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_pick, " [KNUTH_OVERRIDE: KTH_OP_PICK]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "0 pick", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_pick, " [KNUTH_OVERRIDE: KTH_OP_PICK]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "-1 pick", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_pick, " [KNUTH_OVERRIDE: KTH_OP_PICK]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"19 20 21", "0 pick 20 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"19 20 21", "1 pick 21 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"19 20 21", "2 pick 22 equalverify depth 3 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"nop", "0 roll", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_roll, " [KNUTH_OVERRIDE: KTH_OP_ROLL]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "-1 roll", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_roll, " [KNUTH_OVERRIDE: KTH_OP_ROLL]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"19 20 21", "0 roll 20 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"19 20 21", "1 roll 21 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"19 20 21", "2 roll 22 equalverify depth 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"nop", "rot 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_rot, " [KNUTH_OVERRIDE: KTH_OP_ROT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "1 rot 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_rot, " [KNUTH_OVERRIDE: KTH_OP_ROT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "1 2 rot 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_rot, " [KNUTH_OVERRIDE: KTH_OP_ROT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "0 1 2 rot", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "swap 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_swap, " [KNUTH_OVERRIDE: KTH_OP_SWAP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "swap 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_swap, " [KNUTH_OVERRIDE: KTH_OP_SWAP]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "swap 1 equalverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal_verify_failed, " [KNUTH_OVERRIDE: KTH_OP_EQUAL_VERIFY_FAILED]"}, // flags: P2SH,STRICTENC, expected: EQUALVERIFY
    {"nop", "tuck 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_tuck, " [KNUTH_OVERRIDE: KTH_OP_TUCK]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "tuck 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_tuck, " [KNUTH_OVERRIDE: KTH_OP_TUCK]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 0", "tuck depth 3 equalverify swap 2drop", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"nop", "2dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup2, " [KNUTH_OVERRIDE: KTH_OP_DUP2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "2dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup2, " [KNUTH_OVERRIDE: KTH_OP_DUP2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "3dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup3, " [KNUTH_OVERRIDE: KTH_OP_DUP3]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "3dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup3, " [KNUTH_OVERRIDE: KTH_OP_DUP3]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 2", "3dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_dup3, " [KNUTH_OVERRIDE: KTH_OP_DUP3]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "2over 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_over2, " [KNUTH_OVERRIDE: KTH_OP_OVER2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "2 3 2over 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_over2, " [KNUTH_OVERRIDE: KTH_OP_OVER2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "2swap 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_swap2, " [KNUTH_OVERRIDE: KTH_OP_SWAP2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "2 3 2swap 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_swap2, " [KNUTH_OVERRIDE: KTH_OP_SWAP2]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "cat", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_cat, "CAT, empty stack [KNUTH_OVERRIDE: KTH_OP_CAT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"'a'", "cat", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_cat, "CAT, one parameter [KNUTH_OVERRIDE: KTH_OP_CAT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"'abcd' 'efgh'", "cat 'abcdefgh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'' ''", "cat '' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT two empty strings"}, // flags: P2SH,STRICTENC, expected: OK
    {"'abc' ''", "cat 'abc' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'' 'def'", "cat 'def' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxh' 'ataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT, maximum length"}, // flags: P2SH,STRICTENC, expected: OK
    {"'' 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT, maximum length with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' ''", "cat 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CAT, maximum length with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'a' 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "cat", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_push_data_size, "CAT oversized result"}, // flags: P2SH,STRICTENC, expected: PUSH_SIZE
    {"", "split", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_split, "SPLIT, empty stack [KNUTH_OVERRIDE: KTH_OP_SPLIT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"'a'", "split", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_split, "SPLIT, one parameter [KNUTH_OVERRIDE: KTH_OP_SPLIT]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"'abcdef' 3", "split 'def' equalverify 'abc' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"'' 0", "split '' equalverify '' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'abc' 0", "split 'abc' equalverify '' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, boundary condition"}, // flags: P2SH,STRICTENC, expected: OK
    {"'abc' 3", "split '' equalverify 'abc' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, boundary condition"}, // flags: P2SH,STRICTENC, expected: OK
    {"'abc' 4", "split", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_split, "SPLIT, out of bounds [KNUTH_OVERRIDE: KTH_OP_SPLIT]"}, // flags: P2SH,STRICTENC, expected: SPLIT_RANGE
    {"'abc' -1", "split", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_split, "SPLIT, out of bounds [KNUTH_OVERRIDE: KTH_OP_SPLIT]"}, // flags: P2SH,STRICTENC, expected: SPLIT_RANGE
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "145 split 'ataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equalverify 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, maximum length"}, // flags: P2SH,STRICTENC, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "0 split 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equalverify '' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, maximum length with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "520 split '' equalverify 'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "SPLIT, maximum length with empty string"}, // flags: P2SH,STRICTENC, expected: OK
    {"", "reversebytes 0 equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::op_reverse_bytes, "REVERSEBYTES, empty stack [KNUTH_OVERRIDE: KTH_OP_REVERSE_BYTES]"}, // flags: P2SH, expected: INVALID_STACK_OPERATION
    {"0", "reversebytes 0 equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, empty data"}, // flags: P2SH, expected: OK
    {"[99]", "reversebytes [99] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 1 byte"}, // flags: P2SH, expected: OK
    {"[beef]", "reversebytes [efbe] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 2 bytes"}, // flags: P2SH, expected: OK
    {"[deada1]", "reversebytes [a1adde] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes"}, // flags: P2SH, expected: OK
    {"[deadbeef]", "reversebytes [efbeadde] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 4 bytes"}, // flags: P2SH, expected: OK
    {"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "reversebytes 'metsys_hsac_cinortcele_reep-ot-reep_A_:nioctiB' equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, whitepaper title"}, // flags: P2SH, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "reversebytes 'hhfanqyfcglncrriufjfvjgqcbbwrilaxeczpotsfzvmyrwlobpjtvtmhamvzoiwwjqazpyekvxpvsiiwuvertiqqiksmgkfplzamdvcelrrjsrvsxgojxngirwcoxhtmvtpzdjwzsuamlpudecziyiqkuukdbakbswcyelaksmudkgqccxpbobrkdpajsgogcuspzrptvrrsyuhrtxcftlpwlodgkavcqwofyxcwrsbovwojgznkiyijhohlyxevhqnqsibheuinifzybtbblzutjtsrotyqndybrncsdyrzlblxctejeyienvjeuslnzxomqzclruxceqispbntxngdpttfzzvqxshwlktvtkupouvjaugatahxgzzjidcauprbwdwakushoyhmogialwljyjrluudbkwjrkitxfsbtrvvyaesdosyzyxiysldhrybsxfrzpwwpubzncyzupdwoumfkfxnivdsryzlizjumarmlpqwfwqvngfegryrinviygnz' equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes"}, // flags: P2SH, expected: OK
    {"[123456]", "reversebytes [563412] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes equal"}, // flags: P2SH, expected: OK
    {"[020406080a0c]", "dup reversebytes reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes double reverse equal"}, // flags: P2SH, expected: OK
    {"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, whitepaper title double reverse equal"}, // flags: P2SH, expected: OK
    {"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "dup reversebytes reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes double reverse equal"}, // flags: P2SH, expected: OK
    {"[0102030201]", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 1"}, // flags: P2SH, expected: OK
    {"[7766554444556677]", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 2"}, // flags: P2SH, expected: OK
    {"'madam'", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 3"} // flags: P2SH, expected: OK
};

/**
 * Script test chunk 9 (tests 900 to 999)
 */
bchn_script_test_list const script_tests_from_json_9{
    {"'racecar'", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 4"}, // flags: P2SH, expected: OK
    {"'redrum_siris_murder'", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 5"}, // flags: P2SH, expected: OK
    {"'step_on_no_pets'", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 6"}, // flags: P2SH, expected: OK
    {"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 1"}, // flags: P2SH, expected: EVAL_FALSE
    {"[1234]", "dup reversebytes equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 2"}, // flags: P2SH, expected: EVAL_FALSE
    {"", "num2bin 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin, "NUM2BIN, empty stack [KNUTH_OVERRIDE: KTH_OP_NUM2BIN]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "num2bin 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin, "NUM2BIN, one parameter [KNUTH_OVERRIDE: KTH_OP_NUM2BIN]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 0", "num2bin 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, canonical argument "}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "num2bin [00] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, zero extend"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 7", "num2bin [00000000000000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, zero extend"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "num2bin 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, canonical argument "}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 1", "num2bin -42 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, canonical argument "}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 2", "num2bin [2a80] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, canonical argument "}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 10", "num2bin [2a000000000000000080] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN, large materialization"}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 520", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Pushing 520 bytes is ok"}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 521", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_size_exceeded, "Pushing 521 bytes is not [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_SIZE_EXCEEDED]"}, // flags: P2SH,STRICTENC, expected: PUSH_SIZE
    {"-42 -3", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_size_exceeded, "Negative size [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_SIZE_EXCEEDED]"}, // flags: P2SH,STRICTENC, expected: PUSH_SIZE
    {"[abcdef4280] 4", "num2bin [abcdefc2] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Item size reduction"}, // flags: P2SH,STRICTENC, expected: OK
    {"[abcdef] 2", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_impossible_encoding, "output too small [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_IMPOSSIBLE_ENCODING]"}, // flags: P2SH,STRICTENC, expected: IMPOSSIBLE_ENCODING
    {"[abcdef] 3", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[80] 0", "num2bin 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Negative zero"}, // flags: P2SH,STRICTENC, expected: OK
    {"[80] 3", "num2bin [000000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Negative zero, larger output"}, // flags: P2SH,STRICTENC, expected: OK
    {"[80] 1", "num2bin [00] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (1) requested"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00000080] 4", "num2bin [00000000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (4) requested"}, // flags: P2SH,STRICTENC, expected: OK
    {"[abcdef4243] 5", "num2bin [abcdef4243] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "NUM2BIN where len(a) > 4"}, // flags: P2SH,STRICTENC, expected: OK
    {"[abcdef4243] 6", "num2bin [abcdef424300] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "pads output properly"}, // flags: P2SH,STRICTENC, expected: OK
    {"[abcdef4243] 4", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_impossible_encoding, "output too small [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_IMPOSSIBLE_ENCODING]"}, // flags: P2SH,STRICTENC, expected: IMPOSSIBLE_ENCODING
    {"[0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat 520", "num2bin [0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "520 byte 1st operand"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0000000000] 5", "num2bin [0000000000] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "1st operand not minimally encoded"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0001000000] 3", "num2bin [000100] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "1st operand can shrink"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [050000]", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "2nd operand not minimally encoded"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [0500000000]", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_invalid_size, "2nd operand > 4 bytes [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_INVALID_SIZE]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"[abcdef42] [abcdef4243]", "num2bin", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num2bin_invalid_size, "2nd operand > 4 bytes [KNUTH_OVERRIDE: KTH_OP_NUM2BIN_INVALID_SIZE]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"", "bin2num 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_bin2num, "BIN2NUM, empty stack [KNUTH_OVERRIDE: KTH_OP_BIN2NUM]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "bin2num 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, canonical argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "bin2num 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, canonical argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"-42", "bin2num -42 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, canonical argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00]", "bin2num 0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, non-canonical argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"[ffffff7f]", "bin2num 2147483647 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, maximum size argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"[ffffffff]", "bin2num -2147483647 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, maximum size argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"[ffffffff00]", "bin2num 2147483647 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_bin2num_invalid_number_range, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"[ffffff7f80]", "bin2num -2147483647 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "BIN2NUM, non-canonical maximum size argument"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0100000000]", "bin2num 1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[FE00000000]", "bin2num 254 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[0500000080]", "bin2num [85] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: P2SH,STRICTENC, expected: OK
    {"[800000]", "bin2num 128 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Pad where MSB of number is set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[800080]", "bin2num -128 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Pad where MSB of number is set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[8000]", "bin2num 128 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Pad where MSB of number is set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[8080]", "bin2num -128 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Pad where MSB of number is set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0f0000]", "bin2num 15 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Don't pad where MSB of number is not set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0f0080]", "bin2num -15 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Don't pad where MSB of number is not set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0f00]", "bin2num 15 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Don't pad where MSB of number is not set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0f80]", "bin2num -15 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Don't pad where MSB of number is not set"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0100800000]", "bin2num 8388609 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Ensure significant zero bytes are retained"}, // flags: P2SH,STRICTENC, expected: OK
    {"[0100800080]", "bin2num -8388609 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Ensure significant zero bytes are retained"}, // flags: P2SH,STRICTENC, expected: OK
    {"[01000f0000]", "bin2num 983041 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Ensure significant zero bytes are retained"}, // flags: P2SH,STRICTENC, expected: OK
    {"[01000f0080]", "bin2num -983041 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Ensure significant zero bytes are retained"}, // flags: P2SH,STRICTENC, expected: OK
    {"[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_bin2num_invalid_number_range, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_bin2num_invalid_number_range, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"[ffffffffffffffff00]", "bin2num 9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_bin2num_invalid_number_range, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE]"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    {"[ffffffffffffff7f80]", "bin2num -9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "BIN2NUM, non-canonical maximum size argument"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"nop", "size 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_size, " [KNUTH_OVERRIDE: KTH_OP_SIZE]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"'abc'", "if invert else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "INVERT disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 0 if mul2 else 1 endif", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "2MUL disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 0 if div2 else 1 endif", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "2DIV disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 2 0 if mul else 1 endif", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "MUL disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 2 0 if lshift else 1 endif", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "LSHIFT disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 2 0 if rshift else 1 endif", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "RSHIFT disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"0 0", "and 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] [00]", "and [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [00]", "and [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] 1", "and [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "and 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "and 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_and, "AND, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "and 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_and, "AND, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "and 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_and, "AND, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OPERAND_SIZE
    {"[ab] [cd]", "and [89] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "AND, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "or 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "OR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] [00]", "or [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [00]", "or 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] 1", "or 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "or 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "or 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_or, "OR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "or 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_or, "OR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "or 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_or, "OR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OPERAND_SIZE
    {"[ab] [cd]", "or [ef] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 0", "xor 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] [00]", "xor [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 [00]", "xor 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"[00] 1", "xor 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "xor [00] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0", "xor 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_xor, "XOR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "xor 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_xor, "XOR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "xor 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_xor, "XOR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OP_XOR, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OPERAND_SIZE
    {"[ab] [cd]", "xor [66] equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 1", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 -1", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 1", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"} // flags: P2SH,STRICTENC, expected: OK
};

/**
 * Script test chunk 10 (tests 1000 to 1099)
 */
bchn_script_test_list const script_tests_from_json_10{
    {"-1 -1", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"28 21", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"12 -7", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-32 29", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-42 -27", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Round towards zero [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 123", "div 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"511 0", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div_by_zero, "DIV, divide by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV_BY_ZERO, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: DIV_BY_ZERO
    {"1 1", "div depth 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"2147483647 1", "div 2147483647 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 2147483647", "div 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 2147483647", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 1", "div -2147483647 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 2147483647", "div 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 2147483647", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -1", "div -2147483647 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 -2147483647", "div 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -2147483647", "div -1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 -1", "div 2147483647 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 -2147483647", "div 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 -2147483647", "div 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483648 1", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"1 2147483648", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"-2147483648 1", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"1 -2147483648", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648 1", "div", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_div, "We can do math on 5-byte integers on 64-bit mode [KNUTH_OVERRIDE: ERROR: KTH_OP_DIV, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 2147483648", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648 1", "div -2147483648 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 -2147483648", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 1", "div 9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 9223372036854775807", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 9223372036854775807", "div 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 1", "div -9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1 9223372036854775807", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 9223372036854775807", "div -1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 -1", "div -9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 -9223372036854775807", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 -9223372036854775807", "div -1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 -1", "div 9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-1 -9223372036854775807", "div 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 -9223372036854775807", "div 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 1", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 1", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 -1", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-1 -1", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"82 23", "mod 13 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"8 -3", "mod 2 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-71 13", "mod -6 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-110 -31", "mod -17 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"0 1", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, " [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1 0", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod_by_zero, "MOD, modulo by zero [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD_BY_ZERO, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: MOD_BY_ZERO
    {"1 1", "mod depth 1 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Stack depth correct [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"1", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "Not enough operands [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"2147483647 123", "mod 79 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"123 2147483647", "mod 123 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 2147483647", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 123", "mod -79 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-123 2147483647", "mod -123 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 2147483647", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -123", "mod 79 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"123 -2147483647", "mod 123 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483647 -2147483647", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 -123", "mod -79 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-123 -2147483647", "mod -123 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"-2147483647 -2147483647", "mod 0 equal", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::success, "Check boundary condition [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: OK
    {"2147483648 1", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"1 2147483648", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"-2147483648 1", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"1 -2147483648", "mod", kth::domain::machine::rule_fork::bch_pythagoras, kth::error::op_mod, "We cannot do math on 5-byte integers [KNUTH_OVERRIDE: ERROR: KTH_OP_MOD, FORK: KTH_PYTHAGORAS]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648 1", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 2147483648", "mod 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648 1", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1 -2147483648", "mod 1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 123", "mod 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"123 9223372036854775807", "mod 123 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 123", "mod -7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-123 9223372036854775807", "mod -123 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 9223372036854775807", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 -123", "mod 7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"123 -9223372036854775807", "mod 123 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 -123", "mod -7 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-123 -9223372036854775807", "mod -123 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-9223372036854775807 -9223372036854775807", "mod 0 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "Check boundary condition"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"", "equal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal, "EQUAL must error when there are no stack items [KNUTH_OVERRIDE: KTH_OP_EQUAL]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "equal not", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_equal, "EQUAL must error when there are not 2 stack items [KNUTH_OVERRIDE: KTH_OP_EQUAL]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0 1", "equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1 1 add", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"11 1 add 12 sub", "11 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"", "checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION
    {"0", "checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION
    {"0 0", "checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION
    {"0 0", "0 checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE
    {"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: EVAL_FALSE
    {"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_check_data_sig, "Check that NULLFAIL trigger only when specified [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: NULLFAIL
    {"[300602010102010101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, "Ensure that sighashtype is ignored [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"} // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER
};

/**
 * Script test chunk 11 (tests 1100 to 1199)
 */
bchn_script_test_list const script_tests_from_json_11{
    {"[300702010102020001] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig, "Non canonical DER encoding [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIG]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER
    {"", "checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION
    {"0 0", "checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: INVALID_STACK_OPERATION
    {"0 0", "0 checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: PUBKEYTYPE
    {"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: CHECKDATASIGVERIFY
    {"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_check_data_sig_verify, "Check that NULLFAIL trigger only when specified [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC, expected: CHECKDATASIGVERIFY
    {"[3006020101020101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, " [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: NULLFAIL
    {"[300602010102010101] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, "Ensure that sighashtype is ignored [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER
    {"[300702010102020001] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, "Non canonical DER encoding [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: P2SH,STRICTENC,NULLFAIL, expected: SIG_DER
    {"2147483648 0 add", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, "arithmetic operands must be in range [-2^31 + 1, 2^31 - 1] [KNUTH_OVERRIDE: KTH_OP_ADD]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"-2147483648 0 add", "nop", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, "arithmetic operands must be in range [-2^31 + 1, 2^31 - 1]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483647 dup add", "4294967294 numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_num_equal, "NUMEQUAL must be in numeric range [KNUTH_OVERRIDE: KTH_OP_NUM_EQUAL]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648 0 add", "nop", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "arithmetic operands are within range [-2^63 + 1, 2^63 - 1]"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648 0 add", "nop", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "arithmetic operands are within range [-2^63 + 1, 2^63 - 1]"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647 dup add", "4294967294 numequal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "NUMEQUAL is within the proper integer range"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"'abcdef' not", "0 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_not, "NOT is an arithmetic operand [KNUTH_OVERRIDE: KTH_OP_NOT]"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2 dup mul", "4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 dup mul", "4 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "OP_MUL re-enabled"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"4611686018427387903 2 mul", "9223372036854775806 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1317624576693539401 7 mul", "9223372036854775807 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, ""}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"4611686018427387904 2 mul", "1 equal", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_mul_overflow, "64-bit integer overflow detected [KNUTH_OVERRIDE: KTH_OP_MUL_OVERFLOW]"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    {"2 mul2", "4 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 div2", "1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 2 lshift", "8 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"2 1 rshift", "1 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_disabled, "disabled"}, // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE
    {"1", "nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10 2 equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::unsatisfied_locktime, " [KNUTH_OVERRIDE: UNSATISFIED_LOCKTIME]"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    // {"'NOP_1_to_10' nop1 checklocktimeverify checksequenceverify nop4 nop5 nop6 nop7 nop8 nop9 nop10", "'NOP_1_to_11' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::unsatisfied_locktime, " [KNUTH_OVERRIDE: UNSATISFIED_LOCKTIME]"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"0x50", "1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "opcode 0x50 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xbd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "available, unassigned codepoints, invalid if executed"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xbe else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xbf else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xef else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "TOKEN_PREFIX; invalid if executed"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if inputindex else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "opcodes for native introspection (not activated here)"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if activebytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if txversion else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if txinputcount else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if txoutputcount else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if txlocktime else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if utxovalue else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if utxobytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if outpointtxhash else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if outpointindex else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if inputbytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if inputsequencenumber else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if outputvalue else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if outputbytecode else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if reserved3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED3, invalid if executed"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if reserved4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED4, invalid if executed"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xd6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "opcodes >= FIRST_UNDEFINED_OP_VALUE invalid if executed"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xd7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xd8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xd9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xda else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xdb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xdc else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xdd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xde else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xdf else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe1 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe2 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe5 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xe9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xea else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xeb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xec else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xed else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xee else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xef else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf0 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf1 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf2 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf3 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf4 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf5 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf6 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf7 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf8 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xf9 else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xfa else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xfb else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xfc else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xfd else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xfe else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "if 0xff else 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, ""}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1 if 1 else", "0xff endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "invalid because scriptSig and scriptPubKey are processed separately"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"nop", "ripemd160", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_ripemd160, " [KNUTH_OVERRIDE: KTH_OP_RIPEMD160]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "sha1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_sha1, " [KNUTH_OVERRIDE: KTH_OP_SHA1]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "sha256", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_sha256, " [KNUTH_OVERRIDE: KTH_OP_SHA256]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "hash160", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_hash160, " [KNUTH_OVERRIDE: KTH_OP_HASH160]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "hash256", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_hash256, " [KNUTH_OVERRIDE: KTH_OP_HASH256]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb'", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_push_data_size, ">520 byte push"}, // flags: P2SH,STRICTENC, expected: PUSH_SIZE
    {"0", "if 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' endif 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_push_data_size, ">520 byte push in non-executed IF branch"}, // flags: P2SH,STRICTENC, expected: PUSH_SIZE
    {"nop", "0 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 'bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb' 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f 2dup 0x616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "10,001-byte scriptPubKey"} // flags: P2SH,STRICTENC, expected: SCRIPT_SIZE
};

/**
 * Script test chunk 12 (tests 1200 to 1299)
 */
bchn_script_test_list const script_tests_from_json_12{
    {"nop1", "nop10", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, ""}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"1", "ver", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_VER is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "verif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_VERIF is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "vernotif", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_VERNOTIF is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "reserved", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "reserved1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED1 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "reserved2", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED2 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "reserved3", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED3 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "reserved4", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED4 is reserved"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"1", "0xd0", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "0xd0 == FIRST_UNDEFINED_OP_VALUE"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"2147483648", "add1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, "We cannot do math on 5-byte integers"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648", "negate 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "We cannot do math on 5-byte integers"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"-2147483648", "add1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, "Because we use a sign bit, -2147483648 is also 5 bytes"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483647", "add1 sub1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, "We cannot do math on 5-byte integers, even if the result is 4-bytes"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648", "sub1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_sub, "We cannot do math on 5-byte integers, even if the result is 4-bytes"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483648", "negate 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"-2147483648", "add1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483647", "add1 sub1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483648", "sub1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do math on 5-byte integers on 64-bit mode"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"9223372036854775807", "add1 sub1 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::op_add, "We cannot do math on 9-byte integers, even if the result is 8-bytes"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: INVALID_NUMBER_RANGE_64_BIT
    {"2147483648 1", "boolor 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "We cannot do BOOLOR on 5-byte integers (but we can still do IF etc)"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648 1", "booland 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "We cannot do BOOLAND on 5-byte integers"}, // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE
    {"2147483648 1", "boolor 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do BOOLOR on 5-byte integers"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"2147483648 1", "booland 1", kth::domain::machine::rule_fork::bch_gauss, kth::error::success, "We can do BOOLOR on 5-byte integers"}, // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK
    {"1", "1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "ENDIF without IF"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1", "if 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "IF without ENDIF"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"1 if 1", "endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "IFs don't carry over"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"nop", "if 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "The following tests check the if(stack.size() < N) tests in each opcode"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"nop", "notif 1 endif", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_stack_scope, "They are here to catch copy-and-paste errors"}, // flags: P2SH,STRICTENC, expected: UNBALANCED_CONDITIONAL
    {"nop", "verify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "Most of them are duplicated elsewhere,"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "toaltstack 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "but, hey, more is always better, right?"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "fromaltstack", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_from_alt_stack, ""}, // flags: P2SH,STRICTENC, expected: INVALID_ALTSTACK_OPERATION
    {"1", "2drop 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "2dup", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1", "3dup", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1 1", "2over", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1 1 1 1", "2rot", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1 1", "2swap", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "ifdup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "drop 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "dup 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "nip", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "over", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_over, " [KNUTH_OVERRIDE: KTH_OP_OVER]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1 1 3", "pick", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "pick 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1 1 3", "roll", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "roll 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1", "rot", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "swap", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "tuck", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "size 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_size, " [KNUTH_OVERRIDE: KTH_OP_SIZE]"}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "equal 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "equalverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "add1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "sub1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_sub, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "negate 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "abs 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "not 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "0notequal 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "add", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_add, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "sub", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_sub, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "booland", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "boolor", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "numequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "numequalverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "numnotequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "lessthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "greaterthan", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "lessthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "greaterthanorequal", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "min", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1", "max", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"1 1", "within", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "ripemd160 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "sha1 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "sha256 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "hash160 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"nop", "hash256 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKSIG must error when there are no stack items"}, // flags: STRICTENC, expected: INVALID_STACK_OPERATION
    {"0", "checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKSIG must error when there are not 2 stack items"}, // flags: STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKMULTISIG must error when there are no stack items"}, // flags: STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "-1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKMULTISIG must error when the specified number of pubkeys is negative"}, // flags: STRICTENC, expected: PUBKEY_COUNT
    {"", "1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKMULTISIG must error when there are not enough pubkeys on the stack"}, // flags: STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "-1 0 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKMULTISIG must error when the specified number of signatures is negative"}, // flags: STRICTENC, expected: SIG_COUNT
    {"", "1 'pk1' 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKMULTISIG must error when there are not enough signatures on the stack"}, // flags: STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "'dummy' 'sig1' 1 'pk1' 1 checkmultisig if 1 endif", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "CHECKMULTISIG must push false to stack when signature is invalid when NOT in strict enc mode"}, // flags: NONE, expected: EVAL_FALSE
    {"", "0 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig 0 0 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "202 CHECKMULTISIGS, fails due to 201 op limit"}, // flags: P2SH,STRICTENC, expected: OP_COUNT
    {"1", "0 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify 0 0 checkmultisigverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION
    {"", "nop nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "Fails due to 201 script operation limit"}, // flags: P2SH,STRICTENC, expected: OP_COUNT
    {"1", "nop nop nop nop nop nop nop nop nop nop nop nop nop 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify 0 0 'a' 'b' 'c' 'd' 'e' 'f' 'g' 'h' 'i' 'j' 'k' 'l' 'm' 'n' 'o' 'p' 'q' 'r' 's' 't' 20 checkmultisigverify", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, ""}, // flags: P2SH,STRICTENC, expected: OP_COUNT
    {"0 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21", "21 checkmultisig 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "nPubKeys > 20"}, // flags: P2SH,STRICTENC, expected: PUBKEY_COUNT
    {"0 'sig' 1 0", "checkmultisig 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "nSigs > nPubKeys"}, // flags: P2SH,STRICTENC, expected: SIG_COUNT
    {"nop [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_pushonly, "Tests for Script.IsPushOnly()"}, // flags: P2SH,STRICTENC, expected: SIG_PUSHONLY
    {"nop1 [51]", "hash160 [da1745e9b549bd0bfa1a569971c77eba30cd5a4b] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_pushonly, ""}, // flags: P2SH,STRICTENC, expected: SIG_PUSHONLY
    {"0 [50]", "hash160 [ece424a6bb6ddf4db592c0faed60685047a361b1] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_RESERVED in P2SH should fail"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0 0x01 ver", "hash160 [0f4d7845db968f2a81b530b6f3c1d6246d4c7e01] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::op_reserved, "OP_VER in P2SH should fail"}, // flags: P2SH,STRICTENC, expected: BAD_OPCODE
    {"0x00", "'00' equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "Basic OP_0 execution"}, // flags: P2SH,STRICTENC, expected: EVAL_FALSE
    {"pushdata1 0x00", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "Empty vector minimally represented by OP_0"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "pushdata1 0x00 drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "Empty vector minimally represented by OP_0 (scriptPubKey)"} // flags: MINIMALDATA, expected: MINIMALDATA
};

/**
 * Script test chunk 13 (tests 1300 to 1399)
 */
bchn_script_test_list const script_tests_from_json_13{
    {"[81]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "-1 minimally represented by OP_1NEGATE"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[81] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "-1 minimally represented by OP_1NEGATE (scriptPubKey)"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[01]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "1 to 16 minimally represented by OP_1 to OP_16"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[01] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "1 to 16 minimally represented by OP_1 to OP_16 (scriptPubKey)"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[02]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[02] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[03]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[03] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[04]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[04] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[05]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[05] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[06]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[06] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[07]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[07] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[08]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[08] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[09]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[09] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0a]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0a] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0b]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0b] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0c]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0c] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0d]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0d] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0e]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0e] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[0f]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[0f] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[10]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[10] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, ""}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[1.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA1 of 72 bytes minimally represented by direct push"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[1.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA1 of 72 bytes minimally represented by direct push (scriptPubKey)"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[2.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA2 of 255 bytes minimally represented by PUSHDATA1"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[2.111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA2 of 255 bytes minimally represented by PUSHDATA1 (scriptPubKey)"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[4.11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111]", "drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA4 of 256 bytes minimally represented by PUSHDATA2"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"", "[4.11111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111111] drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "PUSHDATA4 of 256 bytes minimally represented by PUSHDATA2 (scriptPubKey)"}, // flags: MINIMALDATA, expected: MINIMALDATA
    {"[4f]", "hash160 [c692a0d72bb4690b00f2728775fb8e45eeb190ad] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "minimal push inside P2SH redeemScript"}, // flags: P2SH,MINIMALDATA, expected: OK
    {"[1.4f]", "hash160 [c692a0d72bb4690b00f2728775fb8e45eeb190ad] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "P2SH redeemScript itself was not pushed minimally"}, // flags: P2SH,MINIMALDATA, expected: MINIMALDATA
    {"1 [81]", "hash160 [19e672940307de32037c538ad37578ab38f2b83e] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "P2SH redeemscript 'OP_BIN2NUM' pushed naively"}, // flags: P2SH,MINIMALDATA, expected: MINIMALDATA
    {"1 -1", "hash160 [19e672940307de32037c538ad37578ab38f2b83e] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "P2SH redeemscript 'OP_BIN2NUM' pushed minimally"}, // flags: P2SH,MINIMALDATA, expected: OK
    {"[0181]", "hash160 [823ceb939791c0227262f8c2e5d29f072a590609] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimaldata, "non-minimal push inside P2SH redeemScript"}, // flags: P2SH,MINIMALDATA, expected: MINIMALDATA
    {"[0181]", "hash160 [823ceb939791c0227262f8c2e5d29f072a590609] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "non-minimal push inside P2SH redeemScript but P2SH not active"}, // flags: MINIMALDATA, expected: OK
    {"[00]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals 0"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals 0"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[80]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "0x80 (negative zero) numequals 0"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0080]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals 0"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0500]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals 5"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[050000]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals 5"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0580]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals -5"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[050080]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "numequals -5"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[ff7f80]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "Minimal encoding is 0xffff"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[ff7f00]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "Minimal encoding is 0xff7f"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[ffff7f80]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "Minimal encoding is 0xffffff"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[ffff7f00]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "Minimal encoding is 0xffff7f"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"1 [0000]", "pick drop", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"1 [0000]", "roll drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "add1 drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "sub1 drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "negate drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "abs drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "not drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000]", "0notequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "add drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "add drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "sub drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "sub drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0100]", "div drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::minimal_number, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 1", "div drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::minimal_number, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0100]", "mod drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::minimal_number, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 1", "mod drop 1", kth::domain::machine::rule_fork::bch_euclid, kth::error::minimal_number, " [KNUTH_OVERRIDE: FORK: SIGPUSHONLY]"}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "booland drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "booland drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "boolor drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "boolor drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "numequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 1", "numequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "numequalverify 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "numequalverify 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "numnotequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "numnotequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "lessthan drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "lessthan drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "greaterthan drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "greaterthan drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "lessthanorequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "lessthanorequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "greaterthanorequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "greaterthanorequal drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "min drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "min drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000]", "max drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0", "max drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 0 0", "within drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000] 0", "within drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 0 [0000]", "within drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 0 [0000]", "checkmultisig drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""} // flags: MINIMALDATA, expected: MINIMALNUM
};

/**
 * Script test chunk 14 (tests 1400 to 1499)
 */
bchn_script_test_list const script_tests_from_json_14{
    {"0 [0000] 0", "checkmultisig drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000] 0 1", "checkmultisig drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 0 [0000]", "checkmultisigverify 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"0 [0000] 0", "checkmultisigverify 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[0000] 1", "split 2drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "first operand isn't a number, so no encoding rules"}, // flags: MINIMALDATA, expected: OK
    {"[0000] [0100]", "split 2drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[11111111111111111111111111111111111111110000] [14]", "num2bin drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "first operand is an unlimited number and exempted from length/minimal encoding rules"}, // flags: MINIMALDATA, expected: OK
    {"[11111111111111111111111111111111111111110000] [1400]", "num2bin drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA, expected: MINIMALNUM
    {"[ffffff7f000000000000000000000000000000000000]", "bin2num drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "first operand is a number limited in magnitude but exempted from length/minimal encoding rules"}, // flags: MINIMALDATA, expected: OK
    {"[0000]", "checklocktimeverify drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA,CHECKLOCKTIMEVERIFY, expected: MINIMALNUM
    {"[0000]", "checksequenceverify drop 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, ""}, // flags: MINIMALDATA,CHECKSEQUENCEVERIFY, expected: MINIMALNUM
    {"[303e021d4444444444444444444444444444444444444444444444444444444444021d444444444444444444444444444444444444444444444444444444444401]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[303e021d4444444444444444444444444444444444444444444444444444444444021d4444444444444444444444444444444444444444444444444444444444]", "0 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checkdatasig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [303e021d4444444444444444444444444444444444444444444444444444444444021d444444444444444444444444444444444444444444444444444444444401]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::sig_badlength, ""}, // flags: NONE, expected: SIG_BADLENGTH
    {"0 [303e021d4444444444444444444444444444444444444444444444444444444444021d444444444444444444444444444444444444444444444444444444444401]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_badlength, ""}, // flags: STRICTENC,NULLFAIL, expected: SIG_BADLENGTH
    {"0 [303d021d4444444444444444444444444444444444444444444444444444444444021c4444444444444444444444444444444444444444444444444444444401]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"0 [303f021d4444444444444444444444444444444444444444444444444444444444021e44444444444444444444444444444444444444444444444444444444444401]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: STRICTENC, expected: OK
    {"[00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checkdatasig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, ""}, // flags: NONE, expected: OK
    {"[00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checkdatasig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: STRICTENC, expected: OK
    {"0 [0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_badlength, ""}, // flags: STRICTENC, expected: SIG_BADLENGTH
    {"0 [0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_badlength, ""}, // flags: STRICTENC,NULLFAIL, expected: SIG_BADLENGTH
    {"0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501] [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501]", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 0 2 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "2-of-2 CHECKMULTISIG NOT with the first pubkey invalid, and both signatures validly encoded."}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"0 [3044022044dc17b0887c161bb67ba9635bf758735bdde503e4b0a0987f587f14a4e1143d022009a215772d49a85dae40d8ca03955af26ad3978a0ff965faa12915e9586249a501] 1", "2 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 2 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_signature_encoding, "2-of-2 CHECKMULTISIG NOT with both pubkeys valid, but first signature invalid."}, // flags: STRICTENC, expected: SIG_DER
    {"0 [304402205451ce65ad844dbb978b8bdedf5082e33b43cae8279c30f2c74d9e9ee49a94f802203fe95a7ccf74da7a232ee523ef4a53cb4d14bdd16289680cdb97a63819b8f42f01] [304402205451ce65ad844dbb978b8bdedf5082e33b43cae8279c30f2c74d9e9ee49a94f802203fe95a7ccf74da7a232ee523ef4a53cb4d14bdd16289680cdb97a63819b8f42f]", "2 [02a673638cb9587cb68ea08dbef685c6f2d2a751a8b3c6f2a7e9a4999e6e4bfaf5] [02a673638cb9587cb68ea08dbef685c6f2d2a751a8b3c6f2a7e9a4999e6e4bfaf5] [02a673638cb9587cb68ea08dbef685c6f2d2a751a8b3c6f2a7e9a4999e6e4bfaf5] 3 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_signature_encoding, "2-of-3 with one valid and one invalid signature due to parse error, nSigs > validSigs"}, // flags: P2SH,STRICTENC, expected: SIG_DER
    {"[0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Overly long signature is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[30220220000000000000000000000000000000000000000000000000000000000000000000]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Missing S is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[3024021077777777777777777777777777777777020a7777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "S with invalid S length is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[302403107777777777777777777777777777777702107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Non-integer R is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[302402107777777777777777777777777777777703107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Non-integer S is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[3014020002107777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Zero-length R is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[3014021077777777777777777777777777777777020001]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Zero-length S is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[302402107777777777777777777777777777777702108777777777777777777777777777777701]", "0 checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "Negative S is incorrectly encoded for DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash160 [17743beb429c55c942d2ec703b98c4d57c2df5c6] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "v0 P2SH-P2WPKH with SCRIPT_DISALLOW_SEGWIT_RECOVERY"}, // flags: CLEANSTACK,DISALLOW_SEGWIT_RECOVERY,P2SH, expected: CLEANSTACK
    {"[001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash160 [17743beb429c55c942d2ec703b98c4d57c2df5c6] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Valid Segwit Recovery with v0 P2SH-P2WPKH"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash256 [3ebf47c95c3fdb3b7ea73fe366549c8c59fe097dfd46237ac82c5cab0dc00dff] equal", kth::domain::machine::rule_fork::bch_descartes, kth::error::cleanstack, "Invalid Segwit Recovery with nonsensical v0 P2SH_32-P2WPKH"}, // flags: CLEANSTACK,P2SH,P2SH_32, expected: CLEANSTACK
    {"0 [001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash160 [17743beb429c55c942d2ec703b98c4d57c2df5c6] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "v0 P2SH-P2WPKH Segwit Recovery with extra stack item"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"0 [001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash256 [3ebf47c95c3fdb3b7ea73fe366549c8c59fe097dfd46237ac82c5cab0dc00dff] equal", kth::domain::machine::rule_fork::bch_descartes, kth::error::cleanstack, "Invalid and nonsensical v0 P2SH_32-P2WPKH Segwit Recovery with extra stack item"}, // flags: CLEANSTACK,P2SH,P2SH_32, expected: CLEANSTACK
    {"[00205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [17a6be2f8fe8e94f033e53d17beefda0f3ac4409] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "v0 P2SH-P2WSH with SCRIPT_DISALLOW_SEGWIT_RECOVERY"}, // flags: CLEANSTACK,DISALLOW_SEGWIT_RECOVERY,P2SH, expected: CLEANSTACK
    {"[00205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [17a6be2f8fe8e94f033e53d17beefda0f3ac4409] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Valid Segwit Recovery with v0 P2SH-P2WSH"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"0 [00205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [17a6be2f8fe8e94f033e53d17beefda0f3ac4409] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "v0 P2SH-P2WSH Segwit Recovery with extra stack item"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[00015a]", "hash160 [40b6941895022d458de8f4bbfe27f3aaa4fb9a74] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (too short)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[00025a01]", "hash160 [86123d8e050333a605e434ecf73128d83815b36f] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Segwit Recovery with valid witness program (min allowed length)"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[00285a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f2021222324252627]", "hash160 [df7b93f88e83471b479fb219ae90e5b633d6b750] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Segwit Recovery with valid witness program (max allowed length)"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[00295a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728]", "hash160 [13aa4fcfd630508e0794dca320cac172c5790aea] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (too long)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[60205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [9b0c7017004d3818b7c833ddb3cb5547a22034d0] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Segwit Recovery with valid witness program (max allowed version)"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[4f205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [97aa1e96e49ca6d744d7344f649dd9f94bcc35eb] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (invalid version -1)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[0111205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [4b5321beb1c09f593ff3c02be4af21c7f949e101] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (invalid version 17)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[00205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f51]", "hash160 [8eb812176c9e71732584123dd06d3246e659b199] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (more than 2 stack items)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[00020000]", "hash160 [0e01bcfe7c6f3fd2fd8f81092299369744684733] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Valid segwit recovery, in spite of false value being left on stack (0)"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[00020000]", "hash256 [94e34f4cb55063341d5d1436a317fce53f7ab4353a8e15f4de545e12c97a30dd] equal", kth::domain::machine::rule_fork::bch_descartes, kth::error::stack_false, "Attempt to do the same as above, false value being left on stack (0), but with p2sh_32 should fail."}, // flags: CLEANSTACK,P2SH,P2SH_32, expected: EVAL_FALSE
    {"[00020080]", "hash160 [10ddc638cb26615f867dad80efacced9e73766bc] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "Valid segwit recovery, in spite of false value being left on stack (minus 0)"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[00020000]", "hash160 [0e01bcfe7c6f3fd2fd8f81092299369744684733] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::stack_false, "Otherwise valid segwit recovery, in spite of false value being left on stack (0), but with SCRIPT_DISALLOW_SEGWIT_RECOVERY"}, // flags: CLEANSTACK,DISALLOW_SEGWIT_RECOVERY,P2SH, expected: EVAL_FALSE
    {"[00020080]", "hash160 [10ddc638cb26615f867dad80efacced9e73766bc] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::stack_false, "Otherwise valid segwit recovery, in spite of false value being left on stack (minus 0), but with SCRIPT_DISALLOW_SEGWIT_RECOVERY"}, // flags: CLEANSTACK,DISALLOW_SEGWIT_RECOVERY,P2SH, expected: EVAL_FALSE
    {"[50205a0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f]", "hash160 [be02794ceede051da41b420e88a86fff2802af06] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::op_reserved, "Segwit Recovery with invalid witness program (OP_RESERVED in version field)"}, // flags: CLEANSTACK,P2SH, expected: BAD_OPCODE
    {"[01001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash160 [0718743e67c1ef4911e0421f206c5ff81755718e] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (non-minimal push in version field)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[004c0245aa]", "hash160 [d3ec673296c7fd7e1a9e53bfc36f414de303e905] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "Segwit Recovery with invalid witness program (non-minimal push in program field)"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[001491b24bf9f5288532960ac687abb035127b1d28a5]", "hash160 [17a6be2f8fe8e94f033e53d17beefda0f3ac4409] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::stack_false, "v0 P2SH-P2WPKH Segwit Recovery whose redeem script hash does not match P2SH output"}, // flags: CLEANSTACK,P2SH, expected: EVAL_FALSE
    {"[001491b24bf9f5288532960ac687abb035127b1d28a5]", "1", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "v0 P2SH-P2WPKH Segwit Recovery spending a non-P2SH output"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[3044022022127048516d473153d1f74e46e828496776752e3255f672f760a41e83f54e6f0220502956b739ed82aad916dc4a73e1fd55d02aad514b5211f1ba7d0dadf53c637901]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK"}, // flags: NONE, expected: OK
    {"[3044022022127048516d463153d1f74e46e828496776752e3255f672f760a41e83f54e6f0220502956b739ed82aad916dc4a73e1fd55d02aad514b5211f1ba7d0dadf53c637901]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "P2PK, bad sig"}, // flags: NONE, expected: EVAL_FALSE
    {"[304402201e0ec3c6c263f34049c93e0bc646d7287ca2cc6571d658e4e7269daebc96ef35022009841f101e6dcaba8993d0259e5732a871e253be807556bf5618bf0bc3e84af001] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508]", "dup hash160 [1018853670f9f3b0582c5b9ee8ce93764ac32b93] equalverify checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PKH"}, // flags: NONE, expected: OK
    {"[304402201dddd1a3b642f1b543cbecad602558636135d59b3850a4d1646a0b11c712b340022016d58230012421b304d1cc42ccc1e33738206be64f7984be30ed437fa7a2fd0a01] [03363d90d446b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640]", "dup hash160 [c0834c0c158f53be706d234c38fd52de7eece656] equalverify checksig", kth::domain::machine::rule_fork::no_rules, kth::error::invalid_script, "P2PKH, bad pubkey"}, // flags: NONE, expected: EQUALVERIFY
    {"[304402206e8719d1746852542bed4f05a57f0369759553c7d655229759de2643f371e20002207a7f06790da5757d2f37c4d074de0fe66ac149ee011baeb4dd3fa97f288637f881]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK anyonecanpay"}, // flags: NONE, expected: OK
    {"[304402206e8719d1746852542bed4f05a57f0369759553c7d655229759de2643f371e20002207a7f06790da5757d2f37c4d074de0fe66ac149ee011baeb4dd3fa97f288637f801]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "P2PK anyonecanpay marked with normal hashtype"}, // flags: NONE, expected: EVAL_FALSE
    {"[304402206f04a92fb434b24b06180c7c2af5f4fb73f15427da93b5ab8c167c7d10f4f37902203f0b57300a19fe106d79f427360a6b4720117ab50de44047e52a35de2523291e01] [210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798ac]", "hash160 [23b0ad3477f2178bc0b3eed26e4e6316f4e83aa1] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2SH(P2PK)"}, // flags: P2SH, expected: OK
    {"[304402206f04a92fb434b24b06180c7c2af5f4fb73f15427da93b5ab8c167c7d10f4f37902203f0b57300a19fe106d79f427360a6b4720117ab50de44047e52a35de2523291e01] [210279be667ef9dcbbac54a06295ce870b07029bfcdb2dce28d959f2815b16f81798ac]", "hash160 [23b0ad3477f2178bc0b3eed26e4e6316f4e83aa1] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, "P2SH(P2PK), bad redeemscript"}, // flags: P2SH, expected: EVAL_FALSE
    {"[304402201799a8feda56fd3c58b816eac4f0bb916136b4f60b5c5da3264f5c29aec13abf022029c9d7779a5376b5d0398e389817699bb2485a305eb1fa5868081c3a9d89bee201] [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] [76a91491b24bf9f5288532960ac687abb035127b1d28a588ac]", "hash160 [7f67f0521934a57d3039f77f9f32cf313f3ac74b] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2SH(P2PKH)"}, // flags: P2SH, expected: OK
    {"[3044022001aabce638c1bc883223651a4252d36e5918681e85562fe5b112f08bc944644902203110421d46142317ec5ca117ce4d40593ace16ab5b9688eb2ce8103b3a9273c701] [76a9147cf9c846cd4882efec4bf07e44ebdad495c94f4b88ac]", "hash160 [2df519943d5acc0ef5222091f9dfe3543f489a82] equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2SH(P2PKH), bad sig but no VERIFY_P2SH"}, // flags: NONE, expected: OK
    {"[3044022001aabce638c1bc883223651a4252d36e5918681e85562fe5b112f08bc944644902203110421d46142317ec5ca117ce4d40593ace16ab5b9688eb2ce8103b3a9273c701] [76a9147cf9c846cd4882efec4bf07e44ebdad495c94f4b88ac]", "hash160 [2df519943d5acc0ef5222091f9dfe3543f489a82] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::invalid_script, "P2SH(P2PKH), bad sig"}, // flags: P2SH, expected: EQUALVERIFY
    {"0 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [3044022031a1e5289b0d9c33ec182a7f67210b9997187c710f7d3f0f28bdfb618c4e025c02205d95fe63ee83a20ec44159a06f7c0b43b61d5f0c346ca4a2cc7b91878ad1a85001] [304402200a9faba8228f7a86bf6c3b2a0da0e2f9136ea390e5a5f66dbf232e499459f34a0220437bcac47d837870eeb41aabc379cbf2b1dcef954bd887f3968849922694ebd701]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "3-of-3"}, // flags: NONE, expected: OK
    {"0 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [3044022031a1e5289b0d9c33ec182a7f67210b9997187c710f7d3f0f28bdfb618c4e025c02205d95fe63ee83a20ec44159a06f7c0b43b61d5f0c346ca4a2cc7b91878ad1a85001] 0", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "3-of-3, 2 sigs"}, // flags: NONE, expected: EVAL_FALSE
    {"0 [304402205b6256a4755890fe278ea083eddf5c0519d0d7fad14fe265e077c5627171b27e02200d841cb15cdc9a9c8f3ec20c5e4bb1b0e5715d26353f500378f4215195e3558c01] [304402200b0620f910e92621934e88ae0177b0bb381ba812d4c23c77afbf1a3ab637a0bb0220275387c859089939e22d803dfae77a4ecd122d951d0feb9e6a65a6918c2b1e0201] 0x4c69 0x52210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f8179821038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464053ae", "hash160 [c9e4a896d149702d0d1695434feddd52e24ad78d] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2SH(2-of-3)"}, // flags: P2SH, expected: OK
    {"0 [304402205b6256a4755890fe278ea083eddf5c0519d0d7fad14fe265e077c5627171b27e02200d841cb15cdc9a9c8f3ec20c5e4bb1b0e5715d26353f500378f4215195e3558c01] 0 0x4c69 0x52210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f8179821038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464053ae", "hash160 [c9e4a896d149702d0d1695434feddd52e24ad78d] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, "P2SH(2-of-3), 1 sig"}, // flags: P2SH, expected: EVAL_FALSE
    {"[30440220007dc49df798c714600c93a94dcdffc89013aac6683c2be18c54d136fea0a26e0220681bb9fad75a638536c0cfafaca8332a15c6c4047fd3989df5d232c0efa5411f01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with too much R padding but no DERSIG"}, // flags: NONE, expected: OK
    {"[30440220007dc49df798c714600c93a94dcdffc89013aac6683c2be18c54d136fea0a26e0220681bb9fad75a638536c0cfafaca8332a15c6c4047fd3989df5d232c0efa5411f01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK with too much R padding"}, // flags: DERSIG, expected: SIG_DER
    {"[304502207a404c8f2bb2ac64db7cbd4d3490eaecb06115172d99e4447e6f4dbfbaa92fe4022100631033d772b4313d4a0d3cda8eacab93654dd8bcdfb9340ba9271be29cfc249c01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with too much S padding but no DERSIG"}, // flags: NONE, expected: OK
    {"[304502207a404c8f2bb2ac64db7cbd4d3490eaecb06115172d99e4447e6f4dbfbaa92fe4022100631033d772b4313d4a0d3cda8eacab93654dd8bcdfb9340ba9271be29cfc249c01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK with too much S padding"}, // flags: DERSIG, expected: SIG_DER
    {"[30440220d888248d694d649657d71a5baebb70512525ba5fe76575afb5cbbe9f0680783d022054a16bce5e3957be61e15940b11d66515bc8df1c0e090c47acf32515d56ef95001]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with too little R padding but no DERSIG"}, // flags: NONE, expected: OK
    {"[30440220d888248d694d649657d71a5baebb70512525ba5fe76575afb5cbbe9f0680783d022054a16bce5e3957be61e15940b11d66515bc8df1c0e090c47acf32515d56ef95001]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK with too little R padding"}, // flags: DERSIG, expected: SIG_DER
    {"[30440220001d5bd10da0bc0ab3018cea865f8d936bf625fee21464b127532f6021d5e82b02203b3660b9dccd43ae3def3ec3e1853b2566bb6bd899f5364c06508eec532f700e01]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK NOT with bad sig with too much R padding but no DERSIG"}, // flags: NONE, expected: OK
    {"[30440220001d5bd10da0bc0ab3018cea865f8d936bf625fee21464b127532f6021d5e82b02203b3660b9dccd43ae3def3ec3e1853b2566bb6bd899f5364c06508eec532f700e01]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK NOT with bad sig with too much R padding"}, // flags: DERSIG, expected: SIG_DER
    {"[30440220001d5bd10da0bd0ab3018cea865f8d936bf625fee21464b127532f6021d5e82b02203b3660b9dccd43ae3def3ec3e1853b2566bb6bd899f5364c06508eec532f700e01]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "P2PK NOT with too much R padding but no DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"[30440220001d5bd10da0bd0ab3018cea865f8d936bf625fee21464b127532f6021d5e82b02203b3660b9dccd43ae3def3ec3e1853b2566bb6bd899f5364c06508eec532f700e01]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK NOT with too much R padding"}, // flags: DERSIG, expected: SIG_DER
    {"[30440220d888248d694d649657d71a5baebb70512525ba5fe76575afb5cbbe9f0680783d022054a16bce5e3957be61e15940b11d66515bc8df1c0e090c47acf32515d56ef95001]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 1, without DERSIG"}, // flags: NONE, expected: OK
    {"[30440220d888248d694d649657d71a5baebb70512525ba5fe76575afb5cbbe9f0680783d022054a16bce5e3957be61e15940b11d66515bc8df1c0e090c47acf32515d56ef95001]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 1, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[304402208eae7755261155b02becf6e6990fe6d1392210ff19d46cbbba5456e4b59a71cd022047054a0a3f229eaf9f597033c85877913596fa7bdaf2f1b55cf5704ce6ee99bc01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 2, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"[304402208eae7755261155b02becf6e6990fe6d1392210ff19d46cbbba5456e4b59a71cd022047054a0a3f229eaf9f597033c85877913596fa7bdaf2f1b55cf5704ce6ee99bc01]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 2, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 3, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::stack_false, "BIP66 example 3, with DERSIG"}, // flags: DERSIG, expected: EVAL_FALSE
    {"0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 4, without DERSIG"}, // flags: NONE, expected: OK
    {"0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66 example 4, with DERSIG"}, // flags: DERSIG, expected: OK
    {"[300602010102010101]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66 example 4, with DERSIG, non-null DER-compliant signature"}, // flags: DERSIG, expected: OK
    {"0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "BIP66 example 4, with DERSIG and NULLFAIL"}, // flags: DERSIG,NULLFAIL, expected: OK
    {"[300602010102010101]", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "BIP66 example 4, with DERSIG and NULLFAIL, non-null DER-compliant signature"}, // flags: DERSIG,NULLFAIL, expected: NULLFAIL
    {"1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 5, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 5, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 6, without DERSIG"}, // flags: NONE, expected: OK
    {"1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checksig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 6, with DERSIG"} // flags: DERSIG, expected: SIG_DER
};

/**
 * Script test chunk 15 (tests 1500 to 1599)
 */
bchn_script_test_list const script_tests_from_json_15{
    {"0 [30440220868d1de0f6cc0b0d465992dd177fc545df03381f5a79c171ad55990e623417da022078276700801d7f454b013ded53ddeca5d9ea55bd704e1683832ece986c09fd3201] [304402207349bbfeebc59c0efc363dc94e0c0f0491e9c6456a86b753c4ce1ca74b51bfdf02200d94da026a56016b975451d7b0dd2c46780ac3dfa008791e0665af04a235e80f01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 7, without DERSIG"}, // flags: NONE, expected: OK
    {"0 [30440220868d1de0f6cc0b0d465992dd177fc545df03381f5a79c171ad55990e623417da022078276700801d7f454b013ded53ddeca5d9ea55bd704e1683832ece986c09fd3201] [304402207349bbfeebc59c0efc363dc94e0c0f0491e9c6456a86b753c4ce1ca74b51bfdf02200d94da026a56016b975451d7b0dd2c46780ac3dfa008791e0665af04a235e80f01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 7, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"0 [30440220e8172152b43bc9c788442b116c2345f04591d44d5be77cd6e9acafed7cd3649e022075c0e1014e000cf02e2cc4b33159d74e94c54a5c8d2c046bae2a7e1e38896b2f01] [30440220667437f14da10aca92795812e31784b42824d865c535584b7f01a0b1d7bda8b802203331b1f9d6421310aace34a3a8b193853bf17dcec0cfe3f2ddec806bbeee527801]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 8, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"0 [30440220e8172152b43bc9c788442b116c2345f04591d44d5be77cd6e9acafed7cd3649e022075c0e1014e000cf02e2cc4b33159d74e94c54a5c8d2c046bae2a7e1e38896b2f01] [30440220667437f14da10aca92795812e31784b42824d865c535584b7f01a0b1d7bda8b802203331b1f9d6421310aace34a3a8b193853bf17dcec0cfe3f2ddec806bbeee527801]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 8, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"0 0 [30440220e6afebaa7f2b0a06c014519ee51d97e73365151e802a3913bb9b1198021ffdb702200e6cbbe6ba99a5b4424756a544e5ffed5867b67430b9ba893f2b65309a8a762d01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 9, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"0 0 [30440220e6afebaa7f2b0a06c014519ee51d97e73365151e802a3913bb9b1198021ffdb702200e6cbbe6ba99a5b4424756a544e5ffed5867b67430b9ba893f2b65309a8a762d01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 9, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"0 0 [30440220e8e61fae71ba1147b92ea58aae6d314001d6071add352af500da476f466782600220112491a261785241c009386e18a5f79b946a58ba958081a7da1ccec31f3aa9bc01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 10, without DERSIG"}, // flags: NONE, expected: OK
    {"0 0 [30440220e8e61fae71ba1147b92ea58aae6d314001d6071add352af500da476f466782600220112491a261785241c009386e18a5f79b946a58ba958081a7da1ccec31f3aa9bc01]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "BIP66 example 10, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"0 [30440220868d1de0f6cc0b0d465992dd177fc545df03381f5a79c171ad55990e623417da022078276700801d7f454b013ded53ddeca5d9ea55bd704e1683832ece986c09fd3201] 0", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "BIP66 example 11, without DERSIG"}, // flags: NONE, expected: EVAL_FALSE
    {"0 [30440220868d1de0f6cc0b0d465992dd177fc545df03381f5a79c171ad55990e623417da022078276700801d7f454b013ded53ddeca5d9ea55bd704e1683832ece986c09fd3201] 0", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig", kth::domain::machine::rule_fork::bip66_rule, kth::error::stack_false, "BIP66 example 11, with DERSIG"}, // flags: DERSIG, expected: EVAL_FALSE
    {"0 [30440220e8172152b43bc9c788442b116c2345f04591d44d5be77cd6e9acafed7cd3649e022075c0e1014e000cf02e2cc4b33159d74e94c54a5c8d2c046bae2a7e1e38896b2f01] 0", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "BIP66 example 12, without DERSIG"}, // flags: NONE, expected: OK
    {"0 [30440220e8172152b43bc9c788442b116c2345f04591d44d5be77cd6e9acafed7cd3649e022075c0e1014e000cf02e2cc4b33159d74e94c54a5c8d2c046bae2a7e1e38896b2f01] 0", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 2 checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66 example 12, with DERSIG"}, // flags: DERSIG, expected: OK
    {"[3044022032d4a3dec9cad54c94b5585fb058f7e41ac5173843b0c2e7197a40421ee4ab54022041d4d4296d8c2821bb39f6349f0140bc2c95f16496df9fcd894758673d1db7d80101]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with multi-byte hashtype, without DERSIG"}, // flags: NONE, expected: OK
    {"[3044022032d4a3dec9cad54c94b5585fb058f7e41ac5173843b0c2e7197a40421ee4ab54022041d4d4296d8c2821bb39f6349f0140bc2c95f16496df9fcd894758673d1db7d80101]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig", kth::domain::machine::rule_fork::bip66_rule, kth::error::invalid_signature_encoding, "P2PK with multi-byte hashtype, with DERSIG"}, // flags: DERSIG, expected: SIG_DER
    {"[3045022032d4a3dec9cad54c94b5585fb058f7e41ac5173843b0c2e7197a40421ee4ab54022100be2b2bd69273d7de44c609cb60febf428e18eb821869006e368b06259318896901]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with high S but no LOW_S"}, // flags: NONE, expected: OK
    {"[3045022032d4a3dec9cad54c94b5585fb058f7e41ac5173843b0c2e7197a40421ee4ab54022100be2b2bd69273d7de44c609cb60febf428e18eb821869006e368b06259318896901]", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_high_s, "P2PK with high S"}, // flags: LOW_S, expected: SIG_HIGH_S
    {"[304402201162483d0440fc508d79d314a91cfc4df4ea31b302f02c16893fa15d9c9f7f06022032afebdd5b2964c40c887b967f9599e0e5b5ff5b2f899ab62ada12a5af2a481701]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: OK
    {"[304402201162483d0440fc508d79d314a91cfc4df4ea31b302f02c16893fa15d9c9f7f06022032afebdd5b2964c40c887b967f9599e0e5b5ff5b2f899ab62ada12a5af2a481701]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "P2PK with hybrid pubkey"}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"[3044022057a786d0e26b1bad0f928d05b94a995afefe4f523450ef82893779e95cf2042102203b049387f08eac1829061b91a6cf0b67280b4b4948e4e1773d3b4dfbbd64a4c001]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "P2PK NOT with hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: EVAL_FALSE
    {"[3044022057a786d0e26b1bad0f928d05b94a995afefe4f523450ef82893779e95cf2042102203b049387f08eac1829061b91a6cf0b67280b4b4948e4e1773d3b4dfbbd64a4c001]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "P2PK NOT with hybrid pubkey"}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"[3044022057a786d0e26b1aad0f928d05b94a995afefe4f523450ef82893779e95cf2042102203b049387f08eac1829061b91a6cf0b67280b4b4948e4e1773d3b4dfbbd64a4c001]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK NOT with invalid hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: OK
    {"[3044022057a786d0e26b1aad0f928d05b94a995afefe4f523450ef82893779e95cf2042102203b049387f08eac1829061b91a6cf0b67280b4b4948e4e1773d3b4dfbbd64a4c001]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "P2PK NOT with invalid hybrid pubkey"}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"0 [30440220379db2a13c8c679bda7f500c93bae36e0d75c96b8af1754316a9c80e843f2923022046ee947cd3b916ab78f95be6ae29cc2f037ddb47f8e59c966b1e6813b270648001]", "1 [0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "1-of-2 with the second 1 hybrid pubkey and no STRICTENC"}, // flags: NONE, expected: OK
    {"0 [30440220379db2a13c8c679bda7f500c93bae36e0d75c96b8af1754316a9c80e843f2923022046ee947cd3b916ab78f95be6ae29cc2f037ddb47f8e59c966b1e6813b270648001]", "1 [0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "1-of-2 with the second 1 hybrid pubkey"}, // flags: STRICTENC, expected: OK
    {"0 [304402202d4dd56f6dee2eccc049a4ebd65e77e4325aa3f2940130412b9f96fd28fa8c5a022074963950dd250d2fc943e60c7a4133cc8b1fcc46253e9b19f71c9723d1d894ef01]", "1 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 2 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "1-of-2 with the first 1 hybrid pubkey"}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"[30440220020cf2d9ccc6d8a8b2f6f0f90e9805845194d9715392e58f08e1b63d2566c53e02207b75a322355b3820f59dffc5793ed5947aa3f4e460edeb28cc306573db09495705]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with undefined hashtype but no STRICTENC"}, // flags: NONE, expected: OK
    {"[30440220020cf2d9ccc6d8a8b2f6f0f90e9805845194d9715392e58f08e1b63d2566c53e02207b75a322355b3820f59dffc5793ed5947aa3f4e460edeb28cc306573db09495705]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "P2PK with undefined hashtype"}, // flags: STRICTENC, expected: SIG_HASHTYPE
    {"[3044022070fd4f47970fdb7814b79482d1132a7c3598437bef78f8458de6e5d66ce918fd02204e657846fee57cb6fc3cbaae6b85db8646cc351f96155c86387388c2e8a5a83a21] [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8]", "dup hash160 [91b24bf9f5288532960ac687abb035127b1d28a5] equalverify checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PKH with invalid sighashtype"}, // flags: NONE, expected: OK
    {"[3044022070fd4f47970fdb7814b79482d1132a7c3598437bef78f8458de6e5d66ce918fd02204e657846fee57cb6fc3cbaae6b85db8646cc351f96155c86387388c2e8a5a83a21] [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8]", "dup hash160 [91b24bf9f5288532960ac687abb035127b1d28a5] equalverify checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "P2PKH with invalid sighashtype and STRICTENC"}, // flags: STRICTENC, expected: SIG_HASHTYPE
    {"[30440220643b4c3979d9d2c41842d4843113c1b40ca1df85805c7c654cabb015208c23f702207d3675bf98419065ac9a24865b652551849a0b1e52fdc429cd150f5e7897627821] [41048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26cafac]", "hash160 [49ba2f86705b5dcd48d93b750f03289db3b8ce21] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2SH(P2PK) with invalid sighashtype"}, // flags: P2SH, expected: OK
    {"[30440220643b4c3979d9d2c41842d4843113c1b40ca1df85805c7c654cabb015208c23f702207d3675bf98419065ac9a24865b652551849a0b1e52fdc429cd150f5e7897627821] [41048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26cafac]", "hash160 [49ba2f86705b5dcd48d93b750f03289db3b8ce21] equal", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "P2SH(P2PK) with invalid sighashtype and STRICTENC"}, // flags: P2SH,STRICTENC, expected: SIG_HASHTYPE
    {"[304402200cbfa0902c2129020eb09acbf89725a409573096046d4bb7ba0bdc2af1588139022062a9c69be90b7f434c7e38a8a741df604a695610a8f6a0789082f1104f72d57105]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK NOT with invalid sig and undefined hashtype but no STRICTENC"}, // flags: NONE, expected: OK
    {"[304402200cbfa0902c2129020eb09acbf89725a409573096046d4bb7ba0bdc2af1588139022062a9c69be90b7f434c7e38a8a741df604a695610a8f6a0789082f1104f72d57105]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "P2PK NOT with invalid sig and undefined hashtype"}, // flags: STRICTENC, expected: SIG_HASHTYPE
    {"1 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [3044022031a1e5289b0d9c33ec182a7f67210b9997187c710f7d3f0f28bdfb618c4e025c02205d95fe63ee83a20ec44159a06f7c0b43b61d5f0c346ca4a2cc7b91878ad1a85001] [304402200a9faba8228f7a86bf6c3b2a0da0e2f9136ea390e5a5f66dbf232e499459f34a0220437bcac47d837870eeb41aabc379cbf2b1dcef954bd887f3968849922694ebd701]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "3-of-3 with nonzero dummy"}, // flags: NONE, expected: OK
    {"1 [304402203f78692a12d075d5057dea07265f0ba8ede774f083b85896fe07231efbe155da0220388fc48ffd8fb1c189fbf8bd767ee1c38b3f5d864b18faa1d81d0c3670cda85b01] [3044022070a2850b5363fc49db8f617f08f5947bb85a1d439da07a6dc0d2fc178d9a5f3f022019e15a476e9dab8e3cb18a2bcae279366b1701c1b11c020851517cf03132bbff01] [304402206457d83204e94902c789a16fa9e2f1f1d25684c7748c93373e9077b01a49b80a02204b0509cba423c7bbad8f80653c54a1ce10c901d33292464a4d5162f2016ddc2901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "3-of-3 NOT with invalid sig and nonzero dummy"}, // flags: NONE, expected: OK
    {"0 [3044022044b11d5a64bebbd4280c616146bc961eed3081300eef527fbe8da91f6775892a022045b044719bf5c7100617cd518fff22c102f93e6768ed986543668be43bc44d1601] dup", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "2-of-2 with two identical keys and sigs pushed using OP_DUP but no SIGPUSHONLY"}, // flags: NONE, expected: OK
    {"0 [3044022044b11d5a64bebbd4280c616146bc961eed3081300eef527fbe8da91f6775892a022045b044719bf5c7100617cd518fff22c102f93e6768ed986543668be43bc44d1601] dup", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::bch_euclid, kth::error::sig_pushonly, "2-of-2 with two identical keys and sigs pushed using OP_DUP"}, // flags: SIGPUSHONLY, expected: SIG_PUSHONLY
    {"[304402204b26ae3298e583956aed912c1e6a32b94efc4cf3a97495d83f7a6edd64f8c11702204d0b112f3ebbffe8a0626deb57f62f33432fae8dc1a3de1658464bbb12f8260c01] nop8 [2103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640ac]", "hash160 [215640c2f72f0d16b4eced26762035a42ffed39a] equal", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2SH(P2PK) with non-push scriptSig but no P2SH or SIGPUSHONLY"}, // flags: NONE, expected: OK
    {"[3044022032d4a3dec9cad54c94b5585fb058f7e41ac5173843b0c2e7197a40421ee4ab54022041d4d4296d8c2821bb39f6349f0140bc2c95f16496df9fcd894758673d1db7d801] nop8", "[03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "P2PK with non-push scriptSig but with P2SH validation"}, // flags: NONE, expected: OK
    {"[304402204b26ae3298e583956aed912c1e6a32b94efc4cf3a97495d83f7a6edd64f8c11702204d0b112f3ebbffe8a0626deb57f62f33432fae8dc1a3de1658464bbb12f8260c01] nop8 [2103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640ac]", "hash160 [215640c2f72f0d16b4eced26762035a42ffed39a] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::sig_pushonly, "P2SH(P2PK) with non-push scriptSig but no SIGPUSHONLY"}, // flags: P2SH, expected: SIG_PUSHONLY
    {"[304402204b26ae3298e583956aed912c1e6a32b94efc4cf3a97495d83f7a6edd64f8c11702204d0b112f3ebbffe8a0626deb57f62f33432fae8dc1a3de1658464bbb12f8260c01] nop8 [2103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640ac]", "hash160 [215640c2f72f0d16b4eced26762035a42ffed39a] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::sig_pushonly, "P2SH(P2PK) with non-push scriptSig but not P2SH"}, // flags: SIGPUSHONLY, expected: SIG_PUSHONLY
    {"0 [3044022044b11d5a64bebbd4280c616146bc961eed3081300eef527fbe8da91f6775892a022045b044719bf5c7100617cd518fff22c102f93e6768ed986543668be43bc44d1601] [3044022044b11d5a64bebbd4280c616146bc961eed3081300eef527fbe8da91f6775892a022045b044719bf5c7100617cd518fff22c102f93e6768ed986543668be43bc44d1601]", "2 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "2-of-2 with two identical keys and sigs pushed"}, // flags: SIGPUSHONLY, expected: OK
    {"11 [3044022022127048516d473153d1f74e46e828496776752e3255f672f760a41e83f54e6f0220502956b739ed82aad916dc4a73e1fd55d02aad514b5211f1ba7d0dadf53c637901]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2PK with unnecessary input but no CLEANSTACK"}, // flags: P2SH, expected: OK
    {"11 [3044022022127048516d473153d1f74e46e828496776752e3255f672f760a41e83f54e6f0220502956b739ed82aad916dc4a73e1fd55d02aad514b5211f1ba7d0dadf53c637901]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "P2PK with unnecessary input"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"11 [3044022065066ad31463bd8ebb9cb7ef621e9c96fa3411a3ed393a3e41120a7ea688babf02206869ac4e007f853c1a15bca2670eabeb9a54cac1bddf5c4f73c03f8c17cbffa701] [410479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8ac]", "hash160 [31edc23bdafda4639e669f89ad6b2318dd79d032] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, "P2SH with unnecessary input but no CLEANSTACK"}, // flags: P2SH, expected: OK
    {"11 [3044022065066ad31463bd8ebb9cb7ef621e9c96fa3411a3ed393a3e41120a7ea688babf02206869ac4e007f853c1a15bca2670eabeb9a54cac1bddf5c4f73c03f8c17cbffa701] [410479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8ac]", "hash160 [31edc23bdafda4639e669f89ad6b2318dd79d032] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::cleanstack, "P2SH with unnecessary input"}, // flags: CLEANSTACK,P2SH, expected: CLEANSTACK
    {"[3044022065066ad31463bd8ebb9cb7ef621e9c96fa3411a3ed393a3e41120a7ea688babf02206869ac4e007f853c1a15bca2670eabeb9a54cac1bddf5c4f73c03f8c17cbffa701] [410479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8ac]", "hash160 [31edc23bdafda4639e669f89ad6b2318dd79d032] equal", kth::domain::machine::rule_fork::bch_euclid, kth::error::success, "P2SH with CLEANSTACK"}, // flags: CLEANSTACK,P2SH, expected: OK
    {"[123450.0]", "[304402206e3afa6dd4d1db87538fa48a0ef3f824d7ec554103fe5fb3527254d78bd79617022066097e981df0d1ce3c07224a73585ca1f5e8ebdf79639b2cc73c20bde066075141]", kth::domain::machine::rule_fork::no_rules, kth::error::sighash_forkid, "OK"}, // flags: NONE, expected: SIGHASH_FORKID
    {"[123450.0]", "[3044022054e19540c81ccb214b62acda158faf01440d98adab2e13ed43a4d35cc551bfff02205b05fc09882819b3fdbe56adfef63243d7faa5c4837db0c72ad9356eae9a5b8c41]", kth::domain::machine::rule_fork::no_rules, kth::error::sighash_forkid, "EVAL_FALSE"}, // flags: NONE, expected: SIGHASH_FORKID
    {"[123450.0]", "[304402206e3afa6dd4d1db87538fa48a0ef3f824d7ec554103fe5fb3527254d78bd79617022066097e981df0d1ce3c07224a73585ca1f5e8ebdf79639b2cc73c20bde066075141]", kth::domain::machine::rule_fork::no_rules, kth::error::strict_encoding, "ILLEGAL_FORKID"}, // flags: NONE, expected: STRICTENC
    {"[123450.0]", "[304402205ce75e7249823fe2cc78f861579189f8e10585008aa057d4e6d755bfca1c8c6802200f63547feabc3f198f18742e84d57d6ca4bdd477d10a121554647cba36742f2b41]", kth::domain::machine::rule_fork::no_rules, kth::error::sighash_forkid, "EVAL_FALSE"}, // flags: NONE, expected: SIGHASH_FORKID
    {"[123450.0]", "[304402206e3afa6dd4d1db87538fa48a0ef3f824d7ec554103fe5fb3527254d78bd79617022066097e981df0d1ce3c07224a73585ca1f5e8ebdf79639b2cc73c20bde066075141]", kth::domain::machine::rule_fork::no_rules, kth::error::sighash_forkid, "OK"}, // flags: NONE, expected: SIGHASH_FORKID
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "Standard CHECKDATASIG"}, // flags: NULLFAIL,STRICTENC, expected: OK
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "CHECKDATASIG with NULLFAIL flags"}, // flags: NULLFAIL,STRICTENC, expected: NULLFAIL
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG without NULLFAIL flags"}, // flags: STRICTENC, expected: OK
    {"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIG empty signature"}, // flags: NULLFAIL,STRICTENC, expected: OK
    {"[3045022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022100c869b0fe59e24e4db15bbec84c29828e24c2b6f02370eb32dfbe1ca77c98aa98] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIG with High S but no Low S"}, // flags: NULLFAIL,STRICTENC, expected: OK
    {"[3045022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022100c869b0fe59e24e4db15bbec84c29828e24c2b6f02370eb32dfbe1ca77c98aa98] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_high_s, "CHECKDATASIG with High S"}, // flags: LOW_S,NULLFAIL,STRICTENC, expected: SIG_HIGH_S
    {"[3044022093cbfaab2bbef6fc019d1286018fd11e2612d817756d40913b3f82a428f1e90a022045d010bd0f3c31aa470d096bb3f693275214f48d4e727f45a9e0adb2645d7489] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIG with too little R padding but no DERSIG"}, // flags: NULLFAIL, expected: OK
    {"[3044022093cbfaab2bbef6fc019d1286018fd11e2612d817756d40913b3f82a428f1e90a022045d010bd0f3c31aa470d096bb3f693275214f48d4e727f45a9e0adb2645d7489] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::invalid_signature_encoding, "CHECKDATASIG with too little R padding"}, // flags: NULLFAIL,STRICTENC, expected: SIG_DER
    {"[304402205ffea7d9fa4e0ce8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIG with hybrid pubkey but no STRICTENC"}, // flags: NULLFAIL, expected: OK
    {"[304402205ffea7d9fa4e0ce8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, "CHECKDATASIG with hybrid pubkey"}, // flags: NULLFAIL,STRICTENC, expected: PUBKEYTYPE
    {"[304402205ffea7d9fa4e0de8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "CHECKDATASIG with invalid hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: OK
    {"[304402205ffea7d9fa4e0de8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, "CHECKDATASIG with invalid hybrid pubkey"}, // flags: NULLFAIL,STRICTENC, expected: PUBKEYTYPE
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "Standard CHECKDATASIGVERIFY"}, // flags: NULLFAIL,STRICTENC, expected: OK
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "CHECKDATASIGVERIFY with NULLFAIL flags"}, // flags: NULLFAIL,STRICTENC, expected: NULLFAIL
    {"[3044022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022037964f01a61db1b24ea44137b3d67d7095ec25f68bd7b508e01441e5539d96a9] 1", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKDATASIGVERIFY without NULLFAIL flags"}, // flags: STRICTENC, expected: CHECKDATASIGVERIFY
    {"0 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::op_check_data_sig_verify, "CHECKDATASIGVERIFY empty signature [KNUTH_OVERRIDE: KTH_OP_CHECKDATASIGVERIFY]"}, // flags: NULLFAIL,STRICTENC, expected: CHECKDATASIGVERIFY
    {"[3045022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022100c869b0fe59e24e4db15bbec84c29828e24c2b6f02370eb32dfbe1ca77c98aa98] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIG with High S but no Low S"}, // flags: NULLFAIL,STRICTENC, expected: OK
    {"[3045022021309a532a60d471cc4ef025a96572c7bf26c4640b53c7d45e411a5aa99980f6022100c869b0fe59e24e4db15bbec84c29828e24c2b6f02370eb32dfbe1ca77c98aa98] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_high_s, "CHECKDATASIG with High S"}, // flags: LOW_S,NULLFAIL,STRICTENC, expected: SIG_HIGH_S
    {"[3044022093cbfaab2bbef6fc019d1286018fd11e2612d817756d40913b3f82a428f1e90a022045d010bd0f3c31aa470d096bb3f693275214f48d4e727f45a9e0adb2645d7489] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIGVERIFY with too little R padding but no DERSIG"}, // flags: NULLFAIL, expected: OK
    {"[3044022093cbfaab2bbef6fc019d1286018fd11e2612d817756d40913b3f82a428f1e90a022045d010bd0f3c31aa470d096bb3f693275214f48d4e727f45a9e0adb2645d7489] 0", "[038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::invalid_signature_encoding, "CHECKDATASIGVERIFY with too little R padding"}, // flags: NULLFAIL,STRICTENC, expected: SIG_DER
    {"[304402205ffea7d9fa4e0ce8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "CHECKDATASIGVERIFY with hybrid pubkey but no STRICTENC"}, // flags: NULLFAIL, expected: OK
    {"[304402205ffea7d9fa4e0ce8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, "CHECKDATASIGVERIFY with hybrid pubkey"}, // flags: NULLFAIL,STRICTENC, expected: PUBKEYTYPE
    {"[304402205ffea7d9fa4e0de8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::invalid_script, "CHECKDATASIGVERIFY with invalid hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: CHECKDATASIGVERIFY
    {"[304402205ffea7d9fa4e0de8058c96edfb721aaf2840912080ac4ae86a92d9462e69fde302203567149cb9c8c343523a87d7a64be304983899d2e2c6f267a7136c48ff21908d] 0", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::pubkey_type, "CHECKDATASIGVERIFY with invalid hybrid pubkey"}, // flags: NULLFAIL,STRICTENC, expected: PUBKEYTYPE
    {"[0df4be7f5fe74b2855b92082720e889038e15d8d747334fa3f300ef4ab1db1eea56aa83d1d60809ff6703791736be87cfb6cbc5c4036aeed3b4ea4e6dab3509001]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "CHECKSIG Schnorr"}, // flags: NONE, expected: OK
    {"[0df4be7f5fe74b2855b92082720e889038e15d8d747334fa3f300ef4ab1db1eea56aa83d1d60809ff6703791736be87cfb6cbc5c4036aeed3b4ea4e6dab3509001]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKSIG Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: OK
    {"[3cf1b3f60b74d0821039f7dc7c21abe3119b9d94ae13f5e5258a8269bee9dfc51c84dbb3ba3eff82de61046f6cfef22ea5cf4a46e3776a5fb35d743aea310f6701]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKSIG Schnorr other key"}, // flags: STRICTENC, expected: OK
    {"[35b32856cbd89eb40130a50c6931ce002e3e9db033179ab6265a276a04795ec8f87e9b4d8343f399915371d7f4a7d4d0c97753f2473b253197695a58eede92de01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKSIG Schnorr mismatched key"}, // flags: STRICTENC, expected: OK
    {"[8d37e95e36718d7fbc8ffd63b0b4ebb89dd5fb683510a95345869399f810a8724a00e5cbbb2190205ffabac601d80bf110a67013521b7a7c02b2a51e07d723eb01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, "CHECKSIGVERIFY Schnorr"}, // flags: NONE, expected: OK
    {"[8d37e95e36718d7fbc8ffd63b0b4ebb89dd5fb683510a95345869399f810a8724a00e5cbbb2190205ffabac601d80bf110a67013521b7a7c02b2a51e07d723eb01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKSIGVERIFY Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: OK
    {"[53c4de3cb6a0190073c98fa415f63b720ea491748c9a760d5d249bb05f13990aa464be04b625cc36cd9e302dc276ad232d5d65d6327bdb18b7c529300d67013f01]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKSIGVERIFY Schnorr other key"}, // flags: STRICTENC, expected: OK
    {"[74c51dae216d8ebee5418204cf90839ea9a288b93eccb1de54b5b5d06bcd69c7028e1b3c6643c001403e7862bee6be39c107e5c8d0a9a8e9143862e5d73c555a01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKSIGVERIFY Schnorr mismatched key"}, // flags: STRICTENC, expected: CHECKSIGVERIFY
    {"[9db0671f61f1fafa84aaab76ad2e070b27cf9ae85338bafc0b947ac9ad8c56ff7b24aa76c95ad86bb13cbff314742dbe1f545869d1a28efa54b411ccd37717e5]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "CHECKDATASIG Schnorr"}, // flags: NONE, expected: OK
    {"[9db0671f61f1fafa84aaab76ad2e070b27cf9ae85338bafc0b947ac9ad8c56ff7b24aa76c95ad86bb13cbff314742dbe1f545869d1a28efa54b411ccd37717e5]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: OK
    {"[65c4bceda6ebb49e692180387e72c84be500f3431daac85d08d5d6c527e296f5b8a5b868a681f76aee309ad05e152b4f190732b3e7c46ef788b68c6035f6eab0]", "0 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG Schnorr other key"}, // flags: STRICTENC, expected: OK
    {"[65c4bceda6ebb49e692180387e72c84be500f3431daac85d08d5d6c527e296f5b8a5b868a681f76aee309ad05e152b4f190732b3e7c46ef788b68c6035f6eab0]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG Schnorr mismatched key"}, // flags: STRICTENC, expected: OK
    {"[e20d68eea1c55d8c23310ef33b4c68e3d876b1c5a36595f4dcc9d728894c957879e53bb4aebf8b3aa36861d89266ff864d2c3f513ab6f79c9d226ad45fbf5407]", "1 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG Schnorr other message"}, // flags: STRICTENC, expected: OK
    {"[e20d68eea1c55d8c23310ef33b4c68e3d876b1c5a36595f4dcc9d728894c957879e53bb4aebf8b3aa36861d89266ff864d2c3f513ab6f79c9d226ad45fbf5407]", "0 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIG Schnorr wrong message"}, // flags: STRICTENC, expected: OK
    {"[9db0671f61f1fafa84aaab76ad2e070b27cf9ae85338bafc0b947ac9ad8c56ff7b24aa76c95ad86bb13cbff314742dbe1f545869d1a28efa54b411ccd37717e5]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::success, "CHECKDATASIGVERIFY Schnorr"}, // flags: NONE, expected: OK
    {"[9db0671f61f1fafa84aaab76ad2e070b27cf9ae85338bafc0b947ac9ad8c56ff7b24aa76c95ad86bb13cbff314742dbe1f545869d1a28efa54b411ccd37717e5]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIGVERIFY Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: OK
    {"[65c4bceda6ebb49e692180387e72c84be500f3431daac85d08d5d6c527e296f5b8a5b868a681f76aee309ad05e152b4f190732b3e7c46ef788b68c6035f6eab0]", "0 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIGVERIFY Schnorr other key"}, // flags: STRICTENC, expected: OK
    {"[65c4bceda6ebb49e692180387e72c84be500f3431daac85d08d5d6c527e296f5b8a5b868a681f76aee309ad05e152b4f190732b3e7c46ef788b68c6035f6eab0]", "0 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKDATASIGVERIFY Schnorr mismatched key"}, // flags: STRICTENC, expected: CHECKDATASIGVERIFY
    {"[e20d68eea1c55d8c23310ef33b4c68e3d876b1c5a36595f4dcc9d728894c957879e53bb4aebf8b3aa36861d89266ff864d2c3f513ab6f79c9d226ad45fbf5407]", "1 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "CHECKDATASIGVERIFY Schnorr other message"}, // flags: STRICTENC, expected: OK
    {"[e20d68eea1c55d8c23310ef33b4c68e3d876b1c5a36595f4dcc9d728894c957879e53bb4aebf8b3aa36861d89266ff864d2c3f513ab6f79c9d226ad45fbf5407]", "0 [048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checkdatasigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::invalid_script, "CHECKDATASIGVERIFY Schnorr wrong message"}, // flags: STRICTENC, expected: CHECKDATASIGVERIFY
    {"0 [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::sig_badlength, "CHECKMULTISIG Schnorr w/ no STRICTENC"}, // flags: NONE, expected: SIG_BADLENGTH
    {"0 [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_badlength, "CHECKMULTISIG Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: SIG_BADLENGTH
    {"0 [833682d4f60cc916a22a2c263e658fa662c49badb1e2a8c6208987bf99b1abd740498371480069e7a7a6e7471bf78c27bd9a1fd04fb212a92017346250ac187b01] [ea4a8d20562a950f4695dc24804565482e9fa111704886179d0c348f2b8a15fe691a305cd599c59c131677146661d5b98cb935330989a85f33afc70d0a21add101] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::sig_badlength, "Schnorr 3-of-3"}, // flags: NONE, expected: SIG_BADLENGTH
    {"0 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [3044022031a1e5289b0d9c33ec182a7f67210b9997187c710f7d3f0f28bdfb618c4e025c02205d95fe63ee83a20ec44159a06f7c0b43b61d5f0c346ca4a2cc7b91878ad1a85001] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::no_rules, kth::error::sig_badlength, "Schnorr-ECDSA-mixed 3-of-3"} // flags: NONE, expected: SIG_BADLENGTH
};

/**
 * Script test chunk 16 (tests 1600 to 1699)
 */
bchn_script_test_list const script_tests_from_json_16{
    {"0 [17fa4dd3e62694cc7816d32b73d5646ea768072aea4926a09e159e5f57be8fd6523800b259fe2a12e27aa29a3719f19e9e4b99d7f8e465a6f19454f914ccb3ec01]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisigverify 1", kth::domain::machine::rule_fork::no_rules, kth::error::sig_badlength, "CHECKMULTISIGVERIFY Schnorr w/ no STRICTENC"}, // flags: NONE, expected: SIG_BADLENGTH
    {"0 [17fa4dd3e62694cc7816d32b73d5646ea768072aea4926a09e159e5f57be8fd6523800b259fe2a12e27aa29a3719f19e9e4b99d7f8e465a6f19454f914ccb3ec01]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisigverify 1", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_badlength, "CHECKMULTISIGVERIFY Schnorr w/ STRICTENC"}, // flags: STRICTENC, expected: SIG_BADLENGTH
    {"[6f1b69791cd7284b5510daef44cd5acd5c1f3d61f6a79705e18f106b46122f1ed8c5965f3c92c90943f9b51f57207e9e5b7fc462571281e2c92377e4ef20ab2b01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PK, bad sig"}, // flags: NONE, expected: OK
    {"[6f1b69791cd7284b5510daef44cd5acd5c1f3d61f6a79705e18f106b46122f1ed8c5965f3c92c90943f9b51f57207e9e5b7fc462571281e2c92377e4ef20ab2b01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Schnorr P2PK, bad sig STRICTENC"}, // flags: STRICTENC, expected: OK
    {"[6f1b69791cd7284b5510daef44cd5acd5c1f3d61f6a79705e18f106b46122f1ed8c5965f3c92c90943f9b51f57207e9e5b7fc462571281e2c92377e4ef20ab2b01]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "Schnorr P2PK, bad sig NULLFAIL"}, // flags: NULLFAIL, expected: NULLFAIL
    {"[4463c103b21e76713571365c4c09224c2a1b343b3cf02e3b56f4f0890a6e7ff96d0bfa2ffa22f8067db3414cc1789abfc48638cb4bc7463907042975f4c84ece01] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508]", "dup hash160 [1018853670f9f3b0582c5b9ee8ce93764ac32b93] equalverify checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PKH"}, // flags: NONE, expected: OK
    {"[d78d543b601bc93b394b5c669933d16d860dc7480383efcaae9521d6ceb4065ba17c02a6d9289efef762fa7a0482eff9c5bce4dd95f8bea421ee70bdd8d5488d01]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Schnorr P2PK with compressed pubkey"}, // flags: STRICTENC, expected: OK
    {"[0df4be7f5fe74b2855b92082720e889038e15d8d747334fa3f300ef4ab1db1eea56aa83d1d60809ff6703791736be87cfb6cbc5c4036aeed3b4ea4e6dab3509001]", "[0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Schnorr P2PK with uncompressed pubkey"}, // flags: STRICTENC, expected: OK
    {"[d211631fdebf4c8376b3d169ef65a1987460eda43c3312e561b0226fa3069f68a68bac0dbf780f77dd60ff602c66186f1da2bb0a31f10187796242f48295ddbe01]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::pubkey_type, "Schnorr P2PK with hybrid pubkey"}, // flags: STRICTENC, expected: PUBKEYTYPE
    {"[d211631fdebf4c8376b3d169ef65a1987460eda43c3312e561b0226fa3069f68a68bac0dbf780f77dd60ff602c66186f1da2bb0a31f10187796242f48295ddbe01]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PK with hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: OK
    {"[078b6b4e7d0689f3a1ef9b5283039c39b7ab3a26c04143017ee7136edbc1ccbcf47173c92c5823b778e4aaba3bf9ef2e988eb54c4cb709dbfa8e62110843c19901]", "[0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] checksig not", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PK NOT with damaged hybrid pubkey but no STRICTENC"}, // flags: NONE, expected: OK
    {"[a522c6aab80595e0fdaf473c89a32e97978858809949fafd6f851254daae231f45338fe53187f79d8507f08c08f8bd2ee795e6ccaca0a04c4e40c613395a685b05]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "Schnorr P2PK with undefined basehashtype and STRICTENC"}, // flags: STRICTENC, expected: SIG_HASHTYPE
    {"[128f02ec5b36057a7f3793c5ffdef9e6cca0ea3200a2f07e5c7189a267daafc4feb2b65a8c7f22b203557fef4c078e98382dc99939666b7c6dbcc62bd25b0bf821] [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8]", "dup hash160 [91b24bf9f5288532960ac687abb035127b1d28a5] equalverify checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PKH with invalid sighashtype but no STRICTENC"}, // flags: NONE, expected: OK
    {"[128f02ec5b36057a7f3793c5ffdef9e6cca0ea3200a2f07e5c7189a267daafc4feb2b65a8c7f22b203557fef4c078e98382dc99939666b7c6dbcc62bd25b0bf821] [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8]", "dup hash160 [91b24bf9f5288532960ac687abb035127b1d28a5] equalverify checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_hashtype, "Schnorr P2PKH with invalid sighashtype and STRICTENC"}, // flags: STRICTENC, expected: SIG_HASHTYPE
    {"[3dae009b3fc84066b644b0508d1cc68fbbdefbb91b049aaa46e8de5c3b4598707d93df80a275022354f8e3e65ca6561c55d2f626c8395d237fb1f2b6b93e83f081]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::success, "Schnorr P2PK anyonecanpay"}, // flags: NONE, expected: OK
    {"[3dae009b3fc84066b644b0508d1cc68fbbdefbb91b049aaa46e8de5c3b4598707d93df80a275022354f8e3e65ca6561c55d2f626c8395d237fb1f2b6b93e83f001]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::no_rules, kth::error::stack_false, "Schnorr P2PK anyonecanpay marked with normal hashtype"}, // flags: NONE, expected: EVAL_FALSE
    {"[d06f2e8e262a974d330c185acdd2eed99622f2c9cc0980eacf976f37965186dba2564a564e8c6697127d9729e6cedd44060ab7ece5c2f0ded2ad3f9a7308c7ce41]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "Schnorr P2PK with fork sig"}, // flags: SIGHASH_FORKID,STRICTENC, expected: OK
    {"[3cf1b3f60b74d0821039f7dc7c21abe3119b9d94ae13f5e5258a8269bee9dfc51c84dbb3ba3eff82de61046f6cfef22ea5cf4a46e3776a5fb35d743aea310f6701]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::missing_forkid, "Schnorr P2PK with non-fork sig"}, // flags: SIGHASH_FORKID,STRICTENC, expected: MISSING_FORKID
    {"[3cf1b3f60b74d0821039f7dc7c21abe3119b9d94ae13f5e5258a8269bee9dfc51c84dbb3ba3eff82de61046f6cfef22ea5cf4a46e3776a5fb35d743aea310f6741]", "[048282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f5150811f8a8098557dfe45e8256e830b60ace62d613ac2f7b17bed31b6eaff6e26caf] checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "Schnorr P2PK with cheater fork bit"}, // flags: SIGHASH_FORKID,STRICTENC, expected: EVAL_FALSE
    {"[300602010702010701] [03a7bcb86f12d0635c850b6f0c945e4b4fcb400092a74b8d7e83275eb562d9fbb6]", "checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "recovered-pubkey CHECKSIG 7,7 (wrapped r)"}, // flags: STRICTENC, expected: OK
    {"[303d021d776879206d757374207765207375666665722077697468206563647361021c2121212121212121212121212121212121212121212121212121212101] [02da78d331f65fd308ed7afbc80d48c908a83050276cb9bdc1fa414bfea4511570]", "checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "recovered-pubkey CHECKSIG with 63-byte DER"}, // flags: STRICTENC, expected: OK
    {"[303e021d776879206d757374207765207375666665722077697468206563647361021d212121212121212121212121212121212121212121212121212121212101] [03f5d556a48a11a677f1a8eb0771f6cd11b1bcf378478c586d54f18634521b833e]", "checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::stack_false, "recovered-pubkey CHECKSIG with 64-byte DER"}, // flags: STRICTENC, expected: EVAL_FALSE
    {"[303f021d776879206d757374207765207375666665722077697468206563647361021e21212121212121212121212121212121212121212121212121212121212101] [02224d851056412fbe03d1e2e8ec9030f5ee99c7b403e9743f261625eb8dd22922]", "checksig", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, "recovered-pubkey CHECKSIG with 65-byte DER"}, // flags: STRICTENC, expected: OK
    {"0 [303e021d776879206d757374207765207375666665722077697468206563647361021d212121212121212121212121212121212121212121212121212121212101] [036cd1f91735dda2d984cfbc17ab0e9e7d754d7e4e1fceb691751cdd5c26b0aecc]", "1 swap 1 checkmultisig", kth::domain::machine::rule_fork::bch_uahf, kth::error::sig_badlength, "recovered-pubkey CHECKMULTISIG with 64-byte DER"}, // flags: STRICTENC, expected: SIG_BADLENGTH
    {"0 [30440220379db2a13c8c679bda7f500c93bae36e0d75c96b8af1754316a9c80e843f2923022046ee947cd3b916ab78f95be6ae29cc2f037ddb47f8e59c966b1e6813b270648001]", "1 [0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 2 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "1-of-2 with unchecked hybrid pubkey with SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0 [304402202d4dd56f6dee2eccc049a4ebd65e77e4325aa3f2940130412b9f96fd28fa8c5a022074963950dd250d2fc943e60c7a4133cc8b1fcc46253e9b19f71c9723d1d894ef01]", "1 [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [0679be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 2 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::pubkey_type, "1-of-2 with checked hybrid pubkey with SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: PUBKEYTYPE
    {"0 [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "Legacy 1-of-1 Schnorr w/ SCHNORR_MULTISIG but no STRICTENC"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG, expected: SIG_BADLENGTH
    {"0 [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "Legacy 1-of-1 Schnorr w/ SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_BADLENGTH
    {"0 [833682d4f60cc916a22a2c263e658fa662c49badb1e2a8c6208987bf99b1abd740498371480069e7a7a6e7471bf78c27bd9a1fd04fb212a92017346250ac187b01] [ea4a8d20562a950f4695dc24804565482e9fa111704886179d0c348f2b8a15fe691a305cd599c59c131677146661d5b98cb935330989a85f33afc70d0a21add101] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "Legacy 3-of-3 Schnorr w/ SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_BADLENGTH
    {"0 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [3044022031a1e5289b0d9c33ec182a7f67210b9997187c710f7d3f0f28bdfb618c4e025c02205d95fe63ee83a20ec44159a06f7c0b43b61d5f0c346ca4a2cc7b91878ad1a85001] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "Legacy 3-of-3 mixed Schnorr-ECDSA w/ SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_BADLENGTH
    {"0 [303e021d776879206d757374207765207375666665722077697468206563647361021d212121212121212121212121212121212121212121212121212121212101] [036cd1f91735dda2d984cfbc17ab0e9e7d754d7e4e1fceb691751cdd5c26b0aecc]", "1 swap 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "recovered-pubkey CHECKMULTISIG with 64-byte DER w/ SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_BADLENGTH
    {"0 0 0", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig not", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 w/ SCHNORR_MULTISIG (return-false still valid via legacy mode)"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0", "0 0 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 0-of-0 w/ SCHNORR_MULTISIG"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0", "0 [beef] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 0-of-1 w/ SCHNORR_MULTISIG, null dummy"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"1 [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 1-of-1 Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[0100] [105e4fed395e64ca013ac1ce020ef69b9990a577fe4b74648faafb69e499f76dd53d5c64aa866924361dd3aadde9b7184bbcb4f79520396c9ed17c4d8489a59701]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 1-of-1 Schnorr, nonminimal bits"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BITFIELD_SIZE
    {"7 [833682d4f60cc916a22a2c263e658fa662c49badb1e2a8c6208987bf99b1abd740498371480069e7a7a6e7471bf78c27bd9a1fd04fb212a92017346250ac187b01] [ea4a8d20562a950f4695dc24804565482e9fa111704886179d0c348f2b8a15fe691a305cd599c59c131677146661d5b98cb935330989a85f33afc70d0a21add101] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 3-of-3 Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"15 [6d1d1f8db2b4e43a51b4f5f16367045f9c1832b8103ce2bab3a21afc223351a685e84bca02c863110e7ad22d4735cd31e1908e741530e3a07863292c3ff21df901] [6d1d1f8db2b4e43a51b4f5f16367045f9c1832b8103ce2bab3a21afc223351a685e84bca02c863110e7ad22d4735cd31e1908e741530e3a07863292c3ff21df901] [98ac816c0adb14db2a593f6402bf70e347a219fba0ccad805a5e37a67e265c800b1d493440965634fe740852fbe4164ec45a722a463099ae8a9872896da972ef01] [e62867a73ae91b390f6ae11717d360fbc7e813a6dc3917040556834350d5b3cfb464e7222a716f37a32ba3fee353ad4957aa3f47cf7ea1285873bc9a0e64759801]", "4 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 4-of-3 Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_COUNT
    {"6 [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001] [a3fac6b1aa2cc8bb222ee379711c243b26a164ced7f2e2f4a0e184a3e53352bfaee598c6446861e5a7e819871b7ce948f5562ecf214c19dfa5112c19d74adc7101]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 (110) Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"5 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [a3fac6b1aa2cc8bb222ee379711c243b26a164ced7f2e2f4a0e184a3e53352bfaee598c6446861e5a7e819871b7ce948f5562ecf214c19dfa5112c19d74adc7101]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 (101) Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"3 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 (011) Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"3 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [a3fac6b1aa2cc8bb222ee379711c243b26a164ced7f2e2f4a0e184a3e53352bfaee598c6446861e5a7e819871b7ce948f5562ecf214c19dfa5112c19d74adc7101]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nullfail, "CHECKMULTISIG 2-of-3 Schnorr, mismatched bits Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: NULLFAIL
    {"7 [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001] [a3fac6b1aa2cc8bb222ee379711c243b26a164ced7f2e2f4a0e184a3e53352bfaee598c6446861e5a7e819871b7ce948f5562ecf214c19dfa5112c19d74adc7101]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-3 Schnorr, all bits set"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: INVALID_BIT_COUNT
    {"14 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-3 Schnorr, extra high bit set"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BIT_RANGE
    {"10 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-3 Schnorr, too high bit set"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BIT_RANGE
    {"2 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-3 Schnorr, too few bits set"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: INVALID_BIT_COUNT
    {"[00] 0 0", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-3 Schnorr, with no bits set (attempt to malleate return-false)"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: INVALID_BIT_COUNT
    {"0 [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101] [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_badlength, "CHECKMULTISIG null dummy with schnorr sigs (with SCHNORR_MULTISIG on)"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_BADLENGTH
    {"3 [4829a4df8042001ba6a3ee6ec1e9c26de662aade55a49f8d018acff0bac0df2edd881cf21f2a1aa651b6f703f0cde22fedaa9b8bc3451101bcac37bcb89dcc8001] [b8c744ebecfa75ef478b2b8852795d93b99640e5c05421ef6f651d4044be6cf2db2c61d0b5395e685731556c87e501433fb6e6a3b44c113d0ba19df8fd01c18101]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nullfail, "CHECKMULTISIG 2-of-3 Schnorr, misordered signatures"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: NULLFAIL
    {"-1 [34b9918c9cd9aa3ad0f642f1dd435164e7bed979e539b2890b8d465b70db47385c514a21b1c6659ab40b8e5395ceae8f6c67c6f4c24e940cbe0a6fe496e7d15e01] [057e3e62dbcf8a1bd3526271ad63f568bdae4d248bd87dfcf4a80b9f6da28e0994e04c670a56e624301141f53efc9a40f7d503171809e3f70d5aee203a90567d01]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] dup 2dup 2dup [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 8 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-8 Schnorr, right way to represent 0b10000001"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[8100] [34b9918c9cd9aa3ad0f642f1dd435164e7bed979e539b2890b8d465b70db47385c514a21b1c6659ab40b8e5395ceae8f6c67c6f4c24e940cbe0a6fe496e7d15e01] [057e3e62dbcf8a1bd3526271ad63f568bdae4d248bd87dfcf4a80b9f6da28e0994e04c670a56e624301141f53efc9a40f7d503171809e3f70d5aee203a90567d01]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] dup 2dup 2dup [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 8 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 2-of-8 Schnorr, wrong way to represent 0b10000001"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BITFIELD_SIZE
    {"[20] [a07b665bb4c5385afb0b0369ee675c5e890c9b9b63962aea5d9fcaeb0fc427e1c13b76baff5bedcf90f61070b3c62b976ffeaf4c6701ee37ae764a1e171b8afc01]", "1 -1 -1 -1 -1 -1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] -1 7 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 1-of-7 Schnorr, second-to-last key"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[0004] [9d831f1ffb45eb4578098e9df80374e4c9172aa99fe3d7bb226feafa5ba280ed2e02834aeef31ee91d12e84afb2701ae6f88821be008741b2c689a779ff3276101]", "1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] -1 -1 13 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 1-of-13 Schnorr, third-to-last key"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[ffff0f] [5f4f7ffff1fa44347dbc148d51fe661b60fbc44b5abe22a04d97ecfb891bdb2568eb91aa38a426dfd7b82e0b45b57af6f3e56143fabc850ebfdfcf6a0d03c4b001] [28add881f6fa489a9016f066022ed80acecbafc351a6d17025cf87bf2b0d9b09f6d001f5df2b1be0d1909a8fe9280b59b5a8cb6d1e37425e733ff3267ce1e1b401] [9f66f20bca4a64fc3cc170c3e1ef2bcb62f86b719b2f8e7154407e9210fde99164d754f787bf65020dc7db3838c8aae3591eaf465f45433fcb3d0a94bea4090401]", "over dup dup 2dup 3dup 3dup 3dup 3dup [14] [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] over dup dup 2dup 3dup 3dup 3dup 3dup [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 20-of-20 Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[000010] [5f4f7ffff1fa44347dbc148d51fe661b60fbc44b5abe22a04d97ecfb891bdb2568eb91aa38a426dfd7b82e0b45b57af6f3e56143fabc850ebfdfcf6a0d03c4b001] [28add881f6fa489a9016f066022ed80acecbafc351a6d17025cf87bf2b0d9b09f6d001f5df2b1be0d1909a8fe9280b59b5a8cb6d1e37425e733ff3267ce1e1b401] [9f66f20bca4a64fc3cc170c3e1ef2bcb62f86b719b2f8e7154407e9210fde99164d754f787bf65020dc7db3838c8aae3591eaf465f45433fcb3d0a94bea4090401]", "over dup dup 2dup 3dup 3dup 3dup 3dup [14] [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] over dup dup 2dup 3dup 3dup 3dup 3dup [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 20-of-20 Schnorr, checkbits +1"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BIT_RANGE
    {"[000010] [be6635b93b92ff98d0d929d3537bf2941bbc33b9b4adc14d928e1b6eae1aa1fd23455e4094764f70dd896f47b3279b57965ecad1b4d32c1000be27be8cf83ece01]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] dup [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] 3dup 3dup 3dup 3dup 3dup 3dup [15] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 1-of-21 Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: PUBKEY_COUNT
    {"[010000] [020fb4d03e4c7ea6e17f5a7d03c74a6cc33fd7f949d6bc5eb8f6122a4fa5a9bc208adafc7a92030fea664f044cccfd473b8ebaae94cd7bdeec8346a02ac0e3df01]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] dup 2dup 3dup 3dup 3dup 3dup 3dup [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 1-of-20 Schnorr, first key"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[000001] [020fb4d03e4c7ea6e17f5a7d03c74a6cc33fd7f949d6bc5eb8f6122a4fa5a9bc208adafc7a92030fea664f044cccfd473b8ebaae94cd7bdeec8346a02ac0e3df01]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] dup 2dup 3dup 3dup 3dup 3dup 3dup [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nullfail, "CHECKMULTISIG 1-of-20 Schnorr, first key, wrong endianness"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: NULLFAIL
    {"1 [f92f753aa4238dfce5e2c68a5bb9f04c4a457c775ef247a2fda9318d2b6ab086cf7d116a24f38a045d179ff59d84c872e9fc2ef0f7574306c22380cd18da411301]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] 2dup 2dup 3dup 3dup 3dup 3dup 3dup [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 1-of-20 Schnorr, truncating zeros not allowed"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BITFIELD_SIZE
    {"[000008] [dbcb269b86b9b0abb01fdf0a38b3ba9852150188bb2255e4a0794ecbbdcba52a6577ca08e0dd89b9543cae1bcf152e5444ee70759bc58a553a4c1e41144fc27001]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] dup 2dup 3dup 3dup 3dup 3dup 3dup [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 1-of-20 Schnorr, last key"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[080000] [dbcb269b86b9b0abb01fdf0a38b3ba9852150188bb2255e4a0794ecbbdcba52a6577ca08e0dd89b9543cae1bcf152e5444ee70759bc58a553a4c1e41144fc27001]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] dup 2dup 3dup 3dup 3dup 3dup 3dup [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nullfail, "CHECKMULTISIG 1-of-20 Schnorr, last key, wrong endianness"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: NULLFAIL
    {"[0800] [dbcb269b86b9b0abb01fdf0a38b3ba9852150188bb2255e4a0794ecbbdcba52a6577ca08e0dd89b9543cae1bcf152e5444ee70759bc58a553a4c1e41144fc27001]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] dup 2dup 3dup 3dup 3dup 3dup 3dup [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 1-of-20 Schnorr, last key, truncating zeros not allowed"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BITFIELD_SIZE
    {"6 [3dfd744897afdad123edf671b9f7ad99f884cf714ada36f4c1cba12f7bdc71c6369832821a91964eb7d05552f1e32e5506d6dc2262e3ca5d1238690e3ddb075401] [84aa5be4ebc905d6390a5be1b0101a0f2aab9563603a52c864db5deb459266cf1ea4376d378aa69a907a4e255919ecbff044d21b76dd004d34266c7ad8b84af501]", "2 [beef] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 (110) Schnorr, first key garbage"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"3 [b7aa94db79ecf22bdfd4d986ac4ba294fc044dd75696a6f95b12fef2dee48e02e06ccda571e0d513a03c585f86a8df1223a02556c11d7f1da71ba10544e9bc9501] [3dfd744897afdad123edf671b9f7ad99f884cf714ada36f4c1cba12f7bdc71c6369832821a91964eb7d05552f1e32e5506d6dc2262e3ca5d1238690e3ddb075401]", "2 [beef] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::pubkey_type, "CHECKMULTISIG 2-of-3 (011) Schnorr, first key garbage"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: PUBKEYTYPE
    {"3 [c09a62bbf5b7e45503a682cc72559b4d060edf1144023b47e765375a8e0cb017da3b0b72e2912a772989ce922f7f298890e66ff9f36c813b872682fc6b95a04b01] [7e57eab163176c372e6f2baa2446998817b429d9cd8e3ee3a6841a95bf19fa128562848b590bf2ada480a23bbb6697f3e920594e7bf410b3b8af489862ee660601]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [beef] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 2-of-3 (011) Schnorr, last key garbage"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"6 [7e57eab163176c372e6f2baa2446998817b429d9cd8e3ee3a6841a95bf19fa128562848b590bf2ada480a23bbb6697f3e920594e7bf410b3b8af489862ee660601] [64b53fe45bc169d53366fe20f7340596ba9ad86566ae725535e32c5a40d4d3dbd448bc898d0528395eca1c634e4ce422c856c6063cb947d8191127317da4a5a001]", "2 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [beef] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::pubkey_type, "CHECKMULTISIG 2-of-3 (110) Schnorr, last key garbage"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: PUBKEYTYPE
    {"[00]", "0 0 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "CHECKMULTISIG 0-of-0 with SCHNORR_MULTISIG, dummy must be null"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: BITFIELD_SIZE
    {"[00]", "0 [beef] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "CHECKMULTISIG 0-of-1 with SCHNORR_MULTISIG, dummy need not be null"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"1 [17fa4dd3e62694cc7816d32b73d5646ea768072aea4926a09e159e5f57be8fd6523800b259fe2a12e27aa29a3719f19e9e4b99d7f8e465a6f19454f914ccb3ec01]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisigverify 1", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "OP_CHECKMULTISIGVERIFY Schnorr"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"1 [304402204d0106d6babcaca95277692eaa566bdc89d9f44b1106c18423345c7e9ac40d79022033a3750421038d15f15101ffdca1147a0eb980bb1b809280cb5368c50c10c42c01]", "1 [0479be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8] 1 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nonschnorr, "CHECKMULTISIG 1-of-1 ECDSA signature in Schnorr mode"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_NONSCHNORR
    {"7 [304402204d69d5caa4dbab259f79fce89d3b459bbd91697c1c052a1554ff3b08b2241cbd0220330a8e17a90d51996e363cb8902fce6278c6350fa59ae12832db2f6a44d64dce01] [ea4a8d20562a950f4695dc24804565482e9fa111704886179d0c348f2b8a15fe691a305cd599c59c131677146661d5b98cb935330989a85f33afc70d0a21add101] [ce9011d76a4df05d6280b2382b4d91490dbec7c3e72dc826be1fc9b4718f627955190745cac96521ea46d6d324c7376461e225310e6cd605b9f266d170769b7901]", "3 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::sig_nonschnorr, "CHECKMULTISIG 3-of-3 Schnorr with mixed-in ECDSA signature"}, // flags: MINIMALDATA,NULLFAIL,SCHNORR_MULTISIG,STRICTENC, expected: SIG_NONSCHNORR
    {"0 [3044022031feb7bc6e213668042b34749aa7aa99a4b40dc8ba53f872fb270442a2e69ecf0220012df792d5bc247a4ebe12f4de300d70aa768f5d9f49a5db752aef86e23f1bd501]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] 3 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on bare CHECKMULTISIG 1-of-3 ECDSA"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[010000] [071da33a41ccc6ff5fcb275cae36e953e6a6092af5c8d256ca640387d36c42ae25117b502bae0881437ad0ff7db7ab23afba59fbcb03563471941733e67fb1ac01]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 -1 [14] checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on bare CHECKMULTISIG 1-of-20 Schnorr"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[d78d543b601bc93b394b5c669933d16d860dc7480383efcaae9521d6ceb4065ba17c02a6d9289efef762fa7a0482eff9c5bce4dd95f8bea421ee70bdd8d5488d01]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on P2PK Schnorr"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[30440220355bca231b5c6eac1c304ece6391404a5e8c9683f1c20749736e56f35969954302205d4af886536352ddf065407828763f3fc41755c34fd08afa4934505fd75e14ed01]", "[0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] checksig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on P2PK ECDSA"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0 [3044022007a5b776fff5a7540301e9abbe29198575ce549333b47c90e2949c08d4ecaf8002203f8d71618b7a5cd3e99eeba0fb3c4cad4115e550ea27563d65074899d074a2d301] 0x4d0102 0x51210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f8179821038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff46405fae", "hash160 [f12c94c5293b608240ce28dc992a3ee1110926c1] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on P2SH CHECKMULTISIG 1-of-15 ECDSA with compressed keys"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f817980078ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac78ac75]", "hash160 [e35559f8e010a9efb62973594c029ddc6f52b031] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "Null signatures make no SigChecks (CHECKSIG)"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[00210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798000070ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba70ba6d77]", "hash160 [0c01c2ac37ed03194c339b0f9915ae6acd0afeba] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "Null signatures make no SigChecks (CHECKDATASIG)"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0 0 [766e6f6f6f6f60210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798766e6f6f6f6f60ae91]", "hash160 [653b2bb5171f68633c8e3437d075e23640fdf4d1] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "Null signatures make no SigChecks (CHECKMULTISIG)"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0 [304402200c90f014caff5716cd50749d52c5a4b5f1d6b79c67c96f71713f6fa3e84f3a8a02203ab681add2f4f908c41f083c982d24b62871e6c4f6eaa5d677572fc7c43c5e2201]", "1 [0279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798] [038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f51508] [03363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff4640] dup 4 checkmultisig", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "SigChecks on bare CHECKMULTISIG 1-of-4 ECDSA"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: INPUT_SIGCHECKS
    {"0 [30440220418df29b58de691affb63165b3276a3932e80d56eb3392114b438f4f61fc9d2902207270492c16a9c56be78f6d926448038de44dd0de7d4ecea61f04f590be8c321401] 0x4de001 0x514f210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f817982103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff46405fae", "hash160 [5493830eb1862c448c66236c694e2b3b37d8fd60] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::invalid_script, "SigChecks on P2SH CHECKMULTISIG 1-of-15 ECDSA with a runt key"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: INPUT_SIGCHECKS
    {"[0200] [c33a73f6c920c95d79d4abf75a3471daac41cfb76a40a40eac752e0c522d1ce7be10d486c52aad12b1a1802f78f860d20dc6be065bebb672e641d17edc7ea7d501] 0x4de001 0x514f210279be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f817982103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff464021038282263212c609d9ea2a6e3e172de238d8c39cabd5ac1ca10646e23fd5f515082103363d90d447b00c9c99ceac05b6262ee053441c7e55552ffe526bad8f83ff46405fae", "hash160 [5493830eb1862c448c66236c694e2b3b37d8fd60] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "SigChecks on P2SH CHECKMULTISIG 1-of-15 Schnorr with a runt key"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"0 [004f4f4f4f4f4f4f4f4f4f5aae]", "hash160 [a3ef8a4b54cc4437e684c5072535acbbf1f29598] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "Very short P2SH multisig 0-of-10, spent with legacy mode (0 sigchecks)"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"[0000] [004f4f4f4f4f4f4f4f4f4f5aae]", "hash160 [a3ef8a4b54cc4437e684c5072535acbbf1f29598] equal", kth::domain::machine::rule_fork::bch_mersenne, kth::error::success, "Very short P2SH multisig 0-of-10, spent with schnorr mode (0 sigchecks)"}, // flags: INPUT_SIGCHECKS,MINIMALDATA,NULLFAIL,P2SH,SCHNORR_MULTISIG,STRICTENC, expected: OK
    {"", "checksequenceverify", kth::domain::machine::rule_fork::bip112_rule, kth::error::invalid_script, "CSV automatically fails on an empty stack"}, // flags: CHECKSEQUENCEVERIFY, expected: INVALID_STACK_OPERATION
    {"-1", "checksequenceverify", kth::domain::machine::rule_fork::bip112_rule, kth::error::negative_locktime, "CSV automatically fails if stack top is negative"}, // flags: CHECKSEQUENCEVERIFY, expected: NEGATIVE_LOCKTIME
    {"0x0100", "checksequenceverify", kth::domain::machine::rule_fork::bch_mersenne, kth::error::minimal_number, "CSV fails if stack top is not minimally encoded"}, // flags: CHECKSEQUENCEVERIFY,MINIMALDATA, expected: MINIMALNUM
    {"0", "checksequenceverify", kth::domain::machine::rule_fork::bip112_rule, kth::error::unsatisfied_locktime, "CSV fails if stack top bit 1 << 31 is set and the tx version < 2"}, // flags: CHECKSEQUENCEVERIFY, expected: UNSATISFIED_LOCKTIME
    {"4294967296", "checksequenceverify", kth::domain::machine::rule_fork::bip112_rule, kth::error::unsatisfied_locktime, "CSV fails if stack top bit 1 << 31 is not set, and tx version < 2"}, // flags: CHECKSEQUENCEVERIFY, expected: UNSATISFIED_LOCKTIME
    {"1", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH,MINIMALIF, expected: OK
    {"2", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"2", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"[0100]", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"[0100]", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"0", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH,MINIMALIF, expected: EVAL_FALSE
    {"[00]", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"[00]", "if 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"1", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH,MINIMALIF, expected: EVAL_FALSE
    {"2", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"2", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""} // flags: P2SH,MINIMALIF, expected: MINIMALIF
};

/**
 * Script test chunk 17 (tests 1700 to 1733)
 */
bchn_script_test_list const script_tests_from_json_17{
    {"[0100]", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"[0100]", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"0", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH,MINIMALIF, expected: OK
    {"[00]", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"[00]", "notif 1 endif", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"1 [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH,MINIMALIF, expected: OK
    {"2 [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"2 [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"[0100] [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"[0100] [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"0 [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH,MINIMALIF, expected: EVAL_FALSE
    {"[00] [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"[00] [635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"[635168]", "hash160 [e7309652a8e3f600f06f5d8d52d6df03d2176cc3] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::invalid_stack_scope, ""}, // flags: P2SH,MINIMALIF, expected: UNBALANCED_CONDITIONAL
    {"1 [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH,MINIMALIF, expected: EVAL_FALSE
    {"2 [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"2 [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"[0100] [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::stack_false, ""}, // flags: P2SH, expected: EVAL_FALSE
    {"[0100] [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"0 [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH,MINIMALIF, expected: OK
    {"[00] [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::success, ""}, // flags: P2SH, expected: OK
    {"[00] [645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::minimalif, ""}, // flags: P2SH,MINIMALIF, expected: MINIMALIF
    {"[645168]", "hash160 [0c3f8fe3d6ca266e76311ecda544c67d15fdd5b0] equal", kth::domain::machine::rule_fork::bip16_rule, kth::error::invalid_stack_scope, ""}, // flags: P2SH,MINIMALIF, expected: UNBALANCED_CONDITIONAL
    {"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66 and NULLFAIL-compliant"}, // flags: DERSIG, expected: OK
    {"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "BIP66 and NULLFAIL-compliant"}, // flags: DERSIG,NULLFAIL, expected: OK
    {"1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::success, "BIP66 and NULLFAIL-compliant"}, // flags: DERSIG,NULLFAIL, expected: OK
    {"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 [300602010102010101]", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66-compliant but not NULLFAIL-compliant"}, // flags: DERSIG, expected: OK
    {"0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 [300602010102010101]", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "BIP66-compliant but not NULLFAIL-compliant"}, // flags: DERSIG,NULLFAIL, expected: NULLFAIL
    {"0 [300602010102010101] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bip66_rule, kth::error::success, "BIP66-compliant but not NULLFAIL-compliant"}, // flags: DERSIG, expected: OK
    {"0 [300602010102010101] 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0", "[14] 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 1 [14] checkmultisig not", kth::domain::machine::rule_fork::bch_daa_cw144, kth::error::sig_nullfail, "BIP66-compliant but not NULLFAIL-compliant"}, // flags: DERSIG,NULLFAIL, expected: NULLFAIL
    {"[300602010102010141]", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::illegal_forkid, ""}, // flags: STRICTENC, expected: ILLEGAL_FORKID
    {"[300602010102010141]", "[02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] checksig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""}, // flags: SIGHASH_FORKID, expected: OK
    {"0 [300602010102010141]", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::illegal_forkid, ""}, // flags: STRICTENC, expected: ILLEGAL_FORKID
    {"0 [300602010102010141]", "1 [02865c40293a680cb9c020e7b1e106d8c1916d3cef99aa431a56d253e69256dac0] 1 checkmultisig not", kth::domain::machine::rule_fork::bch_uahf, kth::error::success, ""} // flags: SIGHASH_FORKID, expected: OK
};


/*
SKIPPED TESTS:
The following tests were skipped during generation:

 * Test 1:
 *   script_sig: "0x4c01"
 *   script_pub_key: "0x01 NOP"
 *   reason: Knuth fails at parsing stage (OP_PUSHDATA1 with missing data), BCHN fails at execution stage
 *
 * Test 2:
 *   script_sig: "0x4d0200ff"
 *   script_pub_key: "0x01 NOP"
 *   reason: ...
 *
 * Test 3:
 *   script_sig: "0x4e03000000ffff"
 *   script_pub_key: "0x01 NOP"
 *   reason: 
 *
 * Test 4:
 *   script_sig: "0x41 0x1052a02f76cf6c3bc43e4671af4c6065f6d918c79f58ad7f90a49c4e2603a4728fc6d81d49087dd97bc43ae184da55dc5195553526ce076df092be27a398b68f41"
 *   script_pub_key: "0x21 0xa8f459fbe84e0568ff03454d438183b0464e0c18ff73658ec0d2619b20ff29e502 REVERSEBYTES CHECKSIG"
 *   reason: Complex Schnorr signature + NULLFAIL case - BCHN expects success but signature appears invalid, needs investigation of NULLFAIL handling
 *
 * Test 5:
 *   script_sig: "1"
 *   script_pub_key: "NOP1"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 6:
 *   script_sig: "1"
 *   script_pub_key: "NOP4"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 7:
 *   script_sig: "1"
 *   script_pub_key: "NOP5"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 8:
 *   script_sig: "1"
 *   script_pub_key: "NOP6"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 9:
 *   script_sig: "1"
 *   script_pub_key: "NOP7"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 10:
 *   script_sig: "1"
 *   script_pub_key: "NOP8"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 11:
 *   script_sig: "1"
 *   script_pub_key: "NOP9"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 12:
 *   script_sig: "1"
 *   script_pub_key: "NOP10"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 13:
 *   script_sig: "NOP10"
 *   script_pub_key: "1"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 14:
 *   script_sig: "1 0x01 0xb9"
 *   script_pub_key: "HASH160 0x14 0x15727299b05b45fdaf9ac9ecf7565cfe27c3e567 EQUAL"
 *   reason: SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS is a standardness rule, not consensus - Knuth doesn't support this
 *
 * Test 15:
 *   script_sig: "1"
 *   script_pub_key: "0x61616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161"
 *   reason: Estos son multiples opcodes 0x61, tengo que ver cómo encodearlos en Knuth
 *
 * Test 16:
 *   script_sig: "0"
 *   script_pub_key: "IF 0x6161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161616161 ENDIF 1"
 *   reason: Estos son multiples opcodes 0x61, tengo que ver cómo encodearlos en Knuth
 *
 * Test 17:
 *   script_sig: "1 2 3 4 5 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f"
 *   script_pub_key: "1 2 3 4 5 6 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f"
 *   reason: Estos son multiples opcodes 0x6f, tengo que ver cómo encodearlos en Knuth
 *
 * Test 18:
 *   script_sig: "1 2 3 4 5 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f"
 *   script_pub_key: "1 TOALTSTACK 2 TOALTSTACK 3 4 5 6 0x6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f6f"
 *   reason: Estos son multiples opcodes 0x6f, tengo que ver cómo encodearlos en Knuth
 *
*/
// END AUTO-GENERATED SCRIPT TESTS

/**
 * Array of all test chunks for easy iteration
 */
std::vector<bchn_script_test_list const*> const all_script_test_chunks{
    &script_tests_from_json_0, //ok
    &script_tests_from_json_1, //ok
    &script_tests_from_json_2, //ok
    &script_tests_from_json_3, //ok
    &script_tests_from_json_4, //ok
    &script_tests_from_json_5, //ok
    // &script_tests_from_json_6, //ok
    // &script_tests_from_json_7, //ok
    // &script_tests_from_json_8, // ok
    // &script_tests_from_json_9, // ok
    // &script_tests_from_json_10, // ok
    // &script_tests_from_json_11, // fail
    // &script_tests_from_json_12, // fail
    // &script_tests_from_json_13, // fail
    // &script_tests_from_json_14, // fail
    // &script_tests_from_json_15, // fail
    // &script_tests_from_json_16, // fail
    // &script_tests_from_json_17 // fail
};


#endif // KTH_TEST_SCRIPT_HPP