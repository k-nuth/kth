#!/usr/bin/env python3
"""
Script to parse Bitcoin Cash Node script_tests.json and generate C++ header file.

This script converts the JSON test data into a C++ header with appropriate data structures
for the script validation test framework.

Usage:
python3 generate_script_tests.py --knuth-overrides knuth_overrides.json /Users/fernando/dev/bchn/bitcoin-cash-node-master/src/test/data/script_tests.json output

"""

import json
import sys
import os
import argparse
import re
from typing import List, Tuple, Optional, Union


# Mapping from JSON flag names to verify_flags_type enum values
# Not used, FORK_MAP is used instead to map flags to C++ fork expressions
FLAG_MAP = {
    "NONE": "verify_flags_none",
    "P2SH": "verify_flags_p2sh", 
    "STRICTENC": "verify_flags_strictenc",
    "DERSIG": "verify_flags_dersig",
    "LOW_S": "verify_flags_low_s",
    "NULLDUMMY": "verify_flags_nulldummy",
    "SIGPUSHONLY": "verify_flags_sigpushonly",
    "MINIMALDATA": "verify_flags_minimaldata",
    "DISCOURAGE_UPGRADABLE_NOPS": "verify_flags_discourage_upgradable_nops",
    "CLEANSTACK": "verify_flags_cleanstack",
    "CHECKLOCKTIMEVERIFY": "verify_flags_checklocktimeverify",
    "CHECKSEQUENCEVERIFY": "verify_flags_checksequenceverify",
    "WITNESS": "verify_flags_witness",
    "DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM": "verify_flags_discourage_upgradable_witness_program",
    "MINIMALIF": "verify_flags_minimal_if",
    "NULLFAIL": "verify_flags_null_fail",
    "WITNESS_PUBKEYTYPE": "verify_flags_witness_public_key_compressed",
    "CONST_SCRIPTCODE": "verify_flags_const_scriptcode",
    "SIGHASH_FORKID": "verify_flags_enable_sighash_forkid",
    "DISALLOW_SEGWIT_RECOVERY": "verify_flags_disallow_segwit_recovery",
    "SCHNORR_MULTISIG": "verify_flags_enable_schnorr_multisig",
    "INPUT_SIGCHECKS": "verify_flags_input_sigchecks",
    "ENFORCE_SIGCHECKS": "verify_flags_enforce_sigchecks",
    "64_BIT_INTEGERS": "verify_flags_64_bit_integers",
    "NATIVE_INTROSPECTION": "verify_flags_native_introspection",
    "P2SH_32": "verify_flags_enable_p2sh_32",
    "ENABLE_TOKENS": "verify_flags_enable_tokens",
    "ENABLE_MAY2025": "verify_flags_enable_may2025",
    "VM_LIMITS_STANDARD": "verify_flags_enable_vm_limits_standard",
}

# Mapping from JSON flag names to fork rules
# Based on kth::domain::machine::rule_fork enum and convert_flags function
FORK_MAP = {
    # BIP rules (1 to 1 mapping)
    "P2SH": "kth::domain::machine::rule_fork::bip16_rule",
    "CHECKLOCKTIMEVERIFY": "kth::domain::machine::rule_fork::bip65_rule", 
    "DERSIG": "kth::domain::machine::rule_fork::bip66_rule",
    "CHECKSEQUENCEVERIFY": "kth::domain::machine::rule_fork::bip112_rule",
    
    # BCH fork rules (1 fork -> multiple flags, any flag activates the fork)
    "STRICTENC": "kth::domain::machine::rule_fork::bch_uahf",
    "SIGHASH_FORKID": "kth::domain::machine::rule_fork::bch_uahf",
    
    "LOW_S": "kth::domain::machine::rule_fork::bch_daa_cw144",
    "NULLFAIL": "kth::domain::machine::rule_fork::bch_daa_cw144",
    
    "SIGPUSHONLY": "kth::domain::machine::rule_fork::bch_euclid",
    "CLEANSTACK": "kth::domain::machine::rule_fork::bch_euclid",
    
    "SCHNORR_MULTISIG": "kth::domain::machine::rule_fork::bch_mersenne",
    "MINIMALDATA": "kth::domain::machine::rule_fork::bch_mersenne",
    
    "ENFORCE_SIGCHECKS": "kth::domain::machine::rule_fork::bch_fermat",
    
    "64_BIT_INTEGERS": "kth::domain::machine::rule_fork::bch_gauss",
    "NATIVE_INTROSPECTION": "kth::domain::machine::rule_fork::bch_gauss",
    
    "P2SH_32": "kth::domain::machine::rule_fork::bch_descartes",
    "ENABLE_TOKENS": "kth::domain::machine::rule_fork::bch_descartes",
    
    "ENABLE_MAY2025": "kth::domain::machine::rule_fork::bch_galois",
    
    # Knuth-specific fictitious fork for pythagoras operations
    "KTH_PYTHAGORAS": "kth::domain::machine::rule_fork::bch_pythagoras",
    
    # BTC-only rules (when not KTH_CURRENCY_BCH)
    "WITNESS": "kth::domain::machine::rule_fork::bip141_rule",
    "DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM": "kth::domain::machine::rule_fork::bip141_rule",
    "WITNESS_PUBKEYTYPE": "kth::domain::machine::rule_fork::bip141_rule",
    "CONST_SCRIPTCODE": "kth::domain::machine::rule_fork::bip141_rule",
    
    "NULLDUMMY": "kth::domain::machine::rule_fork::bip147_rule",
    
    # Flags that don't map to specific forks (policy flags)
    "NONE": None,
    "MINIMALIF": None,  # Policy flag, not a fork rule
    "DISCOURAGE_UPGRADABLE_NOPS": None,  # Policy flag
    "INPUT_SIGCHECKS": None,  # Policy flag
    "VM_LIMITS_STANDARD": None,  # Policy flag
    "DISALLOW_SEGWIT_RECOVERY": None,  # Policy flag
}

# Fork hierarchy - ordered from lowest to highest value
# Higher forks include the functionality of lower forks
FORK_HIERARCHY = [
    "kth::domain::machine::rule_fork::no_rules",
    "kth::domain::machine::rule_fork::bip16_rule", 
    "kth::domain::machine::rule_fork::bch_uahf",
    "kth::domain::machine::rule_fork::bch_daa_cw144",
    "kth::domain::machine::rule_fork::bch_euclid",
    "kth::domain::machine::rule_fork::bch_mersenne",
    "kth::domain::machine::rule_fork::bch_fermat",
    "kth::domain::machine::rule_fork::bch_gauss",
    "kth::domain::machine::rule_fork::bch_descartes",
    "kth::domain::machine::rule_fork::bch_galois",
]

