// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <kth/consensus.hpp>
#include <test_helpers.hpp>

// These give us test accesss to unpublished symbols.
#include "consensus/consensus.hpp"
#include "script/interpreter.h"

using namespace kth::consensus;

// Start Test Suite: consensus verify flags to script flags

// Unnamed enum values require cast for boost comparison macros.

TEST_CASE("consensus verify flags to script flags none NONE", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_none) == (uint32_t)SCRIPT_VERIFY_NONE);
}

TEST_CASE("consensus verify flags to script flags p2sh P2SH", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_p2sh) == (uint32_t)SCRIPT_VERIFY_P2SH);
}

TEST_CASE("consensus verify flags to script flags strictenc STRICTENC", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_strictenc) == (uint32_t)SCRIPT_VERIFY_STRICTENC);
}

TEST_CASE("consensus verify flags to script flags dersig DERSIG", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_dersig) == (uint32_t)SCRIPT_VERIFY_DERSIG);
}

TEST_CASE("consensus verify flags to script flags low s LOW S", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_low_s) == (uint32_t)SCRIPT_VERIFY_LOW_S);
}

#if ! defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus verify flags to script flags nulldummy NULLDUMMY", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_nulldummy) == (uint32_t)SCRIPT_VERIFY_NULLDUMMY);
}
#endif

TEST_CASE("consensus verify flags to script flags sigpushonly SIGPUSHONLY", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_sigpushonly) == (uint32_t)SCRIPT_VERIFY_SIGPUSHONLY);
}

TEST_CASE("consensus verify flags to script flags minimaldata MINIMALDATA", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_minimaldata) == (uint32_t)SCRIPT_VERIFY_MINIMALDATA);
}

TEST_CASE("consensus verify flags to script flags discourage upgradable nops DISCOURAGE UPGRADABLE NOPS", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_discourage_upgradable_nops) == (uint32_t)SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS);
}

TEST_CASE("consensus verify flags to script flags cleanstack CLEANSTACK", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_cleanstack) == (uint32_t)SCRIPT_VERIFY_CLEANSTACK);
}

TEST_CASE("consensus verify flags to script flags checklocktimeverify CHECKLOCKTIMEVERIFY", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_checklocktimeverify) == (uint32_t)SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY);
}

TEST_CASE("consensus verify flags to script flags checksequenceverify CHECKSEQUENCEVERIFY", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_checksequenceverify) == (uint32_t)SCRIPT_VERIFY_CHECKSEQUENCEVERIFY);
}

#if ! defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus verify flags to script flags witness WITNESS", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_witness) == (uint32_t)SCRIPT_VERIFY_WITNESS);
}

TEST_CASE("consensus verify flags to script flags discourage upgradable witness program DISCOURAGE UPGRADABLE WITNESS PROGRAM", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_discourage_upgradable_witness_program) == (uint32_t)SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM);
}
#endif

TEST_CASE("consensus verify flags to script flags minimal if MINIMALIF", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_minimal_if) == (uint32_t)SCRIPT_VERIFY_MINIMALIF);
}

TEST_CASE("consensus verify flags to script flags null fail NULLFAIL", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_null_fail) == (uint32_t)SCRIPT_VERIFY_NULLFAIL);
}

#if ! defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus verify flags to script flags witness public key compressed PUBKEYTYPE", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_witness_public_key_compressed) == (uint32_t)SCRIPT_VERIFY_WITNESS_PUBKEYTYPE);
}
#endif

#if ! defined(KTH_CURRENCY_BCH)
TEST_CASE("consensus verify flags to script flags const scriptcode SCRIPTCODE", "[consensus verify flags to script flags]") {
    REQUIRE(verify_flags_to_script_flags(verify_flags_const_scriptcode) == (uint32_t)SCRIPT_VERIFY_CONST_SCRIPTCODE);
}
#endif

TEST_CASE("consensus verify flags to script flags all all", "[consensus verify flags to script flags]") {
    const uint32_t all_verify_flags =
          verify_flags_none
        | verify_flags_p2sh
        | verify_flags_strictenc
        | verify_flags_dersig
        | verify_flags_low_s
#if ! defined(KTH_CURRENCY_BCH)
        | verify_flags_nulldummy
#endif
        | verify_flags_sigpushonly
        | verify_flags_minimaldata
        | verify_flags_discourage_upgradable_nops
        | verify_flags_cleanstack
        | verify_flags_checklocktimeverify
        | verify_flags_checksequenceverify
#if ! defined(KTH_CURRENCY_BCH)
        | verify_flags_witness
        | verify_flags_discourage_upgradable_witness_program
#endif
        | verify_flags_minimal_if
        | verify_flags_null_fail
#if ! defined(KTH_CURRENCY_BCH)
        | verify_flags_witness_public_key_compressed
#endif
        ;

    const uint32_t all_script_flags =
          SCRIPT_VERIFY_NONE
        | SCRIPT_VERIFY_P2SH
        | SCRIPT_VERIFY_STRICTENC
        | SCRIPT_VERIFY_DERSIG
        | SCRIPT_VERIFY_LOW_S
#if ! defined(KTH_CURRENCY_BCH)
        | SCRIPT_VERIFY_NULLDUMMY
#endif
        | SCRIPT_VERIFY_SIGPUSHONLY
        | SCRIPT_VERIFY_MINIMALDATA
        | SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS
        | SCRIPT_VERIFY_CLEANSTACK
        | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY
        | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY
#if ! defined(KTH_CURRENCY_BCH)
        | SCRIPT_VERIFY_WITNESS
        | SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM
#endif
        | SCRIPT_VERIFY_MINIMALIF
        | SCRIPT_VERIFY_NULLFAIL
#if ! defined(KTH_CURRENCY_BCH)
        | SCRIPT_VERIFY_WITNESS_PUBKEYTYPE
#endif
        ;

    REQUIRE(verify_flags_to_script_flags(all_verify_flags) == all_script_flags);
}

// End Test Suite
