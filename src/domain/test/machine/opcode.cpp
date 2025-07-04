// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test_helpers.hpp>

using namespace kth;
using namespace kd;
using namespace kth::domain::machine;

// Start Test Suite: opcode tests

// opcode_to_string
//-----------------------------------------------------------------------------
// Partial coverage includes those with aliases and samples of each "range" of codes.

// Use the traditional serializations for all codes (in this case 'zero' vs. 'push_0').
TEST_CASE("opcode to string  zero any forks  zero", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_size_0, rule_fork::no_rules) == "zero");
    REQUIRE(opcode_to_string(opcode::push_size_0, rule_fork::all_rules) == "zero");
}

// We formerly serialized all 1-75 as 'special'.
TEST_CASE("opcode to string  push size 42 any forks  push 42", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_size_42, rule_fork::no_rules) == "push_42");
    REQUIRE(opcode_to_string(opcode::push_size_42, rule_fork::all_rules) == "push_42");
}

// Use the traditional serializations for all codes (in this case 'pushdata1' vs. 'push_one').
TEST_CASE("opcode to string  push one size any forks  pushdata1", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_one_size, rule_fork::no_rules) == "pushdata1");
    REQUIRE(opcode_to_string(opcode::push_one_size, rule_fork::all_rules) == "pushdata1");
}

// Use the traditional serializations for all codes (in this case 'pushdata2' vs. 'push_two').
TEST_CASE("opcode to string  push two size size any forks  pushdata2", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_two_size, rule_fork::no_rules) == "pushdata2");
    REQUIRE(opcode_to_string(opcode::push_two_size, rule_fork::all_rules) == "pushdata2");
}

// Use the traditional serializations for all codes (in this case 'pushdata4' vs. 'push_four').
TEST_CASE("opcode to string  push four size any forks  pushdata4", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_four_size, rule_fork::no_rules) == "pushdata4");
    REQUIRE(opcode_to_string(opcode::push_four_size, rule_fork::all_rules) == "pushdata4");
}

// Use the traditional serializations for all codes (in this case 'reserved' vs. 0x50).
TEST_CASE("opcode to string  reserved 80 any forks  reserved", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::reserved_80, rule_fork::no_rules) == "reserved");
    REQUIRE(opcode_to_string(opcode::reserved_80, rule_fork::all_rules) == "reserved");
}

TEST_CASE("opcode to string  push positive 7 any forks  7", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::push_positive_7, rule_fork::no_rules) == "7");
    REQUIRE(opcode_to_string(opcode::push_positive_7, rule_fork::all_rules) == "7");
}

// Use the traditional serializations for all codes (in this case 'ver' vs. '0x62').
TEST_CASE("opcode to string  reserved 98 any forks  ver", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::reserved_98, rule_fork::no_rules) == "ver");
    REQUIRE(opcode_to_string(opcode::reserved_98, rule_fork::all_rules) == "ver");
}

// Use the traditional serializations for all codes (in this case 'verif' vs. '0x65').
TEST_CASE("opcode to string  disabled verif any forks  verif", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::disabled_verif, rule_fork::no_rules) == "verif");
    REQUIRE(opcode_to_string(opcode::disabled_verif, rule_fork::all_rules) == "verif");
}

// Use the traditional serializations for all codes (in this case 'vernotif' vs. '0x66').
TEST_CASE("opcode to string  disabled vernotif any forks  vernotif", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::disabled_vernotif, rule_fork::no_rules) == "vernotif");
    REQUIRE(opcode_to_string(opcode::disabled_vernotif, rule_fork::all_rules) == "vernotif");
}

// Use the traditional serializations for all codes (in this case '2drop' vs. 'drop2').
TEST_CASE("opcode to string  drop2 any forks  2drop", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::drop2, rule_fork::no_rules) == "2drop");
    REQUIRE(opcode_to_string(opcode::drop2, rule_fork::all_rules) == "2drop");
}