# Mapping from JSON error names to kth::error::error_code_t enum
# Based on the convert_result mapping from verify_result_type to kd::error codes
ERROR_MAP = {
    # Logical true result
    "OK": "kth::error::success",
    
    # Logical false result
    "EVAL_FALSE": "kth::error::stack_false",
    
    # Max size errors -> invalid_script
    "SCRIPT_SIZE": "kth::error::invalid_script",
    "PUSH_SIZE": "kth::error::invalid_push_data_size", 
    "OP_COUNT": "kth::error::invalid_script",
    "STACK_SIZE": "kth::error::invalid_script",
    "SIG_COUNT": "kth::error::invalid_script",
    "PUBKEY_COUNT": "kth::error::invalid_script",
    "INPUT_SIGCHECKS": "kth::error::invalid_script",
    
    # Failed verify operations -> invalid_script
    "VERIFY": "kth::error::op_verify_empty_stack",
    "EQUALVERIFY": "kth::error::invalid_script",
    "CHECKMULTISIGVERIFY": "kth::error::invalid_script",
    "CHECKSIGVERIFY": "kth::error::invalid_script",
    "CHECKDATASIGVERIFY": "kth::error::invalid_script",
    "NUMEQUALVERIFY": "kth::error::invalid_script",
    
    # Logical/Format/Canonical errors -> invalid_script
    "BAD_OPCODE": "kth::error::op_reserved",
    "DISABLED_OPCODE": "kth::error::op_disabled",
    "INVALID_STACK_OPERATION": "kth::error::invalid_script",
    "INVALID_ALTSTACK_OPERATION": "kth::error::op_from_alt_stack",
    "UNBALANCED_CONDITIONAL": "kth::error::invalid_stack_scope",
    # "OP_ENDIF": "kth::error::op_endif",
    # "OP_ELSE": "kth::error::op_else",
    
    # Operand/Number/Bit errors -> invalid_script
    "OPERAND_SIZE": "kth::error::invalid_script",
    "INVALID_NUMBER_RANGE": "kth::error::invalid_script",
    "INVALID_NUMBER_RANGE_64_BIT": "kth::error::invalid_script",
    "INVALID_NUMBER_RANGE_BIG_INT": "kth::error::invalid_script",
    "IMPOSSIBLE_ENCODING": "kth::error::invalid_script",
    "SPLIT_RANGE": "kth::error::invalid_script",
    "INVALID_BIT_COUNT": "kth::error::invalid_script",
    "DIV_BY_ZERO": "kth::error::invalid_script",
    "MOD_BY_ZERO": "kth::error::invalid_script",
    "BITFIELD_SIZE": "kth::error::invalid_script",
    "BIT_RANGE": "kth::error::invalid_script",
    
    # BIP65/BIP112 locktime errors -> descriptive names
    "NEGATIVE_LOCKTIME": "kth::error::negative_locktime",
    "UNSATISFIED_LOCKTIME": "kth::error::unsatisfied_locktime",
    
    "OP_RETURN": "kth::error::op_return",

    # Other errors -> invalid_script
    "UNKNOWN_ERROR": "kth::error::invalid_script",
    
    # BIP66 errors (BCH: invalid_signature_encoding, BTC: operation_failed)
    "SIG_DER": "kth::error::invalid_signature_encoding",  # For BCH, may need adjustment for BTC
    
    # BIP62 signature validation errors
    "SIG_HASHTYPE": "kth::error::sig_hashtype",
    "MINIMALDATA": "kth::error::minimaldata", 
    "SIG_PUSHONLY": "kth::error::sig_pushonly",
    "SIG_HIGH_S": "kth::error::sig_high_s",
    "SIG_NULLFAIL": "kth::error::sig_nullfail",  # SIG_NULLDUMMY equivalent
    "NULLFAIL": "kth::error::sig_nullfail",      # Alternative name
    "PUBKEYTYPE": "kth::error::pubkey_type",
    "CLEANSTACK": "kth::error::cleanstack",
    "MINIMALIF": "kth::error::minimalif",
    "MINIMALNUM": "kth::error::minimal_number",
    "STRICTENC": "kth::error::strict_encoding",     # BIP62 related
    
    # Fork ID errors
    "SIGHASH_FORKID": "kth::error::sighash_forkid",  # When used as error code
    
    # Softfork safeness -> operation_failed
    "DISCOURAGE_UPGRADABLE_NOPS": "kth::error::operation_failed",
    "DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM": "kth::error::operation_failed",  # BTC only
    
    # Schnorr/Fork specific errors
    "SIG_BADLENGTH": "kth::error::sig_badlength",
    "SIG_NONSCHNORR": "kth::error::sig_nonschnorr",
    "ILLEGAL_FORKID": "kth::error::illegal_forkid",
    "MUST_USE_FORKID": "kth::error::must_use_forkid",
    "MISSING_FORKID": "kth::error::missing_forkid",
    
    # BCH specific errors -> invalid_script (best guess)
    "SIGCHECKS_LIMIT_EXCEEDED": "kth::error::invalid_script",
    "CONTEXT_NOT_PRESENT": "kth::error::invalid_script",
    "LIMITED_CONTEXT_NO_SIBLING_INFO": "kth::error::invalid_script",
    "INVALID_TX_INPUT_INDEX": "kth::error::invalid_script",
    "INVALID_TX_OUTPUT_INDEX": "kth::error::invalid_script",
    "OP_COST": "kth::error::invalid_script",
    "HASH_ITERS": "kth::error::invalid_script", 
    "CONDITIONAL_STACK_DEPTH": "kth::error::invalid_script",
    
    # Segregated witness errors (BTC only) -> invalid_script
    "WITNESS_PROGRAM_WRONG_LENGTH": "kth::error::invalid_script",
    "WITNESS_PROGRAM_EMPTY_WITNESS": "kth::error::invalid_script",
    "WITNESS_PROGRAM_MISMATCH": "kth::error::invalid_script",
    "WITNESS_MALLEATED": "kth::error::invalid_script",
    "WITNESS_MALLEATED_P2SH": "kth::error::invalid_script",
    "WITNESS_UNEXPECTED": "kth::error::invalid_script",
    "WITNESS_PUBKEYTYPE": "kth::error::invalid_script",
    
    # Transaction errors -> invalid_script
    "TX_INVALID": "kth::error::invalid_script",
    "TX_SIZE_INVALID": "kth::error::invalid_script", 
    "TX_INPUT_INVALID": "kth::error::invalid_script",

    # Knuth-specific operation errors
    "KTH_OP_VERIFY": "kth::error::op_verify",
    "KTH_OP_VERIFY_EMPTY_STACK": "kth::error::op_verify_empty_stack",
    "KTH_OP_VERIFY_FAILED": "kth::error::op_verify_failed",
    "KTH_OP_CAT": "kth::error::op_cat",
    "KTH_OP_SPLIT": "kth::error::op_split", 
    "KTH_OP_REVERSE_BYTES": "kth::error::op_reverse_bytes",
    "KTH_IFDUP_INVALID_STACK_OPERATION": "kth::error::ifdup_invalid_stack_operation",
    "KTH_DROP_INVALID_STACK_OPERATION": "kth::error::drop_invalid_stack_operation",
    "KTH_DUP_INVALID_STACK_OPERATION": "kth::error::dup_invalid_stack_operation",
    "KTH_NIP_INVALID_STACK_OPERATION": "kth::error::nip_invalid_stack_operation",
    "KTH_OVER_INVALID_STACK_OPERATION": "kth::error::over_invalid_stack_operation",
    "KTH_PICK_INVALID_STACK_OPERATION": "kth::error::pick_invalid_stack_operation",
    "KTH_ROLL_INVALID_STACK_OPERATION": "kth::error::roll_invalid_stack_operation",
    "KTH_ROT_INVALID_STACK_OPERATION": "kth::error::rot_invalid_stack_operation",
    "KTH_SWAP_INVALID_STACK_OPERATION": "kth::error::swap_invalid_stack_operation",
    "KTH_TUCK_INVALID_STACK_OPERATION": "kth::error::tuck_invalid_stack_operation",
    "KTH_DUP2_INVALID_STACK_OPERATION": "kth::error::dup2_invalid_stack_operation",
    "KTH_DUP3_INVALID_STACK_OPERATION": "kth::error::dup3_invalid_stack_operation",
    "KTH_OVER2_INVALID_STACK_OPERATION": "kth::error::over2_invalid_stack_operation",
    "KTH_SWAP2_INVALID_STACK_OPERATION": "kth::error::swap2_invalid_stack_operation",
    "KTH_STACK_TO_ALT_OVERFLOW": "kth::error::stack_to_alt_overflow",
    "KTH_STACK_FROM_ALT_UNDERFLOW": "kth::error::stack_from_alt_underflow",
    "KTH_OP_EQUAL_VERIFY_INSUFFICIENT_STACK": "kth::error::op_equal_verify_insufficient_stack",
    "KTH_OP_EQUAL_VERIFY_FAILED": "kth::error::op_equal_verify_failed",
    "KTH_OP_NUM_EQUAL": "kth::error::op_num_equal",
    "KTH_OP_NUM_EQUAL_VERIFY_INSUFFICIENT_STACK": "kth::error::op_num_equal_verify_insufficient_stack",
    "KTH_OP_NUM_EQUAL_VERIFY_FAILED": "kth::error::op_num_equal_verify_failed",
    "KTH_OP_CHECK_SIG_VERIFY_FAILED": "kth::error::op_check_sig_verify_failed",
    "KTH_OP_ENDIF": "kth::error::op_endif",
    "KTH_OP_ELSE": "kth::error::op_else",
    "KTH_OP_DIV": "kth::error::op_div",
    "KTH_OP_DIV_BY_ZERO": "kth::error::op_div_by_zero",
    "KTH_OP_MOD": "kth::error::op_mod",
    "KTH_OP_MOD_BY_ZERO": "kth::error::op_mod_by_zero",
    "KTH_OP_MOD": "kth::error::op_mod",
    "KTH_INVALID_SCRIPT": "kth::error::invalid_script",
    
    # Knuth-specific stack operation errors that map to INVALID_STACK_OPERATION in BCHN
    "KTH_OP_IF_DUP": "kth::error::op_if_dup",
    "KTH_OP_DROP": "kth::error::op_drop", 
    "KTH_OP_DUP": "kth::error::op_dup",
    "KTH_OP_NIP": "kth::error::op_nip",
    "KTH_OP_OVER": "kth::error::op_over",
    "KTH_OP_PICK": "kth::error::op_pick",
    "KTH_OP_ROLL": "kth::error::op_roll",
    "KTH_OP_ROT": "kth::error::op_rot",
    "KTH_OP_SWAP": "kth::error::op_swap",
    "KTH_OP_TUCK": "kth::error::op_tuck",
    "KTH_OP_DUP2": "kth::error::op_dup2",
    "KTH_OP_DUP3": "kth::error::op_dup3",
    "KTH_OP_OVER2": "kth::error::op_over2",
    "KTH_OP_SWAP2": "kth::error::op_swap2",
    "KTH_OP_CAT": "kth::error::op_cat",
    "KTH_OP_SPLIT": "kth::error::op_split",
    
    # Knuth-specific num2bin/bin2num operation errors
    "KTH_OP_NUM2BIN": "kth::error::op_num2bin",
    "KTH_OP_NUM2BIN_INVALID_SIZE": "kth::error::op_num2bin_invalid_size",
    "KTH_OP_NUM2BIN_SIZE_EXCEEDED": "kth::error::op_num2bin_size_exceeded",
    "KTH_OP_NUM2BIN_IMPOSSIBLE_ENCODING": "kth::error::op_num2bin_impossible_encoding",
    "KTH_OP_BIN2NUM": "kth::error::op_bin2num",
    "KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE": "kth::error::op_bin2num_invalid_number_range",
    
    # Knuth-specific bitwise operation errors
    "KTH_OP_SIZE": "kth::error::op_size",
    "KTH_OP_AND": "kth::error::op_and",
    "KTH_OP_OR": "kth::error::op_or",
    "KTH_OP_XOR": "kth::error::op_xor",
    
    # Knuth-specific signature operation errors
    "KTH_OP_CHECKDATASIG": "kth::error::op_check_data_sig",
    "KTH_OP_CHECKDATASIGVERIFY": "kth::error::op_check_data_sig_verify",
    "KTH_OP_EQUAL": "kth::error::op_equal",
    
    # Knuth-specific arithmetic operation errors
    "KTH_OP_ADD": "kth::error::op_add",
    "KTH_OP_NOT": "kth::error::op_not",
    "KTH_OP_MUL": "kth::error::op_mul",
    "KTH_OP_MUL_OVERFLOW": "kth::error::op_mul_overflow",
    
    # Knuth-specific hash operation errors
    "KTH_OP_RIPEMD160": "kth::error::op_ripemd160",
    "KTH_OP_SHA1": "kth::error::op_sha1",
    "KTH_OP_SHA256": "kth::error::op_sha256",
    "KTH_OP_HASH160": "kth::error::op_hash160",
    "KTH_OP_HASH256": "kth::error::op_hash256",
    
    # Knuth-specific introspection operation errors
    "KTH_OP_INPUT_INDEX": "kth::error::op_input_index",
    "KTH_OP_ACTIVE_BYTECODE": "kth::error::op_active_bytecode",
    "KTH_OP_TX_VERSION": "kth::error::op_tx_version",
    "KTH_OP_TX_INPUT_COUNT": "kth::error::op_tx_input_count",
    "KTH_OP_TX_OUTPUT_COUNT": "kth::error::op_tx_output_count",
    "KTH_OP_TX_LOCKTIME": "kth::error::op_tx_locktime",
    "KTH_OP_UTXO_VALUE": "kth::error::op_utxo_value",
    "KTH_OP_UTXO_BYTECODE": "kth::error::op_utxo_bytecode",
    "KTH_OP_OUTPOINT_TX_HASH": "kth::error::op_outpoint_tx_hash",
    "KTH_OP_OUTPOINT_INDEX": "kth::error::op_outpoint_index",
    "KTH_OP_INPUT_BYTECODE": "kth::error::op_input_bytecode",
    "KTH_OP_INPUT_SEQUENCE_NUMBER": "kth::error::op_input_sequence_number",
    "KTH_OP_OUTPUT_VALUE": "kth::error::op_output_value",
    "KTH_OP_OUTPUT_BYTECODE": "kth::error::op_output_bytecode",
}


