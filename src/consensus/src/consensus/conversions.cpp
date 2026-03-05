// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/consensus/conversions.hpp>

#include "script/script_flags.h"

namespace kth::consensus {

// This mapping decouples the consensus API from the satoshi implementation
// files. We prefer to keep our copies of consensus files isomorphic.
// This function is not published (but non-static for testability).
unsigned int verify_flags_to_script_flags(unsigned int flags) {
    unsigned int script_flags = SCRIPT_VERIFY_NONE;

    if ((flags & verify_flags_p2sh) != 0) {
        script_flags |= SCRIPT_VERIFY_P2SH;
    }

    if ((flags & verify_flags_strictenc) != 0) {
        script_flags |= SCRIPT_VERIFY_STRICTENC;
    }

    if ((flags & verify_flags_dersig) != 0) {
        script_flags |= SCRIPT_VERIFY_DERSIG;
    }

    if ((flags & verify_flags_low_s) != 0) {
        script_flags |= SCRIPT_VERIFY_LOW_S;
    }

    if ((flags & verify_flags_sigpushonly) != 0) {
        script_flags |= SCRIPT_VERIFY_SIGPUSHONLY;
    }

    if ((flags & verify_flags_minimaldata) != 0) {
        script_flags |= SCRIPT_VERIFY_MINIMALDATA;
    }

    if ((flags & verify_flags_discourage_upgradable_nops) != 0) {
        script_flags |= SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS;
    }

    if ((flags & verify_flags_cleanstack) != 0) {
        script_flags |= SCRIPT_VERIFY_CLEANSTACK;
    }

    if ((flags & verify_flags_checklocktimeverify) != 0) {
        script_flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
    }

    if ((flags & verify_flags_checksequenceverify) != 0) {
        script_flags |= SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
    }

    if ((flags & verify_flags_null_fail) != 0) {
        script_flags |= SCRIPT_VERIFY_NULLFAIL;
    }

    // Removed
    // if ((flags & verify_flags_compressed_pubkeytype) != 0)
    //     script_flags |= SCRIPT_VERIFY_COMPRESSED_PUBKEYTYPE;

    if ((flags & verify_flags_enable_sighash_forkid) != 0) {
        script_flags |= SCRIPT_ENABLE_SIGHASH_FORKID;
    }

    if ((flags & verify_flags_disallow_segwit_recovery) != 0)
        script_flags |= SCRIPT_DISALLOW_SEGWIT_RECOVERY;

    if ((flags & verify_flags_enable_schnorr_multisig) != 0)
        script_flags |= SCRIPT_ENABLE_SCHNORR_MULTISIG;

    if ((flags & verify_flags_input_sigchecks) != 0)
        script_flags |= SCRIPT_VERIFY_INPUT_SIGCHECKS;

    if ((flags & verify_flags_enforce_sigchecks) != 0)
        script_flags |= SCRIPT_ENFORCE_SIGCHECKS;

    if ((flags & verify_flags_64_bit_integers) != 0)
        script_flags |= SCRIPT_64_BIT_INTEGERS;

    if ((flags & verify_flags_native_introspection) != 0)
        script_flags |= SCRIPT_NATIVE_INTROSPECTION;

    if ((flags & verify_flags_enable_p2sh_32) != 0)
        script_flags |= SCRIPT_ENABLE_P2SH_32;

    if ((flags & verify_flags_enable_tokens) != 0)
        script_flags |= SCRIPT_ENABLE_TOKENS;

    if ((flags & verify_flags_enable_may2025) != 0)
        script_flags |= SCRIPT_ENABLE_MAY2025;

    if ((flags & verify_flags_enable_may2026) != 0)
        script_flags |= SCRIPT_ENABLE_MAY2026;

    return script_flags;
}


} // namespace kth::consensus
