// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <kth/consensus.hpp>
#include <test_helpers.hpp>

// These give us test accesss to unpublished symbols.
#include "consensus/consensus.hpp"
#include "script/script_error.h"

using namespace kth::consensus;

// Start Test Suite: consensus script error to verify result

// Logical result

TEST_CASE("consensus script error to verify result OK eval true", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::OK) == verify_result_eval_true);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_OK) == verify_result_eval_true);
#endif
}

TEST_CASE("consensus script error to verify result EVAL FALSE eval false", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::EVAL_FALSE) == verify_result_eval_false);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_EVAL_FALSE) == verify_result_eval_false);
#endif
}

// Max size errors.

TEST_CASE("consensus script error to verify result SCRIPT SIZE script size", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SCRIPT_SIZE) == verify_result_script_size);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SCRIPT_SIZE) == verify_result_script_size);
#endif
}

TEST_CASE("consensus script error to verify result PUSH SIZE push size", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::PUSH_SIZE) == verify_result_push_size);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_PUSH_SIZE) == verify_result_push_size);
#endif
}

TEST_CASE("consensus script error to verify result OP COUNT op count", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::OP_COUNT) == verify_result_op_count);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_OP_COUNT) == verify_result_op_count);
#endif
}

TEST_CASE("consensus script error to verify result STACK SIZE stack size", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::STACK_SIZE) == verify_result_stack_size);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_STACK_SIZE) == verify_result_stack_size);
#endif
}

TEST_CASE("consensus script error to verify result SIG COUNT sig count", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_COUNT) == verify_result_sig_count);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_COUNT) == verify_result_sig_count);
#endif
}

TEST_CASE("consensus script error to verify result PUBKEY COUNT pubkey count", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::PUBKEY_COUNT) == verify_result_pubkey_count);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_PUBKEY_COUNT) == verify_result_pubkey_count);
#endif
}

// Failed verify operations

TEST_CASE("consensus script error to verify result VERIFY verify", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::VERIFY) == verify_result_verify);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_VERIFY) == verify_result_verify);
#endif
}

TEST_CASE("consensus script error to verify result EQUALVERIFY equalverify", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::EQUALVERIFY) == verify_result_equalverify);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_EQUALVERIFY) == verify_result_equalverify);
#endif
}

TEST_CASE("consensus script error to verify result CHECKMULTISIGVERIFY checkmultisigverify", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::CHECKMULTISIGVERIFY) == verify_result_checkmultisigverify);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_CHECKMULTISIGVERIFY) == verify_result_checkmultisigverify);
#endif
}

TEST_CASE("consensus script error to verify result CHECKSIGVERIFY checksigverify", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::CHECKSIGVERIFY) == verify_result_checksigverify);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_CHECKSIGVERIFY) == verify_result_checksigverify);
#endif
}

TEST_CASE("consensus script error to verify result NUMEQUALVERIFY numequalverify", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::NUMEQUALVERIFY) == verify_result_numequalverify);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_NUMEQUALVERIFY) == verify_result_numequalverify);
#endif
}

// Logical/Format/Canonical errors

TEST_CASE("consensus script error to verify result BAD OPCODE bad opcode", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::BAD_OPCODE) == verify_result_bad_opcode);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_BAD_OPCODE) == verify_result_bad_opcode);
#endif
}

TEST_CASE("consensus script error to verify result DISABLED OPCODE disabled opcode", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::DISABLED_OPCODE) == verify_result_disabled_opcode);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_DISABLED_OPCODE) == verify_result_disabled_opcode);
#endif
}

TEST_CASE("consensus script error to verify result INVALID STACK OPERATION invalid stack operation", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::INVALID_STACK_OPERATION) == verify_result_invalid_stack_operation);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_INVALID_STACK_OPERATION) == verify_result_invalid_stack_operation);
#endif
}

TEST_CASE("consensus script error to verify result INVALID ALTSTACK OPERATION invalid altstack operation", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::INVALID_ALTSTACK_OPERATION) == verify_result_invalid_altstack_operation);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_INVALID_ALTSTACK_OPERATION) == verify_result_invalid_altstack_operation);
#endif
}

TEST_CASE("consensus script error to verify result UNBALANCED CONDITIONAL unbalanced conditional", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::UNBALANCED_CONDITIONAL) == verify_result_unbalanced_conditional);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_UNBALANCED_CONDITIONAL) == verify_result_unbalanced_conditional);
#endif
}

// BIP65

TEST_CASE("consensus script error to verify result NEGATIVE LOCKTIME sig hashtype", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::NEGATIVE_LOCKTIME) == verify_result_negative_locktime);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_NEGATIVE_LOCKTIME) == verify_result_negative_locktime);
#endif
}

TEST_CASE("consensus script error to verify result ERR UNSATISFIED LOCKTIME err sig der", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::UNSATISFIED_LOCKTIME) == verify_result_unsatisfied_locktime);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_UNSATISFIED_LOCKTIME) == verify_result_unsatisfied_locktime);
#endif
}

// BIP62