def parse_flags(flags_str: str) -> Tuple[str, List[str]]:
    """
    Parse flag string and return C++ fork expression and list of errors.
    
    Args:
        flags_str: Comma-separated list of flags
        
    Returns:
        Tuple of (cpp_expression, error_list)
    """
    if not flags_str or flags_str == "NONE":
        return "kth::domain::machine::rule_fork::no_rules", []  # Base fork - no rules active
    
    flags = [flag.strip() for flag in flags_str.split(",")]
    active_forks = set()  # Use set to avoid duplicates
    errors = []
    
    for flag in flags:
        if flag in FORK_MAP:
            fork = FORK_MAP[flag]
            if fork is not None:  # Some flags don't map to forks (policy flags)
                active_forks.add(fork)
        else:
            errors.append(f"Unknown flag: {flag}")
    
    if not active_forks:
        return "kth::domain::machine::rule_fork::no_rules", errors  # No forks active
    elif len(active_forks) == 1:
        return list(active_forks)[0], errors
    else:
        # Multiple forks active - select the highest fork (superior fork includes lower ones)
        highest_fork = select_highest_fork(active_forks)
        return highest_fork, errors


def select_highest_fork(forks: set) -> str:
    """
    Select the highest fork from a set of active forks.
    Higher forks include the functionality of lower forks.
    
    Args:
        forks: Set of fork strings
        
    Returns:
        The highest fork string
    """
    highest_index = -1
    highest_fork = "kth::domain::machine::rule_fork::no_rules"
    
    for fork in forks:
        try:
            index = FORK_HIERARCHY.index(fork)
            if index > highest_index:
                highest_index = index
                highest_fork = fork
        except ValueError:
            # Fork not in hierarchy, this shouldn't happen with our current mapping
            continue
    
    return highest_fork


