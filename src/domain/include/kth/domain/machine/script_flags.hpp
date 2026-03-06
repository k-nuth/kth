// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DOMAIN_MACHINE_SCRIPT_FLAGS_HPP_
#define KTH_DOMAIN_MACHINE_SCRIPT_FLAGS_HPP_

#include <cstdint>

namespace kth::domain {

using script_flags_t = uint64_t;

// Strong type for cumulative upgrade bitmasks.
// Intentionally NOT implicitly convertible to/from script_flags_t, so that
// is_enabled(script_flags_t, upgrade_t) does not compile — preventing the bug
// where a cumulative mask is checked with ANY-bit semantics.
// Use (upgrade & flags) to get a script_flags_t with the filtered feature bits.
enum class upgrade_t : script_flags_t {};

// upgrade | upgrade -> upgrade (for building cumulative masks)
constexpr upgrade_t operator|(upgrade_t a, upgrade_t b) {
    return static_cast<upgrade_t>(static_cast<script_flags_t>(a) | static_cast<script_flags_t>(b));
}

// upgrade | script_flags_t -> upgrade (for combining with individual enum bits)
constexpr upgrade_t operator|(upgrade_t a, script_flags_t b) {
    return static_cast<upgrade_t>(static_cast<script_flags_t>(a) | b);
}

// Convert an upgrade to its raw script_flags_t value (e.g. in tests to pass as active flags).
constexpr script_flags_t to_flags(upgrade_t upgrade) {
    return static_cast<script_flags_t>(upgrade);
}

namespace machine {

enum script_flags : script_flags_t {
    no_rules = 0,

    /// Allow minimum difficulty blocks (hard fork, testnet).
    easy_blocks = 1ULL << 0,

    /// Pay-to-script-hash enabled (soft fork, feature).
    bip16_rule = 1ULL << 1,

    /// No duplicated unspent transaction ids (hard fork, security).
    bip30_rule = 1ULL << 2,

    /// Coinbase must include height (soft fork, security).
    bip34_rule = 1ULL << 3,

    /// Strict DER signatures required (soft fork, security).
    bip66_rule = 1ULL << 4,

    /// Operation nop2 becomes check locktime verify (soft fork, feature).
    bip65_rule = 1ULL << 5,

    /// Hard code bip34-based activation heights (hard fork, optimization).
    bip90_rule = 1ULL << 6,

    /// Assume hash collisions cannot happen (hard fork, optimization).
    allow_collisions = 1ULL << 7,

    /// Enforce relative locktime (soft fork, feature).
    bip68_rule = 1ULL << 8,

    /// Operation nop3 becomes check sequence verify (soft fork, feature).
    bip112_rule = 1ULL << 9,

    /// Use median time past for locktime (soft fork, feature).
    bip113_rule = 1ULL << 10,

#if defined(KTH_CURRENCY_BCH)
    // Individual BCH feature flags
    bch_strictenc            = 1ULL << 11,   //2017-Aug UAHF (1501590000)
    bch_sighash_forkid       = 1ULL << 12,   //2017-Aug UAHF (1501590000)
    bch_low_s                = 1ULL << 13,   //2017-Nov DAA/cw-144 (1510600000)
    bch_nullfail             = 1ULL << 14,   //2017-Nov DAA/cw-144 (1510600000)
    bch_reactivated_opcodes  = 1ULL << 15,   //2018-May pythagoras (1526400000)
    bch_sigpushonly          = 1ULL << 16,   //2018-Nov euclid (1542300000)
    bch_cleanstack           = 1ULL << 17,   //2018-Nov euclid (1542300000)
    bch_schnorr_multisig     = 1ULL << 18,   //2019-Nov mersenne (1573819200)
    bch_minimaldata          = 1ULL << 19,   //2019-Nov mersenne (1573819200)
    bch_enforce_sigchecks    = 1ULL << 20,   //2020-May fermat (1589544000)
    bch_64bit_integers       = 1ULL << 21,   //2022-May gauss (1652616000) - also gates MUL opcode
    bch_native_introspection = 1ULL << 22,   //2022-May gauss (1652616000)
    bch_p2sh_32              = 1ULL << 23,   //2023-May descartes (1684152000)
    bch_tokens               = 1ULL << 24,   //2023-May descartes (1684152000)
    //TODO(fernando): descartes also had tx-level features (restrict tx version, min tx size) - check if they need bits
    bch_vm_limits            = 1ULL << 25,   //2025-May galois (1747310400)
    bch_bigint               = 1ULL << 26,   //2025-May galois (1747310400)
    bch_loops                = 1ULL << 27,   //2026-May leibniz (1778846400) - OP_BEGIN, OP_UNTIL
    bch_subroutines          = 1ULL << 28,   //2026-May leibniz (1778846400) - OP_DEFINE, OP_INVOKE
    bch_bitwise_ops          = 1ULL << 29,   //2026-May leibniz (1778846400) - OP_INVERT, shifts
    bch_p2s                  = 1ULL << 30,   //2026-May leibniz (1778846400) - Pay-to-Script
    bch_2027_may             = 1ULL << 31,   //2027-May cantor (xxxxxxxxx)

