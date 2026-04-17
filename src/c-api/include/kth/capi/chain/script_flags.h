// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file is auto-generated. Do not edit manually.

#ifndef KTH_CAPI_CHAIN_SCRIPT_FLAGS_H_
#define KTH_CAPI_CHAIN_SCRIPT_FLAGS_H_

#include <stdint.h>

#include <kth/capi/primitives.h>
#include <kth/capi/visibility.h>

#ifdef __cplusplus
extern "C" {
#endif

// C enums cannot represent 64-bit values, so we use typedef + #define.
typedef uint64_t kth_script_flags_t;


#define kth_script_flags_no_rules                          0

/// Allow minimum difficulty blocks (hard fork, testnet).
#define kth_script_flags_easy_blocks                       (1ULL << 0)

/// Pay-to-script-hash enabled (soft fork, feature).
#define kth_script_flags_bip16_rule                        (1ULL << 1)

/// No duplicated unspent transaction ids (hard fork, security).
#define kth_script_flags_bip30_rule                        (1ULL << 2)

/// Coinbase must include height (soft fork, security).
#define kth_script_flags_bip34_rule                        (1ULL << 3)

/// Strict DER signatures required (soft fork, security).
#define kth_script_flags_bip66_rule                        (1ULL << 4)

/// Operation nop2 becomes check locktime verify (soft fork, feature).
#define kth_script_flags_bip65_rule                        (1ULL << 5)

/// Hard code bip34-based activation heights (hard fork, optimization).
#define kth_script_flags_bip90_rule                        (1ULL << 6)

/// Assume hash collisions cannot happen (hard fork, optimization).
#define kth_script_flags_allow_collisions                  (1ULL << 7)

/// Enforce relative locktime (soft fork, feature).
#define kth_script_flags_bip68_rule                        (1ULL << 8)

/// Operation nop3 becomes check sequence verify (soft fork, feature).
#define kth_script_flags_bip112_rule                       (1ULL << 9)

/// Use median time past for locktime (soft fork, feature).
#define kth_script_flags_bip113_rule                       (1ULL << 10)

#if defined(KTH_CURRENCY_BCH)
// Individual BCH feature flags
#define kth_script_flags_bch_strictenc                     (1ULL << 11)   // 2017-Aug UAHF (1501590000)
#define kth_script_flags_bch_sighash_forkid                (1ULL << 12)   // 2017-Aug UAHF (1501590000)
#define kth_script_flags_bch_low_s                         (1ULL << 13)   // 2017-Nov DAA/cw-144 (1510600000)
#define kth_script_flags_bch_nullfail                      (1ULL << 14)   // 2017-Nov DAA/cw-144 (1510600000)
#define kth_script_flags_bch_reactivated_opcodes           (1ULL << 15)   // 2018-May pythagoras (1526400000)
#define kth_script_flags_bch_sigpushonly                   (1ULL << 16)   // 2018-Nov euclid (1542300000)
#define kth_script_flags_bch_cleanstack                    (1ULL << 17)   // 2018-Nov euclid (1542300000)
#define kth_script_flags_bch_schnorr_multisig              (1ULL << 18)   // 2019-Nov mersenne (1573819200)
#define kth_script_flags_bch_minimaldata                   (1ULL << 19)   // 2019-Nov mersenne (1573819200)
#define kth_script_flags_bch_enforce_sigchecks             (1ULL << 20)   // 2020-May fermat (1589544000)
#define kth_script_flags_bch_64bit_integers                (1ULL << 21)   // 2022-May gauss (1652616000) - also gates MUL opcode
#define kth_script_flags_bch_native_introspection          (1ULL << 22)   // 2022-May gauss (1652616000)
#define kth_script_flags_bch_p2sh_32                       (1ULL << 23)   // 2023-May descartes (1684152000)
#define kth_script_flags_bch_tokens                        (1ULL << 24)   // 2023-May descartes (1684152000)
#define kth_script_flags_bch_vm_limits                     (1ULL << 25)   // 2025-May galois (1747310400)
#define kth_script_flags_bch_bigint                        (1ULL << 26)   // 2025-May galois (1747310400)
#define kth_script_flags_bch_loops                         (1ULL << 27)   // 2026-May leibniz (1778846400) - OP_BEGIN, OP_UNTIL
#define kth_script_flags_bch_subroutines                   (1ULL << 28)   // 2026-May leibniz (1778846400) - OP_DEFINE, OP_INVOKE
#define kth_script_flags_bch_bitwise_ops                   (1ULL << 29)   // 2026-May leibniz (1778846400) - OP_INVERT, shifts
#define kth_script_flags_bch_p2s                           (1ULL << 30)   // 2026-May leibniz (1778846400) - Pay-to-Script
#define kth_script_flags_bch_2027_may                      (1ULL << 31)   // 2027-May cantor (xxxxxxxxx)

// Policy/standardness flags (grow from bit 61 downward, not enforced in block validation)
#define kth_script_flags_bch_minimalif                     (1ULL << 58)   // OP_IF/NOTIF minimal encoding (BCHN: SCRIPT_VERIFY_MINIMALIF)
#define kth_script_flags_bch_input_sigchecks               (1ULL << 59)   // per-input sigcheck density limit (BCHN: SCRIPT_VERIFY_INPUT_SIGCHECKS)
#define kth_script_flags_bch_discourage_upgradable_nops    (1ULL << 60)   // rejects NOP1, NOP4-NOP10
#define kth_script_flags_bch_disallow_segwit_recovery      (1ULL << 61)   // disables segwit recovery exemption
#else
// Just for segwit coins
/// Segregated witness consensus layer (soft fork, feature).
#define kth_script_flags_bip141_rule                       (1ULL << 11)

/// Segregated witness v0 verification (soft fork, feature).
#define kth_script_flags_bip143_rule                       (1ULL << 12)

/// Prevent dummy value malleability (soft fork, feature).
#define kth_script_flags_bip147_rule                       (1ULL << 13)
#endif

/// Perform difficulty retargeting (hard fork, regtest).
#define kth_script_flags_retarget                          (1ULL << 62)

/// Sentinel bit to indicate tx has not been validated.
#define kth_script_flags_unverified                        (1ULL << 63)

/// Policy/standardness flags mask (bits 58-61, grow downward).
#define kth_script_flags_all_policy_flags                  (kth_script_flags_bch_disallow_segwit_recovery | kth_script_flags_bch_discourage_upgradable_nops | kth_script_flags_bch_input_sigchecks | kth_script_flags_bch_minimalif)

/// All consensus rules (excludes policy flags, retarget, and unverified).
#define kth_script_flags_all_rules                         (0xffffffffffffffff & ~kth_script_flags_all_policy_flags & ~kth_script_flags_retarget & ~kth_script_flags_unverified)

/// All bits set (consensus + policy + internal).
#define kth_script_flags_all_bits                          0xffffffffffffffff

// Cumulative upgrade bitmasks (for chain_state activation).
// In C there is no type-safety distinction between these and individual flags.

/// Rules that use bip34-based activation.
#define kth_upgrade_bip34_activations                      ( kth_script_flags_bip34_rule | kth_script_flags_bip65_rule | kth_script_flags_bip66_rule)

#if ! defined(KTH_CURRENCY_BCH)
/// Rules that use BIP9 bit zero first time activation.
#define kth_upgrade_bip9_bit0_group                        ( kth_script_flags_bip68_rule | kth_script_flags_bip112_rule | kth_script_flags_bip113_rule)

/// Rules that use BIP9 bit one first time activation.
#define kth_upgrade_bip9_bit1_group                        ( kth_script_flags_bip141_rule | kth_script_flags_bip143_rule | kth_script_flags_bip147_rule)
#endif

#if defined(KTH_CURRENCY_BCH)
#define kth_upgrade_bch_uahf                               (kth_script_flags_bch_strictenc | kth_script_flags_bch_sighash_forkid)
#define kth_upgrade_bch_daa_cw144                          (kth_upgrade_bch_uahf | kth_script_flags_bch_low_s | kth_script_flags_bch_nullfail)
#define kth_upgrade_bch_pythagoras                         (kth_upgrade_bch_daa_cw144 | kth_script_flags_bch_reactivated_opcodes)
#define kth_upgrade_bch_euclid                             (kth_upgrade_bch_pythagoras | kth_script_flags_bch_sigpushonly | kth_script_flags_bch_cleanstack)
#define kth_upgrade_bch_pisano                             kth_upgrade_bch_euclid   // schnorr_checksig not gated
#define kth_upgrade_bch_mersenne                           (kth_upgrade_bch_pisano | kth_script_flags_bch_schnorr_multisig | kth_script_flags_bch_minimaldata)
// Note: bch_minimalif is NOT included in any upgrade — BCHN never activates SCRIPT_VERIFY_MINIMALIF
// Note: bch_input_sigchecks is NOT included — it's a standardness check (BCHN: SCRIPT_VERIFY_INPUT_SIGCHECKS)
#define kth_upgrade_bch_fermat                             (kth_upgrade_bch_mersenne | kth_script_flags_bch_enforce_sigchecks)
#define kth_upgrade_bch_euler                              kth_upgrade_bch_fermat   // no new script features
#define kth_upgrade_bch_gauss                              (kth_upgrade_bch_euler | kth_script_flags_bch_64bit_integers | kth_script_flags_bch_native_introspection)
#define kth_upgrade_bch_descartes                          (kth_upgrade_bch_gauss | kth_script_flags_bch_p2sh_32 | kth_script_flags_bch_tokens)
#define kth_upgrade_bch_lobachevski                        kth_upgrade_bch_descartes   // no new script features
#define kth_upgrade_bch_galois                             (kth_upgrade_bch_lobachevski | kth_script_flags_bch_vm_limits | kth_script_flags_bch_bigint)
#define kth_upgrade_bch_leibniz                            (kth_upgrade_bch_galois | kth_script_flags_bch_loops | kth_script_flags_bch_subroutines | kth_script_flags_bch_bitwise_ops | kth_script_flags_bch_p2s)
#define kth_upgrade_bch_cantor                             (kth_upgrade_bch_leibniz | kth_script_flags_bch_2027_may)
#endif

// Standalone script flag constants.
#define kth_script_flags_standard_policy_flags             kth_script_flags_all_policy_flags

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* KTH_CAPI_CHAIN_SCRIPT_FLAGS_H_ */