def parse_error(error_str: str) -> Tuple[str, List[str]]:
    """
    Parse error string and return C++ enum value and list of errors.
    
    Args:
        error_str: Error code string
        
    Returns:
        Tuple of (cpp_enum_value, error_list)
    """
    if error_str in ERROR_MAP:
        return ERROR_MAP[error_str], []
    else:
        return f"/* UNKNOWN_ERROR: {error_str} */ kth::error::unknown", [f"Unknown error code: {error_str}"]


def escape_string(s) -> str:
    """Escape a string for C++ string literal."""
    # Convert to string if it's not already
    s = str(s) if s is not None else ""
    
    # Replace backslashes and quotes
    s = s.replace('\\', '\\\\')
    s = s.replace('"', '\\"')
    s = s.replace('\n', '\\n')
    s = s.replace('\r', '\\r')
    s = s.replace('\t', '\\t')
    return s


def transform_script_encoding(script: str) -> str:
    """
    Transform script encoding from '0xNN 0x...' format to '[hex]' format.
    Also transform PUSHDATA opcodes to special notation.
    
    Examples:
    - "0x01 0x0b" -> "[0b]"
    - "0x02 0x417a" -> "[417a]"
    - "PUSHDATA1 0x01 0x07" -> "[1.07]"
    - "PUSHDATA2 0x0100 0x08" -> "[2.08]"
    - "PUSHDATA4 0x01000000 0x09" -> "[4.09]"
    
    Args:
        script: Original script string
        
    Returns:
        Transformed script string
    """
    if not script or not isinstance(script, str):
        return str(script) if script is not None else ""
    
    # Handle PUSHDATA opcodes first
    # PUSHDATA1: 1 byte size specification
    pushdata1_pattern = r'PUSHDATA1\s+0x([0-9a-fA-F]{1,2})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata1(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        return f"[1.{data_hex}]"
    script = re.sub(pushdata1_pattern, replace_pushdata1, script, flags=re.IGNORECASE)
    
    # PUSHDATA2: 2 bytes size specification  
    pushdata2_pattern = r'PUSHDATA2\s+0x([0-9a-fA-F]{1,4})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata2(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        return f"[2.{data_hex}]"
    script = re.sub(pushdata2_pattern, replace_pushdata2, script, flags=re.IGNORECASE)
    
    # PUSHDATA4: 4 bytes size specification
    pushdata4_pattern = r'PUSHDATA4\s+0x([0-9a-fA-F]{1,8})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata4(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        return f"[4.{data_hex}]"
    script = re.sub(pushdata4_pattern, replace_pushdata4, script, flags=re.IGNORECASE)
    
    # Handle 0x01 N format where N is 1-16 (convert to OP_N opcodes)
    # Pattern: 0x01 followed by numbers 1-16
    opcode_pattern = r'0x01\s+([1-9]|1[0-6])'
    
    def replace_opcode_match(match):
        number = int(match.group(1))
        # OP_1 = 0x51, OP_2 = 0x52, ..., OP_16 = 0x60
        opcode_hex = format(0x50 + number, '02x')
        return f"[{opcode_hex}]"
    
    # Apply opcode transformation first
    transformed = re.sub(opcode_pattern, replace_opcode_match, script)
    
    # Handle regular 0xNN 0x... format (for non-PUSHDATA cases)
    # Pattern to match: 0xNN 0x[hex_data]  
    # Where NN is the length in hex, and hex_data is the actual data
    pattern = r'0x([0-9a-fA-F]{1,2})\s+0x([0-9a-fA-F]+)'
    
    def replace_match(match):
        length_hex = match.group(1)
        data_hex = match.group(2)
        
        # Convert length from hex to decimal
        try:
            expected_length = int(length_hex, 16)
            actual_length = len(data_hex) // 2  # Each byte is 2 hex characters
            
            # Verify that the length matches (optional validation)
            if expected_length == actual_length:
                return f"[{data_hex}]"
            else:
                # If lengths don't match, keep original format
                return match.group(0)
        except ValueError:
            # If conversion fails, keep original format
            return match.group(0)
    
    # Apply the hex transformation to the already transformed script
    final_transformed = re.sub(pattern, replace_match, transformed)
    return final_transformed