// Use the traditional serializations for all codes (in this case '2dup' vs. 'dup2').
TEST_CASE("opcode to string  rdup2 any forks  2dup", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::dup2, rule_fork::no_rules) == "2dup");
    REQUIRE(opcode_to_string(opcode::dup2, rule_fork::all_rules) == "2dup");
}

// Use the traditional serializations for all codes (in this case '3dup' vs. 'dup3').
TEST_CASE("opcode to string  dup3 any forks  3dup", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::dup3, rule_fork::no_rules) == "3dup");
    REQUIRE(opcode_to_string(opcode::dup3, rule_fork::all_rules) == "3dup");
}

// Use the traditional serializations for all codes (in this case '2over' vs. 'over2').
TEST_CASE("opcode to string  over2 any forks  vernotif", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::over2, rule_fork::no_rules) == "2over");
    REQUIRE(opcode_to_string(opcode::over2, rule_fork::all_rules) == "2over");
}

// Use the traditional serializations for all codes (in this case '2rot' vs. 'rot2').
TEST_CASE("opcode to string  rot2 any forks  2rot", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::rot2, rule_fork::no_rules) == "2rot");
    REQUIRE(opcode_to_string(opcode::rot2, rule_fork::all_rules) == "2rot");
}

// Use the traditional serializations for all codes (in this case '2swap' vs. 'swap2').
TEST_CASE("opcode to string  swap2 any forks  2swap", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::swap2, rule_fork::no_rules) == "2swap");
    REQUIRE(opcode_to_string(opcode::swap2, rule_fork::all_rules) == "2swap");
}

// Use the traditional serializations for all codes (in this case '1add' vs. 'add1').
TEST_CASE("opcode to string  add1 any forks  2rot", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::add1, rule_fork::no_rules) == "1add");
    REQUIRE(opcode_to_string(opcode::add1, rule_fork::all_rules) == "1add");
}

// Use the traditional serializations for all codes (in this case '1sub' vs. 'sub1').
TEST_CASE("opcode to string  sub1 any forks  2swap", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::sub1, rule_fork::no_rules) == "1sub");
    REQUIRE(opcode_to_string(opcode::sub1, rule_fork::all_rules) == "1sub");
}

// Ensure nop2 still serializes as 'nop2' without bip65 fork.
TEST_CASE("opcode to string  nop2 or checklocktimeverify any fork except bip65  nop2", "[opcode]") {
    static_assert(opcode::checklocktimeverify == opcode::nop2, "nop2 drift");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::no_rules) == "nop2");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip16_rule) == "nop2");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip30_rule) == "nop2");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip34_rule) == "nop2");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip66_rule) == "nop2");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip112_rule) == "nop2");
}

// Ensure nop2 and checklocktimeverify serialize as 'checklocktimeverify' with bip65 fork.
TEST_CASE("opcode to string  nop2 or checklocktimeverify bip65 fork  checklocktimeverify", "[opcode]") {
    static_assert(opcode::checklocktimeverify == opcode::nop2, "nop2 drift");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::bip65_rule) == "checklocktimeverify");
    REQUIRE(opcode_to_string(opcode::nop2, rule_fork::all_rules) == "checklocktimeverify");
}

// Ensure nop3 still serializes as 'nop3' without bip112 fork.
TEST_CASE("opcode to string  nop3 or checksequenceverify any fork except bip112  nop3", "[opcode]") {
    static_assert(opcode::checksequenceverify == opcode::nop3, "nop3 drift");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::no_rules) == "nop3");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip16_rule) == "nop3");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip30_rule) == "nop3");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip34_rule) == "nop3");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip66_rule) == "nop3");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip65_rule) == "nop3");
}

// Ensure nop3 and checksequenceverify serialize as 'checksequenceverify' with bip112 fork.
TEST_CASE("opcode to string  nop3 or checksequenceverify bip112 fork  checksequenceverify", "[opcode]") {
    static_assert(opcode::checksequenceverify == opcode::nop3, "nop3 drift");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::bip112_rule) == "checksequenceverify");
    REQUIRE(opcode_to_string(opcode::nop3, rule_fork::all_rules) == "checksequenceverify");
}

