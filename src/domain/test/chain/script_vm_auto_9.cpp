// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk 9 (tests 900 to 999)

#include <test_helpers.hpp>
#include "script.hpp"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

TEST_CASE("VM-AUTO #900 [Should Pass]: REVERSEBYTES, 520 bytes", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "reversebytes 'hhfanqyfcglncrriufjfvjgqcbbwrilaxeczpotsfzvmyrwlobpjtvtmhamvzoiwwjqazpyekvxpvsiiwuvertiqqiksmgkfplzamdvcelrrjsrvsxgojxngirwcoxhtmvtpzdjwzsuamlpudecziyiqkuukdbakbswcyelaksmudkgqccxpbobrkdpajsgogcuspzrptvrrsyuhrtxcftlpwlodgkavcqwofyxcwrsbovwojgznkiyijhohlyxevhqnqsibheuinifzybtbblzutjtsrotyqndybrncsdyrzlblxctejeyienvjeuslnzxomqzclruxceqispbntxngdpttfzzvqxshwlktvtkupouvjaugatahxgzzjidcauprbwdwakushoyhmogialwljyjrluudbkwjrkitxfsbtrvvyaesdosyzyxiysldhrybsxfrzpwwpubzncyzupdwoumfkfxnivdsryzlizjumarmlpqwfwqvngfegryrinviygnz' equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #901 [Should Pass]: REVERSEBYTES, 3 bytes equal", "[vm][auto]") { run_bchn_test({"[123456]", "reversebytes [563412] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes equal"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #902 [Should Pass]: REVERSEBYTES, 3 bytes double reverse equal", "[vm][auto]") { run_bchn_test({"[020406080a0c]", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes double reverse equal"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #903 [Should Pass]: REVERSEBYTES, whitepaper title double reverse equal", "[vm][auto]") { run_bchn_test({"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, whitepaper title double reverse equal"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #904 [Should Pass]: REVERSEBYTES, 520 bytes double reverse equal", "[vm][auto]") { run_bchn_test({"'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes double reverse equal"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #905 [Should Pass]: REVERSEBYTES, palindrome 1", "[vm][auto]") { run_bchn_test({"[0102030201]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 1"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #906 [Should Pass]: REVERSEBYTES, palindrome 2", "[vm][auto]") { run_bchn_test({"[7766554444556677]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 2"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #907 [Should Pass]: REVERSEBYTES, palindrome 3", "[vm][auto]") { run_bchn_test({"'madam'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 3"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #908 [Should Pass]: REVERSEBYTES, palindrome 4", "[vm][auto]") { run_bchn_test({"'racecar'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 4"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #909 [Should Pass]: REVERSEBYTES, palindrome 5", "[vm][auto]") { run_bchn_test({"'redrum_siris_murder'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 5"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #910 [Should Pass]: REVERSEBYTES, palindrome 6", "[vm][auto]") { run_bchn_test({"'step_on_no_pets'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 6"}); } // flags: P2SH, expected: OK

TEST_CASE("VM-AUTO #911 [Should Fail: EVAL_FALSE]: REVERSEBYTES, non-palindrome 1", "[vm][auto]") { run_bchn_test({"'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 1"}); } // flags: P2SH, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #912 [Should Fail: EVAL_FALSE]: REVERSEBYTES, non-palindrome 2", "[vm][auto]") { run_bchn_test({"[1234]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 2"}); } // flags: P2SH, expected: EVAL_FALSE

TEST_CASE("VM-AUTO #913 [Should Fail: INVALID_STACK_OPERATION]: NUM2BIN, empty stack", "[vm][auto]") { run_bchn_test({"", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "NUM2BIN, empty stack"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #914 [Should Fail: INVALID_STACK_OPERATION]: NUM2BIN, one parameter", "[vm][auto]") { run_bchn_test({"0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "NUM2BIN, one parameter"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #915 [Should Pass]: NUM2BIN, canonical argument", "[vm][auto]") { run_bchn_test({"0 0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #916 [Should Pass]: NUM2BIN, zero extend", "[vm][auto]") { run_bchn_test({"0 1", "num2bin [00] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, zero extend"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #917 [Should Pass]: NUM2BIN, zero extend", "[vm][auto]") { run_bchn_test({"0 7", "num2bin [00000000000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, zero extend"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #918 [Should Pass]: NUM2BIN, canonical argument", "[vm][auto]") { run_bchn_test({"1 1", "num2bin 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #919 [Should Pass]: NUM2BIN, canonical argument", "[vm][auto]") { run_bchn_test({"-42 1", "num2bin -42 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #920 [Should Pass]: NUM2BIN, canonical argument", "[vm][auto]") { run_bchn_test({"-42 2", "num2bin [2a80] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #921 [Should Pass]: NUM2BIN, large materialization", "[vm][auto]") { run_bchn_test({"-42 10", "num2bin [2a000000000000000080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, large materialization"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #922 [Should Pass]: Pushing 520 bytes is ok", "[vm][auto]") { run_bchn_test({"-42 520", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pushing 520 bytes is ok"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #133: error=KTH_INVALID_OPERAND_SIZE, reason=NUM2BIN size 521 exceeds max. Knuth returns invalid_operand_size, BCHN returns PUSH_SIZE.
TEST_CASE("VM-AUTO #923 [Should Fail: KTH_INVALID_OPERAND_SIZE]: Pushing 521 bytes is not", "[vm][auto]") { run_bchn_test({"-42 521", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "Pushing 521 bytes is not [KNUTH_OVERRIDE: KTH_INVALID_OPERAND_SIZE]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_OPERAND_SIZE

// KNUTH_OVERRIDE #134: error=KTH_INVALID_OPERAND_SIZE, reason=NUM2BIN negative size. Knuth returns invalid_operand_size, BCHN returns PUSH_SIZE.
TEST_CASE("VM-AUTO #924 [Should Fail: KTH_INVALID_OPERAND_SIZE]: Negative size", "[vm][auto]") { run_bchn_test({"-42 -3", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "Negative size [KNUTH_OVERRIDE: KTH_INVALID_OPERAND_SIZE]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_OPERAND_SIZE

TEST_CASE("VM-AUTO #925 [Should Pass]: Item size reduction", "[vm][auto]") { run_bchn_test({"[abcdef4280] 4", "num2bin [abcdefc2] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Item size reduction"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #926 [Should Fail: IMPOSSIBLE_ENCODING]: output too small", "[vm][auto]") { run_bchn_test({"[abcdef] 2", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::impossible_encoding, "output too small"}); } // flags: P2SH,STRICTENC, expected: IMPOSSIBLE_ENCODING

TEST_CASE("VM-AUTO #927 [Should Pass]: [abcdef] 3 | num2bin", "[vm][auto]") { run_bchn_test({"[abcdef] 3", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #928 [Should Pass]: Negative zero", "[vm][auto]") { run_bchn_test({"[80] 0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Negative zero"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #929 [Should Pass]: Negative zero, larger output", "[vm][auto]") { run_bchn_test({"[80] 3", "num2bin [000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Negative zero, larger output"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #930 [Should Pass]: NUM2BIN does not always return input verbatim, when same size (1) requested", "[vm][auto]") { run_bchn_test({"[80] 1", "num2bin [00] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (1) requested"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #931 [Should Pass]: NUM2BIN does not always return input verbatim, when same size (4) requested", "[vm][auto]") { run_bchn_test({"[00000080] 4", "num2bin [00000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (4) requested"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #932 [Should Pass]: NUM2BIN where len(a) > 4", "[vm][auto]") { run_bchn_test({"[abcdef4243] 5", "num2bin [abcdef4243] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN where len(a) > 4"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #933 [Should Pass]: pads output properly", "[vm][auto]") { run_bchn_test({"[abcdef4243] 6", "num2bin [abcdef424300] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "pads output properly"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #934 [Should Fail: IMPOSSIBLE_ENCODING]: output too small", "[vm][auto]") { run_bchn_test({"[abcdef4243] 4", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::impossible_encoding, "output too small"}); } // flags: P2SH,STRICTENC, expected: IMPOSSIBLE_ENCODING

TEST_CASE("VM-AUTO #935 [Should Pass]: 520 byte 1st operand", "[vm][auto]") { run_bchn_test({"[0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat 520", "num2bin [0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "520 byte 1st operand"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #936 [Should Pass]: 1st operand not minimally encoded", "[vm][auto]") { run_bchn_test({"[0000000000] 5", "num2bin [0000000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "1st operand not minimally encoded"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #937 [Should Pass]: 1st operand can shrink", "[vm][auto]") { run_bchn_test({"[0001000000] 3", "num2bin [000100] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "1st operand can shrink"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #938 [Should Pass]: 2nd operand not minimally encoded", "[vm][auto]") { run_bchn_test({"1 [050000]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "2nd operand not minimally encoded"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #939 [Should Fail: INVALID_NUMBER_RANGE]: 2nd operand > 4 bytes", "[vm][auto]") { run_bchn_test({"1 [0500000000]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "2nd operand > 4 bytes"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #940 [Should Fail: INVALID_NUMBER_RANGE]: 2nd operand > 4 bytes", "[vm][auto]") { run_bchn_test({"[abcdef42] [abcdef4243]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "2nd operand > 4 bytes"}); } // flags: P2SH,STRICTENC, expected: INVALID_NUMBER_RANGE

TEST_CASE("VM-AUTO #941 [Should Fail: INVALID_STACK_OPERATION]: BIN2NUM, empty stack", "[vm][auto]") { run_bchn_test({"", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "BIN2NUM, empty stack"}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #942 [Should Pass]: BIN2NUM, canonical argument", "[vm][auto]") { run_bchn_test({"0", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #943 [Should Pass]: BIN2NUM, canonical argument", "[vm][auto]") { run_bchn_test({"1", "bin2num 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #944 [Should Pass]: BIN2NUM, canonical argument", "[vm][auto]") { run_bchn_test({"-42", "bin2num -42 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #945 [Should Pass]: BIN2NUM, non-canonical argument", "[vm][auto]") { run_bchn_test({"[00]", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #946 [Should Pass]: BIN2NUM, maximum size argument", "[vm][auto]") { run_bchn_test({"[ffffff7f]", "bin2num 2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #947 [Should Pass]: BIN2NUM, maximum size argument", "[vm][auto]") { run_bchn_test({"[ffffffff]", "bin2num -2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #135: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
TEST_CASE("VM-AUTO #948 [Should Fail: KTH_INVALID_NUMBER_ENCODING]: BIN2NUM, oversized argument", "[vm][auto]") { run_bchn_test({"[ffffffff00]", "bin2num 2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_NUMBER_ENCODING

TEST_CASE("VM-AUTO #949 [Should Pass]: BIN2NUM, non-canonical maximum size argument", "[vm][auto]") { run_bchn_test({"[ffffff7f80]", "bin2num -2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical maximum size argument"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #950 [Should Pass]: [0100000000] | bin2num 1 equal", "[vm][auto]") { run_bchn_test({"[0100000000]", "bin2num 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #951 [Should Pass]: [FE00000000] | bin2num 254 equal", "[vm][auto]") { run_bchn_test({"[FE00000000]", "bin2num 254 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #952 [Should Pass]: [0500000080] | bin2num [85] equal", "[vm][auto]") { run_bchn_test({"[0500000080]", "bin2num [85] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #953 [Should Pass]: Pad where MSB of number is set", "[vm][auto]") { run_bchn_test({"[800000]", "bin2num 128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #954 [Should Pass]: Pad where MSB of number is set", "[vm][auto]") { run_bchn_test({"[800080]", "bin2num -128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #955 [Should Pass]: Pad where MSB of number is set", "[vm][auto]") { run_bchn_test({"[8000]", "bin2num 128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #956 [Should Pass]: Pad where MSB of number is set", "[vm][auto]") { run_bchn_test({"[8080]", "bin2num -128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #957 [Should Pass]: Don't pad where MSB of number is not set", "[vm][auto]") { run_bchn_test({"[0f0000]", "bin2num 15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #958 [Should Pass]: Don't pad where MSB of number is not set", "[vm][auto]") { run_bchn_test({"[0f0080]", "bin2num -15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #959 [Should Pass]: Don't pad where MSB of number is not set", "[vm][auto]") { run_bchn_test({"[0f00]", "bin2num 15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #960 [Should Pass]: Don't pad where MSB of number is not set", "[vm][auto]") { run_bchn_test({"[0f80]", "bin2num -15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #961 [Should Pass]: Ensure significant zero bytes are retained", "[vm][auto]") { run_bchn_test({"[0100800000]", "bin2num 8388609 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #962 [Should Pass]: Ensure significant zero bytes are retained", "[vm][auto]") { run_bchn_test({"[0100800080]", "bin2num -8388609 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #963 [Should Pass]: Ensure significant zero bytes are retained", "[vm][auto]") { run_bchn_test({"[01000f0000]", "bin2num 983041 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #964 [Should Pass]: Ensure significant zero bytes are retained", "[vm][auto]") { run_bchn_test({"[01000f0080]", "bin2num -983041 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"}); } // flags: P2SH,STRICTENC, expected: OK

TEST_CASE("VM-AUTO #965 [Should Pass]: BIN2NUM, maximum size argument, 64-bit integers", "[vm][auto]") { run_bchn_test({"[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #966 [Should Pass]: BIN2NUM, maximum size argument, 64-bit integers", "[vm][auto]") { run_bchn_test({"[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

// KNUTH_OVERRIDE #136: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
TEST_CASE("VM-AUTO #967 [Should Fail: KTH_INVALID_NUMBER_ENCODING]: BIN2NUM, oversized argument", "[vm][auto]") { run_bchn_test({"[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_NUMBER_ENCODING

// KNUTH_OVERRIDE #137: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
TEST_CASE("VM-AUTO #968 [Should Fail: KTH_INVALID_NUMBER_ENCODING]: BIN2NUM, oversized argument", "[vm][auto]") { run_bchn_test({"[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"}); } // flags: P2SH,STRICTENC, expected: KTH_INVALID_NUMBER_ENCODING

// KNUTH_OVERRIDE #138: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE_64_BIT.
TEST_CASE("VM-AUTO #969 [Should Fail: KTH_INVALID_NUMBER_ENCODING]: BIN2NUM, oversized argument", "[vm][auto]") { run_bchn_test({"[ffffffffffffffff00]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: KTH_INVALID_NUMBER_ENCODING

TEST_CASE("VM-AUTO #970 [Should Pass]: BIN2NUM, non-canonical maximum size argument", "[vm][auto]") { run_bchn_test({"[ffffffffffffff7f80]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical maximum size argument"}); } // flags: P2SH,STRICTENC,64_BIT_INTEGERS, expected: OK

TEST_CASE("VM-AUTO #971 [Should Fail: INVALID_STACK_OPERATION]: nop | size 1", "[vm][auto]") { run_bchn_test({"nop", "size 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""}); } // flags: P2SH,STRICTENC, expected: INVALID_STACK_OPERATION

TEST_CASE("VM-AUTO #972 [Should Fail: DISABLED_OPCODE]: INVERT disabled", "[vm][auto]") { run_bchn_test({"'abc'", "if invert else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "INVERT disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #973 [Should Fail: DISABLED_OPCODE]: LSHIFTNUM disabled", "[vm][auto]") { run_bchn_test({"2 0 if lshiftnum else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "LSHIFTNUM disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #974 [Should Fail: DISABLED_OPCODE]: RSHIFTNUM disabled", "[vm][auto]") { run_bchn_test({"2 0 if rshiftnum else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "RSHIFTNUM disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #975 [Should Fail: DISABLED_OPCODE]: MUL disabled", "[vm][auto]") { run_bchn_test({"2 2 0 if mul else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "MUL disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #976 [Should Fail: DISABLED_OPCODE]: LSHIFTBIN disabled", "[vm][auto]") { run_bchn_test({"2 2 0 if lshiftbin else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "LSHIFTBIN disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

TEST_CASE("VM-AUTO #977 [Should Fail: DISABLED_OPCODE]: RSHIFTBIN disabled", "[vm][auto]") { run_bchn_test({"2 2 0 if rshiftbin else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "RSHIFTBIN disabled"}); } // flags: P2SH,STRICTENC, expected: DISABLED_OPCODE

// KNUTH_OVERRIDE #20: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #978 [Should Pass]: AND, empty parameters", "[vm][auto]") { run_bchn_test({"0 0", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #21: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #979 [Should Pass]: AND, simple and", "[vm][auto]") { run_bchn_test({"[00] [00]", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #22: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #980 [Should Pass]: AND, simple and", "[vm][auto]") { run_bchn_test({"1 [00]", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #23: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #981 [Should Pass]: AND, simple and", "[vm][auto]") { run_bchn_test({"[00] 1", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #24: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #982 [Should Pass]: AND, simple and", "[vm][auto]") { run_bchn_test({"1 1", "and 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #25: error=KTH_OP_AND, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #983 [Should Fail: KTH_OP_AND]: AND, invalid parameter count", "[vm][auto]") { run_bchn_test({"0", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "AND, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_AND

// KNUTH_OVERRIDE #26: error=KTH_OP_AND, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #984 [Should Fail: KTH_OP_AND]: AND, empty stack", "[vm][auto]") { run_bchn_test({"", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "AND, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_AND

// KNUTH_OVERRIDE #29: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN returns OPERAND_SIZE, Knuth returns operand_size_mismatch
TEST_CASE("VM-AUTO #985 [Should Fail: KTH_OPERAND_SIZE_MISMATCH]: AND, different operand size", "[vm][auto]") { run_bchn_test({"0 1", "and 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "AND, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OPERAND_SIZE_MISMATCH

// KNUTH_OVERRIDE #30: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #986 [Should Pass]: AND, more complex operands", "[vm][auto]") { run_bchn_test({"[ab] [cd]", "and [89] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #31: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #987 [Should Pass]: OR, empty parameters", "[vm][auto]") { run_bchn_test({"0 0", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #32: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #988 [Should Pass]: OR, simple and", "[vm][auto]") { run_bchn_test({"[00] [00]", "or [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #33: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #989 [Should Pass]: OR, simple and", "[vm][auto]") { run_bchn_test({"1 [00]", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #34: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #990 [Should Pass]: OR, simple and", "[vm][auto]") { run_bchn_test({"[00] 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #38: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #991 [Should Pass]: OR, simple and", "[vm][auto]") { run_bchn_test({"1 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #35: error=KTH_OP_OR, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #992 [Should Fail: KTH_OP_OR]: OR, invalid parameter count", "[vm][auto]") { run_bchn_test({"0", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "OR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_OR

// KNUTH_OVERRIDE #36: error=KTH_OP_OR, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
TEST_CASE("VM-AUTO #993 [Should Fail: KTH_OP_OR]: OR, empty stack", "[vm][auto]") { run_bchn_test({"", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "OR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OP_OR

// KNUTH_OVERRIDE #39: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_or error (bitcoin:140) - different operand sizes
TEST_CASE("VM-AUTO #994 [Should Fail: KTH_OPERAND_SIZE_MISMATCH]: OR, different operand size", "[vm][auto]") { run_bchn_test({"0 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "OR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: KTH_OPERAND_SIZE_MISMATCH

// KNUTH_OVERRIDE #40: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #995 [Should Pass]: XOR, more complex operands", "[vm][auto]") { run_bchn_test({"[ab] [cd]", "or [ef] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #41: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #996 [Should Pass]: XOR, empty parameters", "[vm][auto]") { run_bchn_test({"0 0", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #42: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #997 [Should Pass]: XOR, simple and", "[vm][auto]") { run_bchn_test({"[00] [00]", "xor [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #43: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #998 [Should Pass]: XOR, simple and", "[vm][auto]") { run_bchn_test({"1 [00]", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK

// KNUTH_OVERRIDE #44: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
TEST_CASE("VM-AUTO #999 [Should Pass]: XOR, simple and", "[vm][auto]") { run_bchn_test({"[00] 1", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"}); } // flags: P2SH,STRICTENC, expected: OK