def smart_lowercase_script(script: str) -> str:
    """
    Apply lowercase only to opcodes, not to hex data or quoted strings.
    
    This function preserves the case of:
    - Hex data in brackets [hex]
    - Hex literals 0x...
    - String literals 'text'
    - Pure hex data
    
    Only opcodes are converted to lowercase.
    
    Args:
        script: Original script string
        
    Returns:
        Script with opcodes in lowercase but hex data and strings preserved
    """
    if not script or not isinstance(script, str):
        return str(script) if script is not None else ""
    
    # First, apply manual opcode renames using search & replace
    # This handles all variants: OP_1ADD, 1ADD, etc.
    manual_renames = [
        ('OP_1ADD', 'add1'),
        ('1ADD', 'add1'),
        ('OP_1SUB', 'sub1'), 
        ('1SUB', 'sub1'),
        ('OP_2MUL', 'mul2'),
        ('2MUL', 'mul2'),
        ('OP_2DIV', 'div2'),
        ('2DIV', 'div2')
    ]
    
    # Apply case-insensitive replacements
    working_script = script
    for old_opcode, new_opcode in manual_renames:
        # Use word boundaries to avoid partial matches
        pattern = r'\b' + re.escape(old_opcode) + r'\b'
        working_script = re.sub(pattern, new_opcode, working_script, flags=re.IGNORECASE)
    
    # Find and preserve quoted strings
    
    # Find all quoted strings and their positions
    quoted_strings = {}
    placeholder_pattern = "____QUOTED_STRING_{}____"
    
    # Replace quoted strings with placeholders
    def replace_quotes(match):
        index = len(quoted_strings)
        placeholder = placeholder_pattern.format(index)
        quoted_strings[placeholder] = match.group(0)
        return placeholder
    
    # Replace all quoted strings with placeholders
    temp_script = re.sub(r"'[^']*'", replace_quotes, working_script)
    
    # Now process tokens normally (opcodes to lowercase, preserve hex)
    tokens = temp_script.split()
    processed_tokens = []
    
    for token in tokens:
        # Check if token is a placeholder for a quoted string
        if token in quoted_strings:
            processed_tokens.append(quoted_strings[token])
        # Check if token is hex data in brackets [hex] or PUSHDATA notation [X.hex] - preserve case
        elif token.startswith('[') and token.endswith(']'):
            processed_tokens.append(token)
        # Check if token is hex literal 0x... - preserve case  
        elif token.startswith('0x'):
            processed_tokens.append(token)
        # Check if token is pure hex data (only hex chars) - preserve case
        elif re.match(r'^[0-9a-fA-F]+$', token) and len(token) % 2 == 0 and len(token) > 2:
            processed_tokens.append(token)
        # Otherwise it's likely an opcode - make lowercase and remove OP_ prefix
        else:
            token_lower = token.lower()
            # Remove OP_ prefix if present
            if token_lower.startswith('op_'):
                processed_tokens.append(token_lower[3:])  # Remove 'op_'
            else:
                processed_tokens.append(token_lower)
    
    return ' '.join(processed_tokens)


def should_skip_test(script_sig: str, script_pub_key: str, skip_tests: List[dict]) -> Tuple[bool, str]:
    """
    Check if a test should be skipped based on skip_tests configuration.
    
    Args:
        script_sig: Script signature
        script_pub_key: Script public key
        skip_tests: List of skip test configurations
        
    Returns:
        Tuple of (should_skip, reason)
    """
    for skip_entry in skip_tests:
        if (str(script_sig) == skip_entry.get('script_sig', '') and 
            str(script_pub_key) == skip_entry.get('script_pub_key', '')):
            return True, skip_entry.get('reason', 'Test skipped')
    return False, ''