// All codes above 'nop10' serialize as hex.
TEST_CASE("opcode to string  checkdatasig 186 any forks  0xba", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::checkdatasig, rule_fork::no_rules) == "checkdatasig");
    REQUIRE(opcode_to_string(opcode::checkdatasig, rule_fork::all_rules) == "checkdatasig");
}

// All codes above 'nop10' serialize as hex.
TEST_CASE("opcode to string  reserved 255 any forks  0xff", "[opcode]") {
    REQUIRE(opcode_to_string(opcode::reserved_255, rule_fork::no_rules) == "0xff");
    REQUIRE(opcode_to_string(opcode::reserved_255, rule_fork::all_rules) == "0xff");
}

// opcode_from_string
//-----------------------------------------------------------------------------
// Partial coverage includes all aliases and boundaries of each "range" of codes.

// zero

TEST_CASE("opcode from string  zero  push size 0", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "zero"));
    REQUIRE(out_code == opcode::push_size_0);
}

TEST_CASE("opcode from string  push 0  push size 0", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_0"));
    REQUIRE(out_code == opcode::push_size_0);
}

TEST_CASE("opcode from string  0  push size 0", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "0"));
    REQUIRE(out_code == opcode::push_size_0);
}

// push n (special)

TEST_CASE("opcode from string  push 1  push size 1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_1"));
    REQUIRE(out_code == opcode::push_size_1);
}

TEST_CASE("opcode from string  push 75  push size 75", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_75"));
    REQUIRE(out_code == opcode::push_size_75);
}

// push n byte size (pushdata)

TEST_CASE("opcode from string  push one  push one size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_one"));
    REQUIRE(out_code == opcode::push_one_size);
}

TEST_CASE("opcode from string  push two  push two size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_two"));
    REQUIRE(out_code == opcode::push_two_size);
}

TEST_CASE("opcode from string  push four  push four size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "push_four"));
    REQUIRE(out_code == opcode::push_four_size);
}

TEST_CASE("opcode from string  pushdata1  push one size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "pushdata1"));
    REQUIRE(out_code == opcode::push_one_size);
}

TEST_CASE("opcode from string  pushdata2 push two size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "pushdata2"));
    REQUIRE(out_code == opcode::push_two_size);
}

TEST_CASE("opcode from string  pushdata4  push four size", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "pushdata4"));
    REQUIRE(out_code == opcode::push_four_size);
}

// negative one

TEST_CASE("opcode from string  minus1  push negative 1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "-1"));
    REQUIRE(out_code == opcode::push_negative_1);
}

// reserved

TEST_CASE("opcode from string  reserved  reserved 80", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved"));
    REQUIRE(out_code == opcode::reserved_80);
}

TEST_CASE("opcode from string  reserved 80  reserved 80", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved_80"));
    REQUIRE(out_code == opcode::reserved_80);
}

// positive numbers

TEST_CASE("opcode from string  1  push positive 1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "1"));
    REQUIRE(out_code == opcode::push_positive_1);
}

TEST_CASE("opcode from string  16  push positive 16", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "16"));
    REQUIRE(out_code == opcode::push_positive_16);
}

// ver/verif/vernotif

TEST_CASE("opcode from string  ver  reserved 98", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "ver"));
    REQUIRE(out_code == opcode::reserved_98);
}

TEST_CASE("opcode from string  verif  disabled verif", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "verif"));
    REQUIRE(out_code == opcode::disabled_verif);
}

TEST_CASE("opcode from string  vernotif  disabled vernotif", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "vernotif"));
    REQUIRE(out_code == opcode::disabled_vernotif);
}

TEST_CASE("opcode from string  reserved 98  reserved 98", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved_98"));
    REQUIRE(out_code == opcode::reserved_98);
}