    // Policy/standardness flags (grow from bit 61 downward, not enforced in block validation)
    bch_minimalif                  = 1ULL << 58, // OP_IF/NOTIF minimal encoding (BCHN: SCRIPT_VERIFY_MINIMALIF)
    bch_input_sigchecks            = 1ULL << 59, // per-input sigcheck density limit (BCHN: SCRIPT_VERIFY_INPUT_SIGCHECKS)
    bch_discourage_upgradable_nops = 1ULL << 60, // rejects NOP1, NOP4-NOP10
    bch_disallow_segwit_recovery   = 1ULL << 61, // disables segwit recovery exemption
#else
    // Just for segwit coins
    /// Segregated witness consensus layer (soft fork, feature).
    bip141_rule = 1ULL << 11,

    /// Segregated witness v0 verification (soft fork, feature).
    bip143_rule = 1ULL << 12,

    /// Prevent dummy value malleability (soft fork, feature).
    bip147_rule = 1ULL << 13,
#endif //KTH_CURRENCY_BCH

    /// Perform difficulty retargeting (hard fork, regtest).
    retarget = 1ULL << 62,

    /// Sentinel bit to indicate tx has not been validated.
    unverified = 1ULL << 63,

    /// Policy/standardness flags mask (bits 58-61, grow downward).
    all_policy_flags = bch_disallow_segwit_recovery | bch_discourage_upgradable_nops
                     | bch_input_sigchecks | bch_minimalif,

    /// All consensus rules (excludes policy flags, retarget, and unverified).
    all_rules = 0xffffffffffffffff & ~all_policy_flags & ~retarget & ~unverified,

    /// All bits set (consensus + policy + internal).
    all_bits = 0xffffffffffffffff
};

// Cumulative bitmask groupings (upgrade_t — cannot be passed to is_enabled).
// Use (upgrade & flags) to filter, or to_flags() to convert explicitly.
namespace upgrade {
    /// Rules that use bip34-based activation.
    inline constexpr auto bip34_activations = static_cast<upgrade_t>(
        script_flags::bip34_rule |
        script_flags::bip65_rule |
        script_flags::bip66_rule);

#if ! defined(KTH_CURRENCY_BCH)
    /// Rules that use BIP9 bit zero first time activation.
    inline constexpr auto bip9_bit0_group = static_cast<upgrade_t>(
        script_flags::bip68_rule |
        script_flags::bip112_rule |
        script_flags::bip113_rule);

    /// Rules that use BIP9 bit one first time activation.
    inline constexpr auto bip9_bit1_group = static_cast<upgrade_t>(
        script_flags::bip141_rule |
        script_flags::bip143_rule |
        script_flags::bip147_rule);
#endif

#if defined(KTH_CURRENCY_BCH)
    inline constexpr auto bch_uahf        = static_cast<upgrade_t>(script_flags::bch_strictenc | script_flags::bch_sighash_forkid);
    inline constexpr auto bch_daa_cw144   = bch_uahf | script_flags::bch_low_s | script_flags::bch_nullfail;
    inline constexpr auto bch_pythagoras  = bch_daa_cw144 | script_flags::bch_reactivated_opcodes;
    inline constexpr auto bch_euclid      = bch_pythagoras | script_flags::bch_sigpushonly | script_flags::bch_cleanstack;
    inline constexpr auto bch_pisano      = bch_euclid;                   // schnorr_checksig not gated
    inline constexpr auto bch_mersenne    = bch_pisano | script_flags::bch_schnorr_multisig | script_flags::bch_minimaldata;
    // Note: bch_minimalif is NOT included in any upgrade — BCHN never activates SCRIPT_VERIFY_MINIMALIF
    // Note: bch_input_sigchecks is NOT included — it's a standardness check (BCHN: SCRIPT_VERIFY_INPUT_SIGCHECKS)
    inline constexpr auto bch_fermat      = bch_mersenne | script_flags::bch_enforce_sigchecks;
    inline constexpr auto bch_euler       = bch_fermat;                   // no new script features
    inline constexpr auto bch_gauss       = bch_euler | script_flags::bch_64bit_integers | script_flags::bch_native_introspection;
    inline constexpr auto bch_descartes   = bch_gauss | script_flags::bch_p2sh_32 | script_flags::bch_tokens;
    inline constexpr auto bch_lobachevski = bch_descartes;                // no new script features
    inline constexpr auto bch_galois      = bch_lobachevski | script_flags::bch_vm_limits | script_flags::bch_bigint;
    inline constexpr auto bch_leibniz     = bch_galois | script_flags::bch_loops | script_flags::bch_subroutines | script_flags::bch_bitwise_ops | script_flags::bch_p2s;
    inline constexpr auto bch_cantor      = bch_leibniz | script_flags::bch_2027_may;
#endif // KTH_CURRENCY_BCH
} // namespace upgrade

#if defined(KTH_CURRENCY_BCH)
// Standard (mempool) policy flags — not enforced in block validation.
// Standard (mempool) policy flags — not enforced in block validation.
// Matches BCHN's STANDARD_NOT_MANDATORY_VERIFY_FLAGS.
inline constexpr script_flags_t standard_policy_flags = script_flags::all_policy_flags;
#endif // KTH_CURRENCY_BCH

} // namespace machine
} // namespace kth::domain

#endif // KTH_DOMAIN_MACHINE_SCRIPT_FLAGS_HPP_