def process_test_entry(entry: List, line_number: int, knuth_overrides: dict = None, skip_tests: List[dict] = None) -> Tuple[Optional[str], List[str], bool]:
    """
    Process a single test entry from the JSON array.
    
    Args:
        entry: JSON array entry
        line_number: Line number in JSON file for error reporting
        knuth_overrides: Dictionary of Knuth-specific error overrides
        skip_tests: List of test configurations to skip
        
    Returns:
        Tuple of (cpp_code_line, error_list, was_manually_skipped)
    """
    # Initialize parameters if None
    if knuth_overrides is None:
        knuth_overrides = {}
    if skip_tests is None:
        skip_tests = []
        
    # Skip comment entries (arrays with strings that start with descriptions)
    if len(entry) == 1 and isinstance(entry[0], str):
        return None, [], False
    
    # Skip if not enough elements
    if len(entry) < 4:
        return None, [], False
    
    # Skip description lines
    if isinstance(entry[0], str) and (
        entry[0].startswith("Format is:") or
        entry[0].startswith("It is evaluated") or
        entry[0].startswith("pushes as") or
        entry[0].startswith("followed by") or
        entry[0].startswith("correct prevout") or
        entry[0].startswith("nSequences")
    ):
        return None, [], False
    
    try:
        script_sig = entry[0]
        script_pub_key = entry[1] 
        
        # Check if this test should be skipped
        should_skip, skip_reason = should_skip_test(script_sig, script_pub_key, skip_tests)
        if should_skip:
            return None, [], True  # Manually skipped test
        
        # Handle different formats:
        # Format 1: [script_sig, script_pub_key, flags, error, comment]
        # Format 2: [script_sig, script_pub_key, "", error, comment]  (no flags)
        
        if len(entry) >= 5:
            flags_str = entry[2]
            expected_error = entry[3]
            comment = entry[4]
        elif len(entry) == 4:
            flags_str = entry[2]
            expected_error = entry[3]
            comment = ""
        else:
            return None, [f"Line {line_number}: Unexpected entry format"], False
        
        # If the flags field is empty or looks like a script, treat as no flags
        if not flags_str or flags_str == "" or ("0x" in flags_str and "CHECKSIG" in flags_str):
            flags_str = "NONE"
        
        # Parse flags to forks
        forks_cpp, flag_errors = parse_flags(flags_str)
        
        # Parse error
        error_cpp, error_errors = parse_error(expected_error)
        
        # Collect all errors
        all_errors = flag_errors + error_errors
        
        # Transform script encodings and apply smart lowercase (opcodes only, preserve hex data case)
        transformed_sig = transform_script_encoding(str(script_sig))
        transformed_pubkey = transform_script_encoding(str(script_pub_key))
        
        # Apply smart lowercase to opcodes only
        smart_lowered_sig = smart_lowercase_script(transformed_sig)
        smart_lowered_pubkey = smart_lowercase_script(transformed_pubkey)
        
        # Check for specific arithmetic operations and adjust error if needed
        if error_cpp == "kth::error::invalid_script":
            # Check both scripts for arithmetic operations
            combined_scripts = smart_lowered_sig + " " + smart_lowered_pubkey
            
            # Use word boundaries to ensure we match complete opcodes only
            import re
            
            # Check for add operations (add, add1)
            if re.search(r'\b(add|add1)\b', combined_scripts):
                # error_cpp = "kth::error::op_add_overflow"
                error_cpp = "kth::error::op_add"
            # Check for sub operations (sub, sub1) 
            elif re.search(r'\b(sub|sub1)\b', combined_scripts):
                # error_cpp = "kth::error::op_sub_underflow"
                error_cpp = "kth::error::op_sub"
        
        # Apply Knuth overrides if specified
        if knuth_overrides:
            # Try to find override using different key combinations
            override_data = None
            override_key_used = None
            
            # First, try with fork-specific key
            fork_specific_key = (str(script_sig), str(script_pub_key), flags_str)
            if fork_specific_key in knuth_overrides:
                override_data = knuth_overrides[fork_specific_key]
                override_key_used = fork_specific_key
            else:
                # Then try with scripts-only key
                scripts_only_key = (str(script_sig), str(script_pub_key))
                if scripts_only_key in knuth_overrides:
                    override_data = knuth_overrides[scripts_only_key]
                    override_key_used = scripts_only_key
            
            if override_data is not None:
                
                # Handle both old format (string) and new format (dict)
                if isinstance(override_data, str):
                    # Old format: just error override
                    override_error = override_data
                    override_comment = f" [KNUTH_OVERRIDE: {override_error}]"
                    # Parse the override error
                    override_error_cpp, override_error_errors = parse_error(override_error)
                    error_cpp = override_error_cpp
                    all_errors.extend(override_error_errors)
                    comment += override_comment
                else:
                    # New format: dict with error and/or script overrides
                    override_comment_parts = []
                    
                    # Override error if specified
                    if 'knuth_error' in override_data:
                        override_error = override_data['knuth_error']
                        override_error_cpp, override_error_errors = parse_error(override_error)
                        error_cpp = override_error_cpp
                        all_errors.extend(override_error_errors)
                        override_comment_parts.append(f"ERROR: {override_error}")
                    
                    # Override script_sig if specified
                    if 'knuth_script_sig' in override_data:
                        script_sig = override_data['knuth_script_sig']
                        # Re-process the new script
                        transformed_sig = transform_script_encoding(str(script_sig))
                        smart_lowered_sig = smart_lowercase_script(transformed_sig)
                        override_comment_parts.append("SIG_REPLACED")
                    
                    # Override script_pub_key if specified  
                    if 'knuth_script_pub_key' in override_data:
                        script_pub_key = override_data['knuth_script_pub_key']
                        # Re-process the new script
                        transformed_pubkey = transform_script_encoding(str(script_pub_key))
                        smart_lowered_pubkey = smart_lowercase_script(transformed_pubkey)
                        override_comment_parts.append("PUBKEY_REPLACED")
                    
                    # Override fork if specified
                    if 'knuth_fork' in override_data:
                        override_fork = override_data['knuth_fork']
                        override_forks_cpp, override_fork_errors = parse_flags(override_fork)
                        forks_cpp = override_forks_cpp
                        all_errors.extend(override_fork_errors)
                        override_comment_parts.append(f"FORK: {override_fork}")
                    
                    # Add override comment
                    if override_comment_parts:
                        comment += f" [KNUTH_OVERRIDE: {', '.join(override_comment_parts)}]"
        
        escaped_sig = escape_string(smart_lowered_sig)
        escaped_pubkey = escape_string(smart_lowered_pubkey) 
        escaped_comment = escape_string(comment)
        
        cpp_line = f'    {{"{escaped_sig}", "{escaped_pubkey}", {forks_cpp}, {error_cpp}, "{escaped_comment}"}}'
        
        # Add comma right after the closing brace
        cpp_line += ","
        
        # Always add original flags and expected error as a comment for validation
        flags_and_error_comment = f" // flags: {flags_str}, expected: {expected_error}"
        cpp_line += flags_and_error_comment
        
        # Add error comments if needed
        if all_errors:
            error_comment = " | ERRORS: " + "; ".join(all_errors)
            cpp_line += error_comment
            
        return cpp_line, all_errors, False
        
    except (IndexError, TypeError) as e:
        return None, [f"Line {line_number}: Failed to parse entry: {e}"], False


