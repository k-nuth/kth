// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.

#include "bchn_vectors.hpp"
#include <kth/domain/machine/script_flags.hpp>

namespace kth::test::data_bchn_bchn_17 {
// Source: script_vm_auto_9.cpp
// KNUTH_OVERRIDE #133: error=KTH_INVALID_OPERAND_SIZE, reason=NUM2BIN size 521 exceeds max. Knuth returns invalid_operand_size, BCHN returns PUSH_SIZE.
// KNUTH_OVERRIDE #134: error=KTH_INVALID_OPERAND_SIZE, reason=NUM2BIN negative size. Knuth returns invalid_operand_size, BCHN returns PUSH_SIZE.
// KNUTH_OVERRIDE #135: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
// KNUTH_OVERRIDE #136: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
// KNUTH_OVERRIDE #137: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE.
// KNUTH_OVERRIDE #138: error=KTH_INVALID_NUMBER_ENCODING, reason=BIN2NUM oversized argument. Knuth returns invalid_number_encoding, BCHN returns INVALID_NUMBER_RANGE_64_BIT.
// KNUTH_OVERRIDE #20: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #21: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #22: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #23: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #24: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #25: error=KTH_OP_AND, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #26: error=KTH_OP_AND, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #29: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN returns OPERAND_SIZE, Knuth returns operand_size_mismatch
// KNUTH_OVERRIDE #30: fork=KTH_PYTHAGORAS, reason=AND requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #31: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #32: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #33: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #34: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #38: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #35: error=KTH_OP_OR, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #36: error=KTH_OP_OR, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. Using KTH_PYTHAGORAS to activate bch_gauss which includes pythagoras functionality.
// KNUTH_OVERRIDE #39: error=KTH_OPERAND_SIZE_MISMATCH, fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly. BCHN returns INVALID_STACK_OPERATION (bitcoin:39) but Knuth returns specific op_or error (bitcoin:140) - different operand sizes
// KNUTH_OVERRIDE #40: fork=KTH_PYTHAGORAS, reason=OR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #41: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #42: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #43: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
// KNUTH_OVERRIDE #44: fork=KTH_PYTHAGORAS, reason=XOR requires bch_pythagoras fork but test only has bch_uahf. BCHN doesn't check forks correctly.
static constexpr bchn_vector rows_0[] = {
    {900, "'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "reversebytes 'hhfanqyfcglncrriufjfvjgqcbbwrilaxeczpotsfzvmyrwlobpjtvtmhamvzoiwwjqazpyekvxpvsiiwuvertiqqiksmgkfplzamdvcelrrjsrvsxgojxngirwcoxhtmvtpzdjwzsuamlpudecziyiqkuukdbakbswcyelaksmudkgqccxpbobrkdpajsgogcuspzrptvrrsyuhrtxcftlpwlodgkavcqwofyxcwrsbovwojgznkiyijhohlyxevhqnqsibheuinifzybtbblzutjtsrotyqndybrncsdyrzlblxctejeyienvjeuslnzxomqzclruxceqispbntxngdpttfzzvqxshwlktvtkupouvjaugatahxgzzjidcauprbwdwakushoyhmogialwljyjrluudbkwjrkitxfsbtrvvyaesdosyzyxiysldhrybsxfrzpwwpubzncyzupdwoumfkfxnivdsryzlizjumarmlpqwfwqvngfegryrinviygnz' equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes"},
    {901, "[123456]", "reversebytes [563412] equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes equal"},
    {902, "[020406080a0c]", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 3 bytes double reverse equal"},
    {903, "'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, whitepaper title double reverse equal"},
    {904, "'zngyivniryrgefgnvqwfwqplmramujzilzyrsdvinxfkfmuowdpuzycnzbupwwpzrfxsbyrhdlsyixyzysodseayvvrtbsfxtikrjwkbduulrjyjlwlaigomhyohsukawdwbrpuacdijzzgxhataguajvuopuktvtklwhsxqvzzfttpdgnxtnbpsiqecxurlczqmoxznlsuejvneiyejetcxlblzrydscnrbydnqytorstjtuzlbbtbyzfiniuehbisqnqhvexylhohjiyiknzgjowvobsrwcxyfowqcvakgdolwpltfcxtrhuysrrvtprzpsucgogsjapdkrbobpxccqgkdumskaleycwsbkabdkuukqiyizceduplmauszwjdzptvmthxocwrignxjogxsvrsjrrlecvdmazlpfkgmskiqqitrevuwiisvpxvkeypzaqjwwiozvmahmtvtjpbolwrymvzfstopzcexalirwbbcqgjvfjfuirrcnlgcfyqnafhh'", "dup reversebytes reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, 520 bytes double reverse equal"},
    {905, "[0102030201]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 1"},
    {906, "[7766554444556677]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 2"},
    {907, "'madam'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 3"},
    {908, "'racecar'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 4"},
    {909, "'redrum_siris_murder'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 5"},
    {910, "'step_on_no_pets'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::success, "REVERSEBYTES, palindrome 6"},
    {911, "'Bitcoin:_A_peer-to-peer_electronic_cash_system'", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 1"},
    {912, "[1234]", "dup reversebytes equal", kth::domain::machine::script_flags::bip16_rule, kth::error::stack_false, "REVERSEBYTES, non-palindrome 2"},
    {913, "", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "NUM2BIN, empty stack"},
    {914, "0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "NUM2BIN, one parameter"},
    {915, "0 0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "},
    {916, "0 1", "num2bin [00] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, zero extend"},
    {917, "0 7", "num2bin [00000000000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, zero extend"},
    {918, "1 1", "num2bin 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "},
    {919, "-42 1", "num2bin -42 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "},
    {920, "-42 2", "num2bin [2a80] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, canonical argument "},
    {921, "-42 10", "num2bin [2a000000000000000080] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN, large materialization"},
    {922, "-42 520", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pushing 520 bytes is ok"},
    {923, "-42 521", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "Pushing 521 bytes is not [KNUTH_OVERRIDE: KTH_INVALID_OPERAND_SIZE]"},
    {924, "-42 -3", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "Negative size [KNUTH_OVERRIDE: KTH_INVALID_OPERAND_SIZE]"},
    {925, "[abcdef4280] 4", "num2bin [abcdefc2] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Item size reduction"},
    {926, "[abcdef] 2", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::impossible_encoding, "output too small"},
    {927, "[abcdef] 3", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""},
    {928, "[80] 0", "num2bin 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Negative zero"},
    {929, "[80] 3", "num2bin [000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Negative zero, larger output"},
    {930, "[80] 1", "num2bin [00] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (1) requested"},
    {931, "[00000080] 4", "num2bin [00000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN does not always return input verbatim, when same size (4) requested"},
    {932, "[abcdef4243] 5", "num2bin [abcdef4243] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "NUM2BIN where len(a) > 4"},
    {933, "[abcdef4243] 6", "num2bin [abcdef424300] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "pads output properly"},
    {934, "[abcdef4243] 4", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::impossible_encoding, "output too small"},
    {935, "[0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat 520", "num2bin [0102030405060708090A0B0C0D0E0F10] dup cat dup cat dup cat dup cat dup cat [0102030405060708] cat equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "520 byte 1st operand"},
    {936, "[0000000000] 5", "num2bin [0000000000] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "1st operand not minimally encoded"},
    {937, "[0001000000] 3", "num2bin [000100] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "1st operand can shrink"},
    {938, "1 [050000]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "2nd operand not minimally encoded"},
    {939, "1 [0500000000]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "2nd operand > 4 bytes"},
    {940, "[abcdef42] [abcdef4243]", "num2bin", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_operand_size, "2nd operand > 4 bytes"},
    {941, "", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, "BIN2NUM, empty stack"},
    {942, "0", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"},
    {943, "1", "bin2num 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"},
    {944, "-42", "bin2num -42 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, canonical argument"},
    {945, "[00]", "bin2num 0 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical argument"},
    {946, "[ffffff7f]", "bin2num 2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument"},
    {947, "[ffffffff]", "bin2num -2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument"},
    {948, "[ffffffff00]", "bin2num 2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"},
    {949, "[ffffff7f80]", "bin2num -2147483647 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical maximum size argument"},
    {950, "[0100000000]", "bin2num 1 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""},
    {951, "[FE00000000]", "bin2num 254 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""},
    {952, "[0500000080]", "bin2num [85] equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, ""},
    {953, "[800000]", "bin2num 128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"},
    {954, "[800080]", "bin2num -128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"},
    {955, "[8000]", "bin2num 128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"},
    {956, "[8080]", "bin2num -128 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Pad where MSB of number is set"},
    {957, "[0f0000]", "bin2num 15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"},
    {958, "[0f0080]", "bin2num -15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"},
    {959, "[0f00]", "bin2num 15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"},
    {960, "[0f80]", "bin2num -15 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Don't pad where MSB of number is not set"},
    {961, "[0100800000]", "bin2num 8388609 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"},
    {962, "[0100800080]", "bin2num -8388609 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"},
    {963, "[01000f0000]", "bin2num 983041 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"},
    {964, "[01000f0080]", "bin2num -983041 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "Ensure significant zero bytes are retained"},
    {965, "[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"},
    {966, "[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, maximum size argument, 64-bit integers"},
    {967, "[ffffffffffffff7f]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"},
    {968, "[ffffffffffffffff]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"},
    {969, "[ffffffffffffffff00]", "bin2num 9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::invalid_number_encoding, "BIN2NUM, oversized argument [KNUTH_OVERRIDE: KTH_INVALID_NUMBER_ENCODING]"},
    {970, "[ffffffffffffff7f80]", "bin2num -9223372036854775807 equal", kth::domain::machine::script_flags::bch_64bit_integers | kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::success, "BIN2NUM, non-canonical maximum size argument"},
    {971, "nop", "size 1", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::insufficient_main_stack, ""},
    {972, "'abc'", "if invert else 1 endif", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "INVERT disabled"},
    {973, "2 0 if lshiftnum else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "LSHIFTNUM disabled"},
    {974, "2 0 if rshiftnum else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "RSHIFTNUM disabled"},
    {975, "2 2 0 if mul else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "MUL disabled"},
    {976, "2 2 0 if lshiftbin else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "LSHIFTBIN disabled"},
    {977, "2 2 0 if rshiftbin else 1 endif", "nop", kth::domain::machine::script_flags::bch_strictenc | kth::domain::machine::script_flags::bip16_rule, kth::error::op_disabled, "RSHIFTBIN disabled"},
    {978, "0 0", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {979, "[00] [00]", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {980, "1 [00]", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {981, "[00] 1", "and [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {982, "1 1", "and 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {983, "0", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "AND, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"},
    {984, "", "and 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "AND, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_AND, FORK: KTH_PYTHAGORAS]"},
    {985, "0 1", "and 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "AND, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"},
    {986, "[ab] [cd]", "and [89] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "AND, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {987, "0 0", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {988, "[00] [00]", "or [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {989, "1 [00]", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {990, "[00] 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {991, "1 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "OR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {992, "0", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "OR, invalid parameter count [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"},
    {993, "", "or 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::insufficient_main_stack, "OR, empty stack [KNUTH_OVERRIDE: ERROR: KTH_OP_OR, FORK: KTH_PYTHAGORAS]"},
    {994, "0 1", "or 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::operand_size_mismatch, "OR, different operand size [KNUTH_OVERRIDE: ERROR: KTH_OPERAND_SIZE_MISMATCH, FORK: KTH_PYTHAGORAS]"},
    {995, "[ab] [cd]", "or [ef] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, more complex operands [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {996, "0 0", "xor 0 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, empty parameters [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {997, "[00] [00]", "xor [00] equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {998, "1 [00]", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
    {999, "[00] 1", "xor 1 equal", kth::domain::machine::script_flags::bch_reactivated_opcodes, kth::error::success, "XOR, simple and [KNUTH_OVERRIDE: FORK: KTH_PYTHAGORAS]"},
};
static constexpr bchn_vector_group group = {
    "script_vm_auto_9.cpp", rows_0
};
} // namespace kth::test::data_bchn_bchn_17