TEST_CASE("opcode from string  disabled verif  disabled verif", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "disabled_verif"));
    REQUIRE(out_code == opcode::disabled_verif);
}

TEST_CASE("opcode from string  disabled vernotif  disabled vernotif", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "disabled_vernotif"));
    REQUIRE(out_code == opcode::disabled_vernotif);
}

// math

TEST_CASE("opcode from string  drop2  drop2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "drop2"));
    REQUIRE(out_code == opcode::drop2);
}

TEST_CASE("opcode from string  dup2  dup2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "dup2"));
    REQUIRE(out_code == opcode::dup2);
}

TEST_CASE("opcode from string  dup3  dup3", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "dup3"));
    REQUIRE(out_code == opcode::dup3);
}

TEST_CASE("opcode from string  over2  over2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "over2"));
    REQUIRE(out_code == opcode::over2);
}

TEST_CASE("opcode from string  rot2  rot2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "rot2"));
    REQUIRE(out_code == opcode::rot2);
}

TEST_CASE("opcode from string  swap2  swap2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "swap2"));
    REQUIRE(out_code == opcode::swap2);
}

TEST_CASE("opcode from string  add1  add1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "add1"));
    REQUIRE(out_code == opcode::add1);
}

TEST_CASE("opcode from string  sub1  sub1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "sub1"));
    REQUIRE(out_code == opcode::sub1);
}

TEST_CASE("opcode from string  mul2  mul2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "mul2"));
    REQUIRE(out_code == opcode::disabled_mul2);
}

TEST_CASE("opcode from string  div2  div2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "div2"));
    REQUIRE(out_code == opcode::disabled_div2);
}

TEST_CASE("opcode from string  2drop  drop2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2drop"));
    REQUIRE(out_code == opcode::drop2);
}

TEST_CASE("opcode from string  2dup  dup2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2dup"));
    REQUIRE(out_code == opcode::dup2);
}

TEST_CASE("opcode from string  3dup  dup3", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "3dup"));
    REQUIRE(out_code == opcode::dup3);
}

TEST_CASE("opcode from string  2over  over2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2over"));
    REQUIRE(out_code == opcode::over2);
}

TEST_CASE("opcode from string  2rot  rot2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2rot"));
    REQUIRE(out_code == opcode::rot2);
}

TEST_CASE("opcode from string  2swap  swap2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2swap"));
    REQUIRE(out_code == opcode::swap2);
}

TEST_CASE("opcode from string  1add  add1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "1add"));
    REQUIRE(out_code == opcode::add1);
}

TEST_CASE("opcode from string  1sub  sub1", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "1sub"));
    REQUIRE(out_code == opcode::sub1);
}

TEST_CASE("opcode from string  2mul  mul2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2mul"));
    REQUIRE(out_code == opcode::disabled_mul2);
}

TEST_CASE("opcode from string  2div  div2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "2div"));
    REQUIRE(out_code == opcode::disabled_div2);
}

// reserved1/reserved2

TEST_CASE("opcode from string  reserved1  reserved 137", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved1"));
    REQUIRE(out_code == opcode::reserved_137);
}

TEST_CASE("opcode from string  reserved2  reserved 138", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved2"));
    REQUIRE(out_code == opcode::reserved_138);
}

TEST_CASE("opcode from string  disabled vernotif  reserved 137", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved_137"));
    REQUIRE(out_code == opcode::reserved_137);
}

TEST_CASE("opcode from string  disabled vernotif  reserved 138", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "reserved_138"));
    REQUIRE(out_code == opcode::reserved_138);
}

// nop2/checklocktimeverify

TEST_CASE("opcode from string  nop2  nop2", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "nop2"));
    REQUIRE(out_code == opcode::nop2);
}

TEST_CASE("opcode from string  checklocktimeverify  nop2 or checklocktimeverify", "[opcode]") {
    static_assert(opcode::checklocktimeverify == opcode::nop2, "nop2 drift");
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "checklocktimeverify"));
    REQUIRE(out_code == opcode::nop2);
}