def generate_header(test_entries: List[str], errors: List[str], skip_tests: List[dict] = None, chunk_size: int = 100) -> str:
    """Generate the complete C++ header file content with chunked arrays."""
    
    header = '''// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers  
// Copyright (c) 2017-2024 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated from script_tests.json
// DO NOT EDIT MANUALLY

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

// Forward declarations - adjust includes as needed for your project
namespace kth::error {
    enum error_code_t;
}

namespace kth::domain::machine {
    enum rule_fork : uint32_t;
}

/**
 * Structure representing a single script test case.
 */
struct script_test {
    std::string script_sig;         ///< The scriptSig (input script)
    std::string script_pub_key;     ///< The scriptPubKey (output script)  
    uint32_t forks;                 ///< Active fork rules (OR of kth::domain::machine::rule_fork values)
    kth::error::error_code_t expected_error; ///< Expected error code
    std::string comment;            ///< Test description/comment
};

using script_test_list = std::vector<script_test>;

'''
    
    # Split test_entries into chunks
    chunks = [test_entries[i:i + chunk_size] for i in range(0, len(test_entries), chunk_size)]
    
    # Generate individual arrays
    for i, chunk in enumerate(chunks):
        header += f'/**\n * Script test chunk {i} (tests {i * chunk_size} to {min((i + 1) * chunk_size - 1, len(test_entries) - 1)})\n */\n'
        header += f'script_test_list const script_tests_from_json_{i}{{\n'
        
        if chunk:
            # Remove comma from the last entry in this chunk
            last_entry = chunk[-1]
            if last_entry.find('},') != -1:
                chunk[-1] = last_entry.replace('},', '}', 1)
            
            header += '\n'.join(chunk) + '\n'
        
        header += '};\n\n'
    
    # Generate array of arrays
    header += '/**\n * Array of all test chunks for easy iteration\n */\n'
    header += 'std::vector<script_test_list const*> const all_script_test_chunks{\n'
    
    for i in range(len(chunks)):
        comma = ',' if i < len(chunks) - 1 else ''
        header += f'    &script_tests_from_json_{i}{comma}\n'
    
    header += '};\n\n'
    
    # Generate helper function for testing
    header += '''/**
 * Run tests on a specific chunk with progress output
 */
template<typename TestFunc>
void run_script_tests_chunked(TestFunc test_func) {
    for (size_t chunk_idx = 0; chunk_idx < all_script_test_chunks.size(); ++chunk_idx) {
        std::println("Testing chunk {} (", chunk_idx, 
                  << all_script_test_chunks[chunk_idx]->size() << " tests)..." << std::endl;
        
        for (size_t test_idx = 0; test_idx < all_script_test_chunks[chunk_idx]->size(); ++test_idx) {
            const auto& test = (*all_script_test_chunks[chunk_idx])[test_idx];
            test_func(test, chunk_idx, test_idx);
        }
        
        std::println("Chunk {} completed.", chunk_idx);
    }
}

/**
 * Get total number of tests across all chunks
 */
inline size_t get_total_script_tests_count() {
    size_t total = 0;
    for (const auto* chunk : all_script_test_chunks) {
        total += chunk->size();
    }
    return total;
}

'''
    
    # Maintain backward compatibility
    header += '/**\n * Backward compatibility: reference to first chunk\n */\n'
    if chunks:
        header += 'script_test_list const& script_tests_from_json = script_tests_from_json_0;\n\n'
    else:
        header += 'script_test_list const script_tests_from_json{};\n\n'
    
    # Add error summary if there were any parsing errors
    if errors:
        header += '/*\nPARSING ERRORS ENCOUNTERED:\n'
        for error in errors:
            header += f' * {error}\n'
        header += '*/\n'
    
    # Add skip_tests as comments at the end
    if skip_tests:
        header += '\n/*\nSKIPPED TESTS:\n'
        header += 'The following tests were skipped during generation:\n\n'
        for i, skip_test in enumerate(skip_tests, 1):
            script_sig = escape_string(str(skip_test.get('script_sig', '')))
            script_pub_key = escape_string(str(skip_test.get('script_pub_key', '')))
            reason = escape_string(str(skip_test.get('reason', 'No reason provided')))
            key_fork = skip_test.get('key_fork', '')
            
            header += f' * Test {i}:\n'
            header += f' *   script_sig: "{script_sig}"\n'
            header += f' *   script_pub_key: "{script_pub_key}"\n'
            if key_fork:
                header += f' *   key_fork: "{key_fork}"\n'
            header += f' *   reason: {reason}\n'
            header += ' *\n'
        header += '*/\n'
    
    return header


def generate_arrays_only(test_entries: List[str], errors: List[str], skip_tests: List[dict] = None, chunk_size: int = 100) -> str:
    """Generate only the individual array definitions for insertion into existing file."""
    
    arrays_content = ""
    
    # Split test_entries into chunks
    chunks = [test_entries[i:i + chunk_size] for i in range(0, len(test_entries), chunk_size)]
    
    # Generate individual arrays only
    for i, chunk in enumerate(chunks):
        arrays_content += f'/**\n * Script test chunk {i} (tests {i * chunk_size} to {min((i + 1) * chunk_size - 1, len(test_entries) - 1)})\n */\n'
        arrays_content += f'script_test_list const script_tests_from_json_{i}{{\n'
        
        if chunk:
            # Remove comma from the last entry in this chunk
            last_entry = chunk[-1]
            if last_entry.find('},') != -1:
                chunk[-1] = last_entry.replace('},', '}', 1)
            
            arrays_content += '\n'.join(chunk) + '\n'
        
        arrays_content += '};\n\n'
    
    # Add error summary if there were any parsing errors
    if errors:
        arrays_content += '/*\nPARSING ERRORS ENCOUNTERED:\n'
        for error in errors:
            arrays_content += f' * {error}\n'
        arrays_content += '*/\n'
    
    # Add skip_tests as comments at the end
    if skip_tests:
        arrays_content += '\n/*\nSKIPPED TESTS:\n'
        arrays_content += 'The following tests were skipped during generation:\n\n'
        for i, skip_test in enumerate(skip_tests, 1):
            script_sig = escape_string(str(skip_test.get('script_sig', '')))
            script_pub_key = escape_string(str(skip_test.get('script_pub_key', '')))
            reason = escape_string(str(skip_test.get('reason', 'No reason provided')))
            key_fork = skip_test.get('key_fork', '')
            
            arrays_content += f' * Test {i}:\n'
            arrays_content += f' *   script_sig: "{script_sig}"\n'
            arrays_content += f' *   script_pub_key: "{script_pub_key}"\n'
            if key_fork:
                arrays_content += f' *   key_fork: "{key_fork}"\n'
            arrays_content += f' *   reason: {reason}\n'
            arrays_content += ' *\n'
        arrays_content += '*/\n'
    
    return arrays_content


