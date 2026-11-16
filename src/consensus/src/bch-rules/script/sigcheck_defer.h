// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_SCRIPT_SIGCHECK_DEFER_H
#define KTH_CONSENSUS_SCRIPT_SIGCHECK_DEFER_H

// Deferred signature checks (KTH_SIGNATURE_VERIFICATION=secp256k1_turbo).
// KTH_SIGCHECK_DEFER_SCOPE(cond) opens, for the rest of the enclosing block, a
// scope in which CPubKey::VerifyECDSA / VerifySchnorr may record the check for
// later batch verification and report it as valid. The interpreter passes
// cond == true only where a failed check fails the script outright
// (NULLFAIL), never for legacy ECDSA CHECKMULTISIG.
#if defined(KTH_SIGVERIFY_TURBO_BATCH)
#include <secp256k1_turbo/deferred.hpp>
#define KTH_SIGCHECK_DEFER_SCOPE(cond) ::secp256k1_turbo::deferred::allow_scope kth_sigcheck_defer_scope_{(cond)}
#define KTH_SIGCHECK_DEFER_ALLOWED() ::secp256k1_turbo::deferred::allowed()
#else
#define KTH_SIGCHECK_DEFER_SCOPE(cond) static_cast<void>(cond)
#define KTH_SIGCHECK_DEFER_ALLOWED() false
#endif

#endif