// nop3/checksequenceverify

TEST_CASE("opcode from string  nop3  nop3", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "nop3"));
    REQUIRE(out_code == opcode::nop3);
}

TEST_CASE("opcode from string  checksequenceverify  nop3 or checksequenceverify", "[opcode]") {
    static_assert(opcode::checksequenceverify == opcode::nop3, "nop3 drift");
    opcode out_code;
    REQUIRE(opcode_from_string(out_code, "checksequenceverify"));
    REQUIRE(out_code == opcode::nop3);
}

// opcode_to_hexadecimal
//-----------------------------------------------------------------------------

TEST_CASE("opcode to hexadecimal  zero  0x00", "[opcode]") {
    REQUIRE(opcode_to_hexadecimal(opcode::push_size_0) == "0x00");
}

TEST_CASE("opcode to hexadecimal  push size 42  0x2a", "[opcode]") {
    REQUIRE(opcode_to_hexadecimal(opcode::push_size_42) == "0x2a");
}

TEST_CASE("opcode to hexadecimal  reserved 255  0xff", "[opcode]") {
    REQUIRE(opcode_to_hexadecimal(opcode::reserved_255) == "0xff");
}

// opcode_from_hexadecimal
//-----------------------------------------------------------------------------

TEST_CASE("opcode from hexadecimal  empty  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, ""));
}

TEST_CASE("opcode from hexadecimal  bogus  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "bogus"));
}

TEST_CASE("opcode from hexadecimal  9 bits  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "0x"));
}

TEST_CASE("opcode from hexadecimal  8 bits  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "0xf"));
}

TEST_CASE("opcode from hexadecimal  16 bits  expected", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_hexadecimal(out_code, "0xff"));
    REQUIRE(out_code == opcode::reserved_255);
}

TEST_CASE("opcode from hexadecimal  24 bits  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "0xffe"));
}

TEST_CASE("opcode from hexadecimal  48 bits  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "0xffee"));
}

TEST_CASE("opcode from hexadecimal  upper case  expected", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_hexadecimal(out_code, "0xFE"));
    REQUIRE(out_code == opcode::reserved_254);
}

TEST_CASE("opcode from hexadecimal  mixed case  expected", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_hexadecimal(out_code, "0xFe"));
    REQUIRE(out_code == opcode::reserved_254);
}

TEST_CASE("opcode from hexadecimal  numeric  expected", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_hexadecimal(out_code, "0x42"));
    REQUIRE(out_code == opcode::push_size_66);
}

TEST_CASE("opcode from hexadecimal  alphanumeric  expected", "[opcode]") {
    opcode out_code;
    REQUIRE(opcode_from_hexadecimal(out_code, "0x4f"));
    REQUIRE(out_code == opcode::push_negative_1);
}

TEST_CASE("opcode from hexadecimal  upper case prefix  false", "[opcode]") {
    opcode out_code;
    REQUIRE( ! opcode_from_hexadecimal(out_code, "0X4f"));
}

// Legacy opcode names documentation:
// The following opcode names are obsolete and no longer recognized by opcode_from_string():
// - "substr" was an old name, the current opcode for this functionality is "split"
// - "left" was an old name, the current opcode for this functionality is "num2bin" 
// - "right" was an old name, the current opcode for this functionality is "bin2num"
// These old names will fail to parse and should not be used in new code.

// Negative tests for legacy opcode names that should fail to parse
TEST_CASE("opcode from string  substr legacy name  false", "[opcode]") {
    opcode out_code;
    REQUIRE_FALSE(opcode_from_string(out_code, "substr"));
}

TEST_CASE("opcode from string  left legacy name  false", "[opcode]") {
    opcode out_code;
    REQUIRE_FALSE(opcode_from_string(out_code, "left"));
}

TEST_CASE("opcode from string  right legacy name  false", "[opcode]") {
    opcode out_code;
    REQUIRE_FALSE(opcode_from_string(out_code, "right"));
}

// End Test Suite