def replace_in_file(target_file: str, new_content: str) -> bool:
    """Replace the auto-generated section in the target file."""
    
    begin_marker = "// BEGIN AUTO-GENERATED SCRIPT TESTS - DO NOT EDIT"
    end_marker = "// END AUTO-GENERATED SCRIPT TESTS"
    
    try:
        # Read the target file
        with open(target_file, 'r') as f:
            content = f.read()
        
        # Find the markers
        begin_pos = content.find(begin_marker)
        end_pos = content.find(end_marker)
        
        if begin_pos == -1:
            print(f"Error: Begin marker '{begin_marker}' not found in {target_file}")
            print("Please add the marker where you want the arrays to be inserted.")
            return False
        
        if end_pos == -1:
            print(f"Error: End marker '{end_marker}' not found in {target_file}")
            print("Please add the marker after where you want the arrays to end.")
            return False
        
        if begin_pos >= end_pos:
            print(f"Error: Begin marker appears after end marker in {target_file}")
            return False
        
        # Find the end of the begin marker line
        begin_line_end = content.find('\n', begin_pos) + 1
        
        # Replace the content between markers
        new_file_content = (
            content[:begin_line_end] + 
            new_content + 
            content[end_pos:]
        )
        
        # Write the updated content
        with open(target_file, 'w') as f:
            f.write(new_file_content)
        
        return True
        
    except Exception as e:
        print(f"Error updating {target_file}: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description='Convert script_tests.json to C++ header')
    parser.add_argument('json_file', help='Path to script_tests.json file')
    parser.add_argument('output_dir', nargs='?', help='Output directory for generated header (required unless using --replace-in-file)')
    parser.add_argument('--knuth-overrides', help='Path to JSON file with Knuth-specific error overrides')
    parser.add_argument('--replace-in-file', help='Replace arrays in existing file instead of creating new file')
    
    args = parser.parse_args()
    
    # Validate arguments
    if not args.replace_in_file and not args.output_dir:
        print("Error: output_dir is required when not using --replace-in-file", file=sys.stderr)
        return 1
    
    # Load Knuth overrides and skip_tests if specified
    knuth_overrides = {}
    skip_tests = []
    if args.knuth_overrides:
        if not os.path.exists(args.knuth_overrides):
            print(f"Error: Overrides file {args.knuth_overrides} does not exist", file=sys.stderr)
            return 1
        
        try:
            with open(args.knuth_overrides, 'r') as f:
                override_data = json.load(f)
                
                # Track key usage for duplicate detection
                key_usage = {}  # base_key -> ("scripts_only" | "with_fork")
                
                # Create a lookup dict using script_sig + script_pub_key + optional fork as key
                for override in override_data.get('overrides', []):
                    script_sig = override['script_sig']
                    script_pub_key = override['script_pub_key']
                    
                    # Check if fork is specified as part of the key
                    key_fork = override.get('key_fork', None)
                    
                    # Create the base key (script_sig + script_pub_key)
                    base_key = (script_sig, script_pub_key)
                    
                    # Create the full key (including fork if specified)
                    if key_fork is not None:
                        full_key = (script_sig, script_pub_key, key_fork)
                        key_type = "with_fork"
                    else:
                        full_key = base_key
                        key_type = "scripts_only"

                    # print(f"base_key: {base_key}, full_key: {full_key}, key_fork: {key_fork}, key_type: {key_type}")
                    
                    # Check for conflicts
                    if base_key in key_usage:
                        existing_type = key_usage[base_key]
                        if existing_type == "scripts_only" and key_type == "scripts_only":
                            print(f"Error: Duplicate key found: scripts ({script_sig}, {script_pub_key}) already exists", file=sys.stderr)
                            return 1
                        elif existing_type == "scripts_only" and key_type == "with_fork":
                            print(f"Error: Conflict: scripts ({script_sig}, {script_pub_key}) already exists without fork specification. Cannot add fork-specific entry.", file=sys.stderr)
                            return 1
                        elif existing_type == "with_fork" and key_type == "scripts_only":
                            print(f"Error: Conflict: scripts ({script_sig}, {script_pub_key}) already has fork-specific entries. Cannot add scripts-only entry.", file=sys.stderr)
                            return 1
                    
                    if full_key in knuth_overrides:
                        if key_fork is not None:
                            print(f"Error: Duplicate key found: scripts ({script_sig}, {script_pub_key}) with fork '{key_fork}' already exists", file=sys.stderr)
                        else:
                            print(f"Error: Duplicate key found: scripts ({script_sig}, {script_pub_key}) already exists", file=sys.stderr)
                        return 1
                    
                    # Support both old format (just error) and new format (full override data)
                    if 'knuth_error' in override and len([k for k in override.keys() if k.startswith('knuth_')]) == 1:
                        # Old format: just error override
                        knuth_overrides[full_key] = override['knuth_error']
                    else:
                        # New format: store the entire override data
                        override_info = {}
                        if 'knuth_error' in override:
                            override_info['knuth_error'] = override['knuth_error']
                        if 'knuth_script_sig' in override:
                            override_info['knuth_script_sig'] = override['knuth_script_sig']
                        if 'knuth_script_pub_key' in override:
                            override_info['knuth_script_pub_key'] = override['knuth_script_pub_key']
                        if 'knuth_fork' in override:
                            override_info['knuth_fork'] = override['knuth_fork']
                        knuth_overrides[full_key] = override_info
                    
                    # Mark this base key as used
                    key_usage[base_key] = key_type
                
                # Load skip_tests if present
                skip_tests = override_data.get('skip_tests', [])
                
                print(f"Loaded {len(knuth_overrides)} override entries from {args.knuth_overrides}")
                if skip_tests:
                    print(f"Loaded {len(skip_tests)} skip_tests entries from {args.knuth_overrides}")
                
        except (json.JSONDecodeError, KeyError) as e:
            print(f"Error: Failed to parse overrides file: {e}", file=sys.stderr)
            return 1
    
    # Validate input file
    if not os.path.exists(args.json_file):
        print(f"Error: Input file {args.json_file} does not exist", file=sys.stderr)
        return 1
    
    # Create output directory and file path only if not using replace-in-file mode
    if not args.replace_in_file:
        os.makedirs(args.output_dir, exist_ok=True)
        output_file = os.path.join(args.output_dir, 'script_tests.hpp')
    
    try:
        # Read and parse JSON
        with open(args.json_file, 'r') as f:
            data = json.load(f)
        
        if not isinstance(data, list):
            print("Error: JSON file must contain an array", file=sys.stderr)
            return 1
        
        # Process all entries
        test_entries = []
        all_errors = []
        manually_skipped_count = 0
        
        for line_num, entry in enumerate(data, 1):
            cpp_line, errors, was_manually_skipped = process_test_entry(entry, line_num, knuth_overrides, skip_tests)
            if cpp_line:
                test_entries.append(cpp_line)
            elif was_manually_skipped:
                manually_skipped_count += 1
            all_errors.extend(errors)
        
        # Choose generation mode based on arguments
        if args.replace_in_file:
            # Generate only arrays and replace in existing file
            arrays_content = generate_arrays_only(test_entries, all_errors, skip_tests)
            
            if replace_in_file(args.replace_in_file, arrays_content):
                print(f"Updated {args.replace_in_file}")
                print(f"Processed {len(test_entries)} test cases")
                
                if manually_skipped_count > 0:
                    print(f"Manually skipped {manually_skipped_count} test cases")
            else:
                return 1
        else:
            # Generate complete header file
            header_content = generate_header(test_entries, all_errors, skip_tests)
            
            # Write output file
            with open(output_file, 'w') as f:
                f.write(header_content)
            
            print(f"Generated {output_file}")
            print(f"Processed {len(test_entries)} test cases")
            
            if manually_skipped_count > 0:
                print(f"Manually skipped {manually_skipped_count} test cases")
        
        if all_errors:
            print(f"Warning: {len(all_errors)} parsing errors encountered")
            for error in all_errors[:10]:  # Show first 10 errors
                print(f"  {error}")
            if len(all_errors) > 10:
                print(f"  ... and {len(all_errors) - 10} more errors")
        
        return 0
        
    except json.JSONDecodeError as e:
        print(f"Error: Failed to parse JSON file: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
