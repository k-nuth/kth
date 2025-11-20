// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_CONSENSUS_EXPORT_HPP
#define KTH_CONSENSUS_EXPORT_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include <kth/consensus/define.hpp>
#include <kth/consensus/version.hpp>

namespace kth::consensus {


/**
 * Result values from calling verify_script.
 */
typedef enum verify_result_type {
    // Logical result
    verify_result_eval_false = 0,
    verify_result_eval_true,

    // Max size errors
    verify_result_script_size,
    verify_result_push_size,
    verify_result_op_count,
    verify_result_stack_size,
    verify_result_sig_count,
    verify_result_pubkey_count,

    // Failed verify operations
    verify_result_verify,
    verify_result_equalverify,
    verify_result_checkmultisigverify,
    verify_result_checksigverify,
    verify_result_numequalverify,

    // Logical/Format/Canonical errors
    verify_result_bad_opcode,
    verify_result_disabled_opcode,
    verify_result_invalid_stack_operation,
    verify_result_invalid_altstack_operation,
    verify_result_unbalanced_conditional,

    // BIP62 errors
    verify_result_sig_hashtype,
    verify_result_sig_der,
    verify_result_minimaldata,
    verify_result_sig_pushonly,
    verify_result_sig_high_s,

#if ! defined(KTH_CURRENCY_BCH)
    verify_result_sig_nulldummy,
#endif

    verify_result_pubkeytype,
    verify_result_cleanstack,
    verify_result_minimalif,
    verify_result_sig_nullfail,
    verify_result_minimalnum,

    // Softfork safeness
    verify_result_discourage_upgradable_nops,

#if ! defined(KTH_CURRENCY_BCH)
    verify_result_discourage_upgradable_witness_program,
#endif

    // Other
    verify_result_op_return,
    verify_result_unknown_error,

#if ! defined(KTH_CURRENCY_BCH)
    // Segregated witness
    verify_result_witness_program_wrong_length,
    verify_result_witness_program_empty_witness,
    verify_result_witness_program_mismatch,
    verify_result_witness_malleated,
    verify_result_witness_malleated_p2sh,
    verify_result_witness_unexpected,
    verify_result_witness_pubkeytype,
#endif

    // augmention codes for tx deserialization
    verify_result_tx_invalid,
    verify_result_tx_size_invalid,
    verify_result_tx_input_invalid,

    // BIP65/BIP112 (shared codes)
    verify_result_negative_locktime,
    verify_result_unsatisfied_locktime


#if defined(KTH_CURRENCY_BCH)
    , verify_result_input_sigchecks

    , verify_result_invalid_operand_size
    , verify_result_invalid_number_range
    , verify_result_impossible_encoding
    , verify_result_invalid_split_range
    , verify_result_invalid_bit_count

    , verify_result_checkdatasigverify

    , verify_result_div_by_zero
    , verify_result_mod_by_zero

    , verify_result_invalid_bitfield_size
    , verify_result_invalid_bit_range

    , verify_result_sig_badlength
    , verify_result_sig_nonschnorr

    , verify_result_illegal_forkid
    , verify_result_must_use_forkid

    , verify_result_sigchecks_limit_exceeded
#endif

#if ! defined(KTH_CURRENCY_BCH)
    , verify_result_op_codeseparator
    , verify_result_sig_findanddelete
#endif


} verify_result;

// BCH Flags
// SCRIPT_VERIFY_NONE = 0,
// SCRIPT_VERIFY_P2SH = (1U << 0),
// SCRIPT_VERIFY_STRICTENC = (1U << 1),
// SCRIPT_VERIFY_DERSIG = (1U << 2),
// SCRIPT_VERIFY_LOW_S = (1U << 3),
// SCRIPT_VERIFY_SIGPUSHONLY = (1U << 5),
// SCRIPT_VERIFY_MINIMALDATA = (1U << 6),
// SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS = (1U << 7),
// SCRIPT_VERIFY_CLEANSTACK = (1U << 8),
// SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY = (1U << 9),
// SCRIPT_VERIFY_CHECKSEQUENCEVERIFY = (1U << 10),
// SCRIPT_VERIFY_MINIMALIF = (1U << 13),
// SCRIPT_VERIFY_NULLFAIL = (1U << 14),
// SCRIPT_ENABLE_SIGHASH_FORKID = (1U << 16),
// SCRIPT_DISALLOW_SEGWIT_RECOVERY = (1U << 20),
// SCRIPT_ENABLE_SCHNORR_MULTISIG = (1U << 21),
// SCRIPT_VERIFY_INPUT_SIGCHECKS = (1U << 22),
// SCRIPT_ENFORCE_SIGCHECKS = (1U << 23),
// SCRIPT_64_BIT_INTEGERS = (1U << 24),
// SCRIPT_NATIVE_INTROSPECTION = (1U << 25),
// SCRIPT_ENABLE_P2SH_32 = (1U << 26),
// SCRIPT_ENABLE_TOKENS = (1U << 27),
// SCRIPT_ENABLE_MAY2025 = (1U << 28),
// SCRIPT_VM_LIMITS_STANDARD = (1U << 29),

/**
 * Flags to use when calling verify_script.
 */
typedef enum verify_flags_type {
    /**
     * Set no flags.
     */
    verify_flags_none = 0,

    /**
     * Evaluate P2SH subscripts (softfork safe, BIP16).
     */
    verify_flags_p2sh = (1U << 0),

    /**
     * Passing a non-strict-DER signature or one with undefined hashtype to a
     * checksig operation causes script failure. Evaluating a pubkey that is
     * not (0x04 + 64 bytes) or (0x02 or 0x03 + 32 bytes) by checksig causes
     * script failure. (softfork safe, but not used or intended as a consensus
     * rule).
     */
    verify_flags_strictenc = (1U << 1),

    /**
     * Passing a non-strict-DER signature to a checksig operation causes script
     * failure (softfork safe, BIP62 rule 1, BIP66).
     */
    verify_flags_dersig = (1U << 2),

    /**
     * Passing a non-strict-DER signature or one with S > order/2 to a checksig
     * operation causes script failure
     * (softfork safe, BIP62 rule 5).
     */
    verify_flags_low_s = (1U << 3),

#if ! defined(KTH_CURRENCY_BCH)
    /**
     * verify dummy stack item consumed by CHECKMULTISIG is of zero-length
     * (softfork safe, BIP62 rule 7, BIP147).
     */
    verify_flags_nulldummy = (1U << 4),
#endif

    /**
     * Using a non-push operator in the scriptSig causes script failure
     * (softfork safe, BIP62 rule 2).
     */
    verify_flags_sigpushonly = (1U << 5),

    /**
     * Require minimal encodings for all push operations (OP_0... OP_16,
     * OP_1NEGATE where possible, direct pushes up to 75 bytes, OP_PUSHDATA
     * up to 255 bytes, OP_PUSHDATA2 for anything larger). Evaluating any other
     * push causes the script to fail (BIP62 rule 3). In addition, whenever a
     * stack element is interpreted as a number, it must be of minimal length
     * (BIP62 rule 4).(softfork safe)
     */
    verify_flags_minimaldata = (1U << 6),

    /**
     * Discourage use of NOPs reserved for upgrades (NOP1,3-10)
     * Provided so that nodes can avoid accepting or mining transactions
     * containing executed NOP's whose meaning may change after a soft-fork,
     * thus rendering the script invalid; with this flag set executing
     * discouraged NOPs fails the script. This verification flag will never be
     * a mandatory flag applied to scripts in a block. NOPs that are not
     * executed, e.g.  within an unexecuted IF ENDIF block, are *not* rejected.
     */
    verify_flags_discourage_upgradable_nops = (1U << 7),

    /**
     * Require that only a single stack element remains after evaluation. This
     * changes the success criterion from "At least one stack element must
     * remain, and when interpreted as a boolean, it must be true" to "Exactly
     * one stack element must remain, and when interpreted as a boolean, it
     * must be true". (softfork safe, BIP62 rule 6)
     * Note: verify_flags_cleanstack must be used with verify_flags_p2sh.
     */
    verify_flags_cleanstack = (1U << 8),

    /**
     * Verify CHECKLOCKTIMEVERIFY, see BIP65 for details.
     */
    verify_flags_checklocktimeverify = (1U << 9),

    /**
     * Verify CHECKSEQUENCEVERIFY, see BIP112 for details.
     */
    verify_flags_checksequenceverify = (1U << 10)

    /**
     * SCRIPT_VERIFY_MINIMALIF (bip141 p2wsh policy).
     */
    , verify_flags_minimal_if = (1U << 13)

    /**
     * SCRIPT_VERIFY_NULLFAIL (bip141 global policy, bip146 soft fork).
     */
    , verify_flags_null_fail = (1U << 14)

#if defined(KTH_CURRENCY_BCH)
    // BCH only flags
    /**
     * SCRIPT_ENABLE_SIGHASH_FORKID (BCH).
     */
    , verify_flags_enable_sighash_forkid = (1U << 16)

    /**
     * SCRIPT_DISALLOW_SEGWIT_RECOVERY (BCH).
     */
    , verify_flags_disallow_segwit_recovery = (1U << 20)

    /**
     * SCRIPT_ENABLE_SCHNORR_MULTISIG (BCH).
     */
    , verify_flags_enable_schnorr_multisig = (1U << 21)

    /**
     * SCRIPT_VERIFY_INPUT_SIGCHECKS (BCH).
     */
    , verify_flags_input_sigchecks = (1U << 22)

    /**
     * SCRIPT_ENFORCE_SIGCHECKS (BCH).
     */
    , verify_flags_enforce_sigchecks = (1U << 23)

    /**
     * SCRIPT_64_BIT_INTEGERS (BCH).
     */
    , verify_flags_64_bit_integers = (1U << 24)

    /**
     * SCRIPT_NATIVE_INTROSPECTION (BCH).
     */
    , verify_flags_native_introspection = (1U << 25)

    /**
     * SCRIPT_ENABLE_P2SH_32 (BCH).
     */
    , verify_flags_enable_p2sh_32 = (1U << 26)

    /**
     * SCRIPT_ENABLE_TOKENS (BCH).
     */
    , verify_flags_enable_tokens = (1U << 27)

    /**
     * SCRIPT_ENABLE_MAY2025 (BCH).
     */
    , verify_flags_enable_may2025 = (1U << 28)

    /**
     * SCRIPT_VM_LIMITS_STANDARD (BCH).
     */
    , verify_flags_enable_vm_limits_standard = (1U << 29)

#else
    // BTC only flags

    /**
     * SCRIPT_VERIFY_WITNESS (bip141).
     */
    , verify_flags_witness = (1U << 11)

    /**
     * SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM (bip141 policy).
     */
    , verify_flags_discourage_upgradable_witness_program = (1U << 12)

    /**
     * SCRIPT_VERIFY_WITNESS_PUBKEYTYPE (bip141/bip143 p2wsh/p2wpkh policy).
     */
    , verify_flags_witness_public_key_compressed = (1U << 15)

    /**
     * SCRIPT_VERIFY_CONST_SCRIPTCODE
     */
    , verify_flags_const_scriptcode = (1U << 16)
#endif
} verify_flags;

/**
 * Verify that the transaction input correctly spends the previous output,
 * considering any additional constraints specified by flags.
 * @param[in]  transaction            The transaction with the script to verify.
 * @param[in]  transaction_size       The byte length of the transaction.
 * @param[in]  locking_script_data    The bytes of the locking script.
 * @param[in]  locking_script_size    The byte length of the locking script.
 * @param[in]  unlocking_script_data  The bytes of the unlocking script.
 * @param[in]  unlocking_script_size  The byte length of the unlocking script.
 * @param[in]  tx_input_index         The zero-based index of the transaction
 *                                    input with signature to be verified.
 * @param[in]  flags                  Verification constraint flags.
 * @param[in]  amount               . Just for BCH, not for BTC nor LTC.
 * @returns                           A script verification result code.
 */

 KC_API verify_result_type verify_script(
    const unsigned char* transaction,
    size_t transaction_size,
    const unsigned char* locking_script_data,
    size_t locking_script_size,
    const unsigned char* unlocking_script_data,
    size_t unlocking_script_size,
    unsigned int tx_input_index,
    unsigned int flags,
    size_t& sig_checks,
    int64_t amount,
    std::vector<std::vector<uint8_t>> coins);

} // namespace kth::consensus

#endif