TEST_CASE("consensus script error to verify result SIG HASHTYPE sig hashtype", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_HASHTYPE) == verify_result_sig_hashtype);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_HASHTYPE) == verify_result_sig_hashtype);
#endif
}

TEST_CASE("consensus script error to verify result ERR SIG DER err sig der", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_DER) == verify_result_sig_der);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_DER) == verify_result_sig_der);
#endif
}

TEST_CASE("consensus script error to verify result ERR MINIMALDATA err minimaldata", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::MINIMALDATA) == verify_result_minimaldata);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_MINIMALDATA) == verify_result_minimaldata);
#endif
}

TEST_CASE("consensus script error to verify result SIG PUSHONLY sig pushonly", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_PUSHONLY) == verify_result_sig_pushonly);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_PUSHONLY) == verify_result_sig_pushonly);
#endif
}

TEST_CASE("consensus script error to verify result SIG HIGH S sig high s", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_HIGH_S) == verify_result_sig_high_s);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_HIGH_S) == verify_result_sig_high_s);
#endif
}

#if ! defined(KTH_CURRENCY_BCH)         //BIP 147
TEST_CASE("consensus script error to verify result SIG NULLDUMMY sig nulldummy", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_NULLDUMMY) == verify_result_sig_nulldummy);
}
#endif

TEST_CASE("consensus script error to verify result PUBKEYTYPE pubkeytype", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::PUBKEYTYPE) == verify_result_pubkeytype);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_PUBKEYTYPE) == verify_result_pubkeytype);
#endif
}

TEST_CASE("consensus script error to verify result CLEANSTACK cleanstack", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::CLEANSTACK) == verify_result_cleanstack);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_CLEANSTACK) == verify_result_cleanstack);
#endif
}

TEST_CASE("consensus script error to verify result MINIMALIF minimalif", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::MINIMALIF) == verify_result_minimalif);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_MINIMALIF) == verify_result_minimalif);
#endif
}

TEST_CASE("consensus script error to verify result SIG NULLFAIL nullfail", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::SIG_NULLFAIL) == verify_result_sig_nullfail);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_SIG_NULLFAIL) == verify_result_sig_nullfail);
#endif
}

// Softfork safeness

TEST_CASE("consensus script error to verify result DISCOURAGE UPGRADABLE NOPS discourage upgradable nops", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::DISCOURAGE_UPGRADABLE_NOPS) == verify_result_discourage_upgradable_nops);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_DISCOURAGE_UPGRADABLE_NOPS) == verify_result_discourage_upgradable_nops);
#endif
}

#if ! defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus script error to verify result DISCOURAGE UPGRADABLE WITNESS PROGRAM discourage upgradable witness program", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM) == verify_result_discourage_upgradable_witness_program);
}

// Segregated witness

TEST_CASE("consensus script error to verify result WITNESS PROGRAM WRONG LENGTH witness program wrong length", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_PROGRAM_WRONG_LENGTH) == verify_result_witness_program_wrong_length);
}

TEST_CASE("consensus script error to verify result WITNESS PROGRAM WITNESS EMPTY witness program empty witness", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_PROGRAM_WITNESS_EMPTY) == verify_result_witness_program_empty_witness);
}

TEST_CASE("consensus script error to verify result WITNESS PROGRAM MISMATCH witness program mismatch", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_PROGRAM_MISMATCH) == verify_result_witness_program_mismatch);
}

TEST_CASE("consensus script error to verify result WITNESS MALLEATED witness malleated", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_MALLEATED) == verify_result_witness_malleated);
}

TEST_CASE("consensus script error to verify result WITNESS MALLEATED P2SH witness malleated p2sh", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_MALLEATED_P2SH) == verify_result_witness_malleated_p2sh);
}

TEST_CASE("consensus script error to verify result WITNESS UNEXPECTED witness unexpected", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_UNEXPECTED) == verify_result_witness_unexpected);
}

TEST_CASE("consensus script error to verify result WITNESS PUBKEYTYPE witness pubkeytype", "[consensus script error to verify result]") {
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_WITNESS_PUBKEYTYPE) == verify_result_witness_pubkeytype);
}
#endif

// Other

TEST_CASE("consensus script error to verify result OP RETURN op return", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::OP_RETURN) == verify_result_op_return);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_OP_RETURN) == verify_result_op_return);
#endif
}

TEST_CASE("consensus script error to verify result UNKNOWN ERROR unknown error", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::UNKNOWN) == verify_result_unknown_error);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_UNKNOWN_ERROR) == verify_result_unknown_error);
#endif
}

TEST_CASE("consensus script error to verify result ERROR COUNT unknown error", "[consensus script error to verify result]") {
#if defined(KTH_CURRENCY_BCH)
    REQUIRE(script_error_to_verify_result(ScriptError::ERROR_COUNT) == verify_result_unknown_error);
#else
    REQUIRE(script_error_to_verify_result(SCRIPT_ERR_ERROR_COUNT) == verify_result_unknown_error);
#endif
}

// End Test Suite
