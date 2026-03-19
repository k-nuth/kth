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
# Based on kth::domain::machine::script_flags enum and convert_flags function
FORK_MAP = {
    # BIP rules (1 to 1 mapping)
    "P2SH": "kth::domain::machine::script_flags::bip16_rule",
    "CHECKLOCKTIMEVERIFY": "kth::domain::machine::script_flags::bip65_rule",
    "DERSIG": "kth::domain::machine::script_flags::bip66_rule",
    "CHECKSEQUENCEVERIFY": "kth::domain::machine::script_flags::bip112_rule",

    # Individual BCH feature flags (1 to 1 mapping)
    "STRICTENC": "kth::domain::machine::script_flags::bch_strictenc",
    "SIGHASH_FORKID": "kth::domain::machine::script_flags::bch_sighash_forkid",
    "LOW_S": "kth::domain::machine::script_flags::bch_low_s",
    "NULLFAIL": "kth::domain::machine::script_flags::bch_nullfail",
    "SIGPUSHONLY": "kth::domain::machine::script_flags::bch_sigpushonly",
    "CLEANSTACK": "kth::domain::machine::script_flags::bch_cleanstack",
    "SCHNORR_MULTISIG": "kth::domain::machine::script_flags::bch_schnorr_multisig",
    "MINIMALDATA": "kth::domain::machine::script_flags::bch_minimaldata",
    "INPUT_SIGCHECKS": "kth::domain::machine::script_flags::bch_input_sigchecks",
    "ENFORCE_SIGCHECKS": "kth::domain::machine::script_flags::bch_enforce_sigchecks",
    "64_BIT_INTEGERS": "kth::domain::machine::script_flags::bch_64bit_integers",
    "NATIVE_INTROSPECTION": "kth::domain::machine::script_flags::bch_native_introspection",
    "P2SH_32": "kth::domain::machine::script_flags::bch_p2sh_32",
    "ENABLE_TOKENS": "kth::domain::machine::script_flags::bch_tokens",
    "ENABLE_MAY2025": "kth::domain::machine::script_flags::bch_vm_limits | kth::domain::machine::script_flags::bch_bigint",

    # Knuth-specific fictitious fork for pythagoras operations
    "KTH_PYTHAGORAS": "kth::domain::machine::script_flags::bch_reactivated_opcodes",

    # BTC-only rules (when not KTH_CURRENCY_BCH)
    "WITNESS": "kth::domain::machine::script_flags::bip141_rule",
    "DISCOURAGE_UPGRADABLE_WITNESS_PROGRAM": "kth::domain::machine::script_flags::bip141_rule",
    "WITNESS_PUBKEYTYPE": "kth::domain::machine::script_flags::bip141_rule",
    "CONST_SCRIPTCODE": "kth::domain::machine::script_flags::bip141_rule",

    "NULLDUMMY": "kth::domain::machine::script_flags::bip147_rule",

    # Flags that don't map to specific forks (policy flags)
    "NONE": None,
    "MINIMALIF": "kth::domain::machine::script_flags::bch_minimalif",
    "DISCOURAGE_UPGRADABLE_NOPS": "kth::domain::machine::script_flags::bch_discourage_upgradable_nops",
    "VM_LIMITS_STANDARD": None,  # Policy flag
    "DISALLOW_SEGWIT_RECOVERY": "kth::domain::machine::script_flags::bch_disallow_segwit_recovery",
}

# Fork hierarchy - ordered from lowest to highest value
# Higher forks include the functionality of lower forks

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
    "OP_COUNT": "kth::error::invalid_operation_count",
    "STACK_SIZE": "kth::error::invalid_stack_size",
    "SIG_COUNT": "kth::error::invalid_operand_size",
    "PUBKEY_COUNT": "kth::error::invalid_operand_size",
    "INPUT_SIGCHECKS": "kth::error::invalid_script",
    
    # Failed verify operations
    "VERIFY": "kth::error::verify_failed",
    "EQUALVERIFY": "kth::error::verify_failed",
    "CHECKMULTISIGVERIFY": "kth::error::invalid_script",
    "CHECKSIGVERIFY": "kth::error::invalid_script",
    "CHECKDATASIGVERIFY": "kth::error::invalid_script",
    "NUMEQUALVERIFY": "kth::error::verify_failed",

    # Logical/Format/Canonical errors
    "BAD_OPCODE": "kth::error::op_reserved",
    "DISABLED_OPCODE": "kth::error::op_disabled",
    "INVALID_STACK_OPERATION": "kth::error::insufficient_main_stack",
    "INVALID_ALTSTACK_OPERATION": "kth::error::insufficient_alt_stack",
    "UNBALANCED_CONDITIONAL": "kth::error::unbalanced_conditional",

    # Operand/Number/Bit errors
    "OPERAND_SIZE": "kth::error::operand_size_mismatch",
    "INVALID_NUMBER_RANGE": "kth::error::invalid_operand_size",
    "INVALID_NUMBER_RANGE_64_BIT": "kth::error::invalid_operand_size",
    "INVALID_NUMBER_RANGE_BIG_INT": "kth::error::invalid_operand_size",
    "IMPOSSIBLE_ENCODING": "kth::error::impossible_encoding",
    "SPLIT_RANGE": "kth::error::invalid_split_range",
    "INVALID_BIT_COUNT": "kth::error::invalid_script",
    "DIV_BY_ZERO": "kth::error::division_by_zero",
    "MOD_BY_ZERO": "kth::error::division_by_zero",
    "OVERFLOW": "kth::error::overflow",
    "BITFIELD_SIZE": "kth::error::invalid_script",
    "BIT_RANGE": "kth::error::invalid_script",
    
    # BIP65/BIP112 locktime errors -> descriptive names
    "NEGATIVE_LOCKTIME": "kth::error::negative_locktime",
    "UNSATISFIED_LOCKTIME": "kth::error::unsatisfied_locktime",
    
    "OP_RETURN": "kth::error::op_return",

    # Other errors -> invalid_script
    "UNKNOWN_ERROR": "kth::error::invalid_script",
    
    # BIP66 errors (BCH: invalid_signature_encoding, BTC: operation_failed)
    "SIG_DER": "kth::error::invalid_signature_lax_encoding",  # In BCH, BIP66 is always active so DER failures return lax_encoding
    
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
    "MISSING_FORKID": "kth::error::must_use_forkid",  # BCHN alias: MISSING_FORKID → ScriptError::MUST_USE_FORKID
    
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

    # Knuth-specific overrides (all map to generic categories now)
    "KTH_OP_VERIFY": "kth::error::verify_failed",
    "KTH_OP_VERIFY_EMPTY_STACK": "kth::error::insufficient_main_stack",
    "KTH_OP_VERIFY_FAILED": "kth::error::verify_failed",
    "KTH_OP_CAT": "kth::error::insufficient_main_stack",
    "KTH_OP_SPLIT": "kth::error::insufficient_main_stack",
    "KTH_OP_REVERSE_BYTES": "kth::error::insufficient_main_stack",
    "KTH_OP_EQUAL_VERIFY_INSUFFICIENT_STACK": "kth::error::insufficient_main_stack",
    "KTH_OP_EQUAL_VERIFY_FAILED": "kth::error::verify_failed",
    "KTH_OP_NUM_EQUAL": "kth::error::insufficient_main_stack",
    "KTH_OP_NUM_EQUAL_VERIFY_INSUFFICIENT_STACK": "kth::error::insufficient_main_stack",
    "KTH_OP_NUM_EQUAL_VERIFY_FAILED": "kth::error::verify_failed",
    "KTH_OP_CHECK_SIG_VERIFY_FAILED": "kth::error::verify_failed",
    "KTH_OP_IF": "kth::error::unbalanced_conditional",
    "KTH_OP_NOTIF": "kth::error::unbalanced_conditional",
    "KTH_OP_ENDIF": "kth::error::unbalanced_conditional",
    "KTH_OP_ELSE": "kth::error::unbalanced_conditional",
    "KTH_OP_DIV": "kth::error::insufficient_main_stack",
    "KTH_OP_DIV_BY_ZERO": "kth::error::division_by_zero",
    "KTH_OP_MOD": "kth::error::insufficient_main_stack",
    "KTH_OP_MOD_BY_ZERO": "kth::error::division_by_zero",
    "KTH_INVALID_SCRIPT": "kth::error::invalid_script",

    # Knuth-specific stack operation errors → all map to insufficient_main_stack
    "KTH_OP_IF_DUP": "kth::error::insufficient_main_stack",
    "KTH_OP_DROP": "kth::error::insufficient_main_stack",
    "KTH_OP_DUP": "kth::error::insufficient_main_stack",
    "KTH_OP_NIP": "kth::error::insufficient_main_stack",
    "KTH_OP_OVER": "kth::error::insufficient_main_stack",
    "KTH_OP_PICK": "kth::error::insufficient_main_stack",
    "KTH_OP_ROLL": "kth::error::insufficient_main_stack",
    "KTH_OP_ROT": "kth::error::insufficient_main_stack",
    "KTH_OP_SWAP": "kth::error::insufficient_main_stack",
    "KTH_OP_TUCK": "kth::error::insufficient_main_stack",
    "KTH_OP_DUP2": "kth::error::insufficient_main_stack",
    "KTH_OP_DUP3": "kth::error::insufficient_main_stack",
    "KTH_OP_OVER2": "kth::error::insufficient_main_stack",
    "KTH_OP_ROT2": "kth::error::insufficient_main_stack",
    "KTH_OP_SWAP2": "kth::error::insufficient_main_stack",
    "KTH_OP_TO_ALT_STACK": "kth::error::insufficient_main_stack",
    "KTH_OP_DROP2": "kth::error::insufficient_main_stack",

    # Knuth-specific num2bin/bin2num operation errors → generic categories
    "KTH_OP_NUM2BIN": "kth::error::insufficient_main_stack",
    "KTH_OP_NUM2BIN_INVALID_SIZE": "kth::error::invalid_operand_size",
    "KTH_OP_NUM2BIN_SIZE_EXCEEDED": "kth::error::invalid_operand_size",
    "KTH_OP_NUM2BIN_IMPOSSIBLE_ENCODING": "kth::error::impossible_encoding",
    "KTH_OP_BIN2NUM": "kth::error::insufficient_main_stack",
    "KTH_OP_BIN2NUM_INVALID_NUMBER_RANGE": "kth::error::invalid_number_encoding",

    # Knuth-specific bitwise/size operation errors → generic categories
    "KTH_OP_SIZE": "kth::error::insufficient_main_stack",
    "KTH_OP_AND": "kth::error::insufficient_main_stack",
    "KTH_OP_OR": "kth::error::insufficient_main_stack",
    "KTH_OP_XOR": "kth::error::insufficient_main_stack",

    # Knuth-specific signature operation errors → generic categories
    "KTH_OP_EQUAL": "kth::error::insufficient_main_stack",

    # Knuth-specific arithmetic operation errors → generic categories
    "KTH_OP_ADD": "kth::error::insufficient_main_stack",
    "KTH_OP_ADD_OVERFLOW": "kth::error::overflow",
    "KTH_OP_NOT": "kth::error::insufficient_main_stack",
    "KTH_OP_MUL": "kth::error::insufficient_main_stack",
    "KTH_OP_MUL_OVERFLOW": "kth::error::overflow",

    # Knuth-specific generic errors
    "KTH_INSUFFICIENT_MAIN_STACK": "kth::error::insufficient_main_stack",
    "KTH_INVALID_OPERAND_SIZE": "kth::error::invalid_operand_size",
    "KTH_OPERAND_SIZE_MISMATCH": "kth::error::operand_size_mismatch",
    "KTH_OVERFLOW": "kth::error::overflow",
    "KTH_INVALID_STACK_SCOPE": "kth::error::invalid_stack_scope",
    "KTH_UNBALANCED_CONDITIONAL": "kth::error::unbalanced_conditional",
    "KTH_INVALID_NUMBER_ENCODING": "kth::error::invalid_number_encoding",

    # Knuth-specific hash operation errors → insufficient_main_stack
    "KTH_OP_RIPEMD160": "kth::error::insufficient_main_stack",
    "KTH_OP_SHA1": "kth::error::insufficient_main_stack",
    "KTH_OP_SHA256": "kth::error::insufficient_main_stack",
    "KTH_OP_HASH160": "kth::error::insufficient_main_stack",
    "KTH_OP_HASH256": "kth::error::insufficient_main_stack",

    # Knuth-specific introspection operation errors → op_reserved (not implemented yet)
    "KTH_OP_INPUT_INDEX": "kth::error::op_reserved",
    "KTH_OP_ACTIVE_BYTECODE": "kth::error::op_reserved",
    "KTH_OP_TX_VERSION": "kth::error::op_reserved",
    "KTH_OP_TX_INPUT_COUNT": "kth::error::op_reserved",
    "KTH_OP_TX_OUTPUT_COUNT": "kth::error::op_reserved",
    "KTH_OP_TX_LOCKTIME": "kth::error::op_reserved",
    "KTH_OP_UTXO_VALUE": "kth::error::op_reserved",
    "KTH_OP_UTXO_BYTECODE": "kth::error::op_reserved",
    "KTH_OP_OUTPOINT_TX_HASH": "kth::error::op_reserved",
    "KTH_OP_OUTPOINT_INDEX": "kth::error::op_reserved",
    "KTH_OP_INPUT_BYTECODE": "kth::error::op_reserved",
    "KTH_OP_INPUT_SEQUENCE_NUMBER": "kth::error::op_reserved",
    "KTH_OP_OUTPUT_VALUE": "kth::error::op_reserved",
    "KTH_OP_OUTPUT_BYTECODE": "kth::error::op_reserved",
}


def parse_flags(flags_str: str) -> Tuple[str, List[str]]:
    """
    Parse flag string and return C++ fork expression and list of errors.

    Each BCHN flag maps to an individual feature bit. Multiple flags are ORed together.

    Args:
        flags_str: Comma-separated list of flags

    Returns:
        Tuple of (cpp_expression, error_list)
    """
    if not flags_str or flags_str == "NONE":
        return "kth::domain::machine::script_flags::no_rules", []

    flags = [flag.strip() for flag in flags_str.split(",")]
    active_flags = set()  # Use set to avoid duplicates
    errors = []

    for flag in flags:
        if flag in FORK_MAP:
            fork = FORK_MAP[flag]
            if fork is not None:  # Some flags don't map to forks (policy flags)
                active_flags.add(fork)
        else:
            errors.append(f"Unknown flag: {flag}")

    if not active_flags:
        return "kth::domain::machine::script_flags::no_rules", errors
    elif len(active_flags) == 1:
        return list(active_flags)[0], errors
    else:
        # OR all active feature bits together
        return " | ".join(sorted(active_flags)), errors


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


# Mapping from byte value to Knuth opcode name for raw opcode expansion.
# Only non-push opcodes (0x4f and above) are mapped here.
# Push opcodes (0x00-0x4e) require special handling with data.
BYTE_TO_OPCODE = {
    0x4f: "-1",
    0x50: "reserved_80",
    0x51: "1", 0x52: "2", 0x53: "3", 0x54: "4", 0x55: "5",
    0x56: "6", 0x57: "7", 0x58: "8", 0x59: "9", 0x5a: "10",
    0x5b: "11", 0x5c: "12", 0x5d: "13", 0x5e: "14", 0x5f: "15",
    0x60: "16",
    0x61: "nop", 0x62: "reserved_98",
    0x63: "if", 0x64: "notif",
    0x65: "disabled_verif", 0x66: "disabled_vernotif",
    0x67: "else", 0x68: "endif",
    0x69: "verify", 0x6a: "return",
    0x6b: "toaltstack", 0x6c: "fromaltstack",
    0x6d: "drop2", 0x6e: "dup2", 0x6f: "dup3",
    0x70: "over2", 0x71: "rot2", 0x72: "swap2",
    0x73: "ifdup", 0x74: "depth", 0x75: "drop",
    0x76: "dup", 0x77: "nip", 0x78: "over",
    0x79: "pick", 0x7a: "roll", 0x7b: "rot",
    0x7c: "swap", 0x7d: "tuck",
    0x7e: "cat", 0x7f: "split",
    0x80: "num2bin", 0x81: "bin2num", 0x82: "size",
    0x83: "invert", 0x84: "and", 0x85: "or", 0x86: "xor",
    0x87: "equal", 0x88: "equalverify",
    0x89: "reserved_137", 0x8a: "reserved_138",
    0x8b: "add1", 0x8c: "sub1",
    0x8d: "mul2", 0x8e: "div2",
    0x8f: "negate", 0x90: "abs", 0x91: "not", 0x92: "nonzero",
    0x93: "add", 0x94: "sub", 0x95: "mul", 0x96: "div", 0x97: "mod",
    0x98: "lshift", 0x99: "rshift",
    0x9a: "booland", 0x9b: "boolor",
    0x9c: "numequal", 0x9d: "numequalverify", 0x9e: "numnotequal",
    0x9f: "lessthan", 0xa0: "greaterthan",
    0xa1: "lessthanorequal", 0xa2: "greaterthanorequal",
    0xa3: "min", 0xa4: "max", 0xa5: "within",
    0xa6: "ripemd160", 0xa7: "sha1", 0xa8: "sha256",
    0xa9: "hash160", 0xaa: "hash256",
    0xab: "codeseparator", 0xac: "checksig", 0xad: "checksigverify",
    0xae: "checkmultisig", 0xaf: "checkmultisigverify",
    0xb0: "nop1", 0xb1: "checklocktimeverify", 0xb2: "checksequenceverify",
    0xb3: "nop4", 0xb4: "nop5", 0xb5: "nop6",
    0xb6: "nop7", 0xb7: "nop8", 0xb8: "nop9", 0xb9: "nop10",
    0xba: "checkdatasig", 0xbb: "checkdatasigverify",
    0xbc: "reversebytes",
}


def expand_raw_hex_opcodes(token: str) -> str:
    """
    Expand a standalone 0xHEXHEX... token into individual Knuth opcodes.

    In BCHN, '0x616161' means 3 consecutive OP_NOP opcodes (raw script bytes).
    In Knuth, we need to expand this to 'nop nop nop'.

    For push opcodes (0x00-0x4b), this also handles the data that follows.
    For 0x4c/0x4d/0x4e (PUSHDATA1/2/4), the data size bytes and data follow.

    Args:
        token: A hex string like '0x616161' or '6161' (without 0x prefix)

    Returns:
        Space-separated Knuth opcodes, or the original token if not expandable
    """
    hex_str = token
    if hex_str.startswith('0x') or hex_str.startswith('0X'):
        hex_str = hex_str[2:]

    if len(hex_str) % 2 != 0:
        return token  # odd length, not valid raw bytes

    try:
        raw_bytes = bytes.fromhex(hex_str)
    except ValueError:
        return token  # not valid hex

    opcodes = []
    i = 0
    while i < len(raw_bytes):
        byte_val = raw_bytes[i]

        if byte_val == 0x00:
            # OP_0 / push_size_0 - pushes empty array
            opcodes.append("0")
            i += 1
        elif 0x01 <= byte_val <= 0x4b:
            # Direct push: next byte_val bytes are data
            data_len = byte_val
            if i + 1 + data_len > len(raw_bytes):
                return token  # not enough data, bail out
            data = raw_bytes[i+1:i+1+data_len]
            opcodes.append(f"[{data.hex()}]")
            i += 1 + data_len
        elif byte_val == 0x4c:
            # PUSHDATA1: 1 byte size + data
            if i + 2 > len(raw_bytes):
                return token
            data_len = raw_bytes[i+1]
            if i + 2 + data_len > len(raw_bytes):
                return token
            data = raw_bytes[i+2:i+2+data_len]
            opcodes.append(f"[1.{data.hex()}]")
            i += 2 + data_len
        elif byte_val == 0x4d:
            # PUSHDATA2: 2 byte size (little-endian) + data
            if i + 3 > len(raw_bytes):
                return token
            data_len = raw_bytes[i+1] | (raw_bytes[i+2] << 8)
            if i + 3 + data_len > len(raw_bytes):
                return token
            data = raw_bytes[i+3:i+3+data_len]
            opcodes.append(f"[2.{data.hex()}]")
            i += 3 + data_len
        elif byte_val == 0x4e:
            # PUSHDATA4: 4 byte size (little-endian) + data
            if i + 5 > len(raw_bytes):
                return token
            data_len = (raw_bytes[i+1] | (raw_bytes[i+2] << 8) |
                       (raw_bytes[i+3] << 16) | (raw_bytes[i+4] << 24))
            if i + 5 + data_len > len(raw_bytes):
                return token
            data = raw_bytes[i+5:i+5+data_len]
            opcodes.append(f"[4.{data.hex()}]")
            i += 5 + data_len
        elif byte_val in BYTE_TO_OPCODE:
            opcodes.append(BYTE_TO_OPCODE[byte_val])
            i += 1
        else:
            # Unknown opcode byte, use reserved_N format
            opcodes.append(f"reserved_{byte_val}")
            i += 1

    return ' '.join(opcodes)


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
    # Only normalize when the declared size matches the actual data length;
    # otherwise preserve the original token sequence (malformed encoding).
    def le_hex_to_int(hex_str):
        """Convert a little-endian hex string to an integer."""
        return int.from_bytes(bytes.fromhex(hex_str.zfill(len(hex_str) + len(hex_str) % 2)), 'little')

    # PUSHDATA1: 1 byte size specification (1 byte = no endian ambiguity)
    pushdata1_pattern = r'PUSHDATA1\s+0x([0-9a-fA-F]{1,2})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata1(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if int(size_hex, 16) != len(data_hex) // 2:
            return match.group(0)
        return f"[1.{data_hex}]"
    script = re.sub(pushdata1_pattern, replace_pushdata1, script, flags=re.IGNORECASE)

    # PUSHDATA2: 2 bytes size specification (little-endian)
    pushdata2_pattern = r'PUSHDATA2\s+0x([0-9a-fA-F]{1,4})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata2(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if le_hex_to_int(size_hex) != len(data_hex) // 2:
            return match.group(0)
        return f"[2.{data_hex}]"
    script = re.sub(pushdata2_pattern, replace_pushdata2, script, flags=re.IGNORECASE)

    # PUSHDATA4: 4 bytes size specification (little-endian)
    pushdata4_pattern = r'PUSHDATA4\s+0x([0-9a-fA-F]{1,8})\s+0x([0-9a-fA-F]+)'
    def replace_pushdata4(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if le_hex_to_int(size_hex) != len(data_hex) // 2:
            return match.group(0)
        return f"[4.{data_hex}]"
    script = re.sub(pushdata4_pattern, replace_pushdata4, script, flags=re.IGNORECASE)

    # Handle inline hex PUSHDATA opcodes: 0x4cNN, 0x4dNNNN, 0x4eNNNNNNNN followed by 0xDATA
    # Only normalize when the declared size matches the actual data length.
    # PUSHDATA1 as 0x4cNN 0xDATA (1 byte size, no endian ambiguity)
    inline_pd1 = r'0x4c([0-9a-fA-F]{2})\s+0x([0-9a-fA-F]+)'
    def replace_inline_pd1(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if int(size_hex, 16) != len(data_hex) // 2:
            return match.group(0)
        return f"[1.{data_hex}]"
    script = re.sub(inline_pd1, replace_inline_pd1, script)

    # PUSHDATA2 as 0x4dNNNN 0xDATA (2 byte size, little-endian)
    inline_pd2 = r'0x4d([0-9a-fA-F]{4})\s+0x([0-9a-fA-F]+)'
    def replace_inline_pd2(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if le_hex_to_int(size_hex) != len(data_hex) // 2:
            return match.group(0)
        return f"[2.{data_hex}]"
    script = re.sub(inline_pd2, replace_inline_pd2, script)

    # PUSHDATA4 as 0x4eNNNNNNNN 0xDATA (4 byte size, little-endian)
    inline_pd4 = r'0x4e([0-9a-fA-F]{8})\s+0x([0-9a-fA-F]+)'
    def replace_inline_pd4(match):
        size_hex = match.group(1)
        data_hex = match.group(2)
        if le_hex_to_int(size_hex) != len(data_hex) // 2:
            return match.group(0)
        return f"[4.{data_hex}]"
    script = re.sub(inline_pd4, replace_inline_pd4, script)
    
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

    # Expand any remaining standalone 0x... tokens as raw opcodes.
    # These are hex strings that weren't matched by the length+data patterns above.
    # In BCHN, '0x616161' means raw script bytes (each byte is an opcode).
    tokens = final_transformed.split()
    expanded_tokens = []
    for token in tokens:
        if token.startswith('0x') and len(token) > 4 and all(c in '0123456789abcdefABCDEF' for c in token[2:]):
            hex_part = token[2:]
            if len(hex_part) % 2 == 0:
                expanded = expand_raw_hex_opcodes(token)
                if expanded != token:
                    expanded_tokens.append(expanded)
                    continue
        expanded_tokens.append(token)

    return ' '.join(expanded_tokens)


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
        # Detect amount format: [[nValue], scriptSig, scriptPubKey, flags, error, comment]
        # Same detection as BCHN: if first element is a list, it contains the amount.
        pos = 0
        amount_satoshis = 0
        if isinstance(entry[0], list):
            # entry[0] is [nValue] where nValue is in BTC (float); convert to satoshis
            amount_btc = entry[0][0]
            amount_satoshis = int(round(amount_btc * 100_000_000))
            pos = 1

        script_sig = entry[pos]
        script_pub_key = entry[pos + 1]

        # Check if this test should be skipped (processed below after generating the line)
        should_skip, skip_reason = should_skip_test(script_sig, script_pub_key, skip_tests)

        # Handle different formats:
        # Format 1: [script_sig, script_pub_key, flags, error, comment]
        # Format 2: [script_sig, script_pub_key, "", error, comment]  (no flags)

        remaining = entry[pos:]
        if len(remaining) >= 5:
            flags_str = remaining[2]
            expected_error = remaining[3]
            comment = remaining[4]
        elif len(remaining) == 4:
            flags_str = remaining[2]
            expected_error = remaining[3]
            comment = ""
        else:
            return None, [f"Line {line_number}: Unexpected entry format"], False
        
        # If the flags field is empty or looks like a script, treat as no flags
        if not flags_str or flags_str == "" or ("0x" in flags_str and "CHECKSIG" in flags_str):
            flags_str = "NONE"
        
        # Parse flags
        flags_cpp, flag_errors = parse_flags(flags_str)
        
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
        
        # Remember original expected_error before overrides (for semantic check)
        original_expected_error = expected_error

        # Override annotation tracking
        ovr_idx = None
        ovr_summary = None

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
                ovr_idx = override_data.get('_ovr_idx', -1)
                ovr_summary = override_data.get('_ovr_summary', '')

                # Handle both old format (wrapped string) and new format (dict)
                if '_ovr_value' in override_data:
                    # Old format: just error override
                    override_error = override_data['_ovr_value']
                    override_comment = f" [KNUTH_OVERRIDE: {override_error}]"
                    # Parse the override error
                    override_error_cpp, override_error_errors = parse_error(override_error)

                    # Semantic safety check: detect OK<->FAIL changes
                    orig_is_ok = (original_expected_error == "OK")
                    new_is_ok = (override_error == "OK")
                    if orig_is_ok != new_is_ok:
                        direction = "OK->FAIL" if orig_is_ok else "FAIL->OK"
                        print(f"ERROR: Override changes test semantics ({direction})! "
                              f"sig=\"{script_sig}\", pub=\"{script_pub_key}\", "
                              f"original={original_expected_error}, override={override_error}",
                              file=sys.stderr)
                        sys.exit(1)

                    error_cpp = override_error_cpp
                    expected_error = override_error
                    all_errors.extend(override_error_errors)
                    comment += override_comment
                else:
                    # New format: dict with error and/or script overrides
                    override_comment_parts = []

                    # Override error if specified
                    if 'knuth_error' in override_data:
                        override_error = override_data['knuth_error']
                        override_error_cpp, override_error_errors = parse_error(override_error)

                        # Semantic safety check: detect OK<->FAIL changes
                        orig_is_ok = (original_expected_error == "OK")
                        new_is_ok = (override_error == "OK")
                        if orig_is_ok != new_is_ok:
                            direction = "OK->FAIL" if orig_is_ok else "FAIL->OK"
                            print(f"ERROR: Override changes test semantics ({direction})! "
                                  f"sig=\"{script_sig}\", pub=\"{script_pub_key}\", "
                                  f"original={original_expected_error}, override={override_error}",
                                  file=sys.stderr)
                            sys.exit(1)

                        error_cpp = override_error_cpp
                        expected_error = override_error
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
                        override_flags_cpp, override_fork_errors = parse_flags(override_fork)
                        flags_cpp = override_flags_cpp
                        all_errors.extend(override_fork_errors)
                        override_comment_parts.append(f"FORK: {override_fork}")

                    # Add override comment
                    if override_comment_parts:
                        comment += f" [KNUTH_OVERRIDE: {', '.join(override_comment_parts)}]"
        
        escaped_sig = escape_string(smart_lowered_sig)
        escaped_pubkey = escape_string(smart_lowered_pubkey)
        escaped_comment = escape_string(comment)

        test_info = {
            'script_sig': escaped_sig,
            'script_pub_key': escaped_pubkey,
            'flags_cpp': flags_cpp,
            'error_cpp': error_cpp,
            'comment': escaped_comment,
            'flags_str': flags_str,
            'expected_error': expected_error,
            'is_skipped': should_skip,
            'skip_reason': skip_reason,
            'errors': all_errors,
            'amount_satoshis': amount_satoshis,
            'override_idx': ovr_idx,
            'override_summary': ovr_summary,
        }

        return test_info, all_errors, should_skip

    except (IndexError, TypeError) as e:
        return None, [f"Line {line_number}: Failed to parse entry: {e}"], False


def format_array_entry(test_info: dict) -> str:
    """Format test info dict as an array entry line for C++ vector initialization."""
    d = test_info
    amount = d.get('amount_satoshis', 0)
    if amount:
        cpp_line = f'    {{"{d["script_sig"]}", "{d["script_pub_key"]}", {d["flags_cpp"]}, {d["error_cpp"]}, "{d["comment"]}", {amount}ULL}}'
    else:
        cpp_line = f'    {{"{d["script_sig"]}", "{d["script_pub_key"]}", {d["flags_cpp"]}, {d["error_cpp"]}, "{d["comment"]}"}}'
    cpp_line += ","
    cpp_line += f' // flags: {d["flags_str"]}, expected: {d["expected_error"]}'
    if d['errors']:
        cpp_line += " | ERRORS: " + "; ".join(d['errors'])
    if d['is_skipped']:
        cpp_line = f"    // {cpp_line.strip()}  [SKIPPED: {d['skip_reason']}]"
    return cpp_line


def format_test_case(test_info: dict, index: int) -> str:
    """Format test info dict as an individual Catch2 TEST_CASE."""
    d = test_info

    # Build test name from comment or scripts (strip override annotations from name)
    import re as _re
    comment_clean = _re.sub(r'\s*\[KNUTH_OVERRIDE:[^\]]*\]', '', d.get('comment', ''))
    if comment_clean.strip():
        name_suffix = comment_clean.strip()
    else:
        name_suffix = f"{d['script_sig']} | {d['script_pub_key']}"

    # Truncate long names for readability
    max_name_len = 150
    if len(name_suffix) > max_name_len:
        name_suffix = name_suffix[:max_name_len - 3] + "..."

    # Show expected result in test name
    if d['expected_error'] == 'OK':
        result_tag = "Should Pass"
    else:
        result_tag = f"Should Fail: {d['expected_error']}"

    # Escape quotes for C++ string literal
    safe_name = name_suffix.replace('\\', '\\\\').replace('"', '\\"')
    test_name = f"VM-AUTO #{index} [{result_tag}]: {safe_name}"

    # Build the test body
    amount = d.get('amount_satoshis', 0)
    if amount:
        body = (f'run_bchn_test({{"{d["script_sig"]}", "{d["script_pub_key"]}", '
                f'{d["flags_cpp"]}, {d["error_cpp"]}, "{d["comment"]}", {amount}ULL}});')
    else:
        body = (f'run_bchn_test({{"{d["script_sig"]}", "{d["script_pub_key"]}", '
                f'{d["flags_cpp"]}, {d["error_cpp"]}, "{d["comment"]}"}});')

    flags_comment = f'// flags: {d["flags_str"]}, expected: {d["expected_error"]}'
    if d['errors']:
        flags_comment += " | ERRORS: " + "; ".join(d['errors'])

    # Collect annotation lines to put above the test
    above_lines = []

    # Add override annotation if this test was overridden
    if d.get('override_idx') is not None:
        above_lines.append(f'// KNUTH_OVERRIDE #{d["override_idx"]}: {d["override_summary"]}')

    if d['is_skipped']:
        above_lines.append(f'// SKIPPED: {d["skip_reason"]}')
        test_line = f'// TEST_CASE("{test_name}", "[vm][auto]") {{ {body} }}'
    else:
        test_line = f'TEST_CASE("{test_name}", "[vm][auto]") {{ {body} }} {flags_comment}'

    if above_lines:
        return '\n'.join(above_lines) + '\n' + test_line
    else:
        return test_line


def generate_test_cases_only(test_entries: List[dict], errors: List[str], skip_tests: List[dict] = None) -> str:
    """Generate individual Catch2 TEST_CASEs for insertion into existing file."""

    lines = []
    for i, test_info in enumerate(test_entries):
        lines.append(format_test_case(test_info, i))

    content = '\n\n'.join(lines) + '\n'

    # Add error summary if there were any parsing errors
    if errors:
        content += '\n/*\nPARSING ERRORS ENCOUNTERED:\n'
        for error in errors:
            content += f' * {error}\n'
        content += '*/\n'

    # Add skip_tests as comments
    if skip_tests:
        content += '\n/*\nSKIPPED TESTS:\n'
        for i, skip_test in enumerate(skip_tests, 1):
            script_sig = escape_string(str(skip_test.get('script_sig', '')))
            script_pub_key = escape_string(str(skip_test.get('script_pub_key', '')))
            reason = escape_string(str(skip_test.get('reason', 'No reason provided')))
            content += f' * {i}. sig="{script_sig}" pub="{script_pub_key}" reason={reason}\n'
        content += '*/\n'

    return content


def generate_test_cases_split(test_entries: List[dict], errors: List[str], skip_tests: List[dict] = None,
                               output_dir: str = ".", tests_per_file: int = 200,
                               header_include: str = "script.hpp") -> List[str]:
    """Generate individual Catch2 TEST_CASEs split across multiple .cpp files.

    Args:
        test_entries: List of test info dicts
        errors: List of parsing errors
        skip_tests: List of skip test configurations
        output_dir: Directory to write generated .cpp files
        tests_per_file: Number of tests per file
        header_include: Path to include for the test header

    Returns:
        List of generated file paths (relative to output_dir)
    """
    os.makedirs(output_dir, exist_ok=True)

    # Clean stale script_vm_auto_*.cpp files from previous runs
    import glob as glob_mod
    for old_file in glob_mod.glob(os.path.join(output_dir, "script_vm_auto_*.cpp")):
        os.remove(old_file)

    # TODO(fernando): TEMPORARY — limit to first 1600 tests while chunks 16+ are under development.
    #                  Remove this cap once all test categories are passing.
    # MAX_TESTS = 1700
    MAX_TESTS = 1000000
    if len(test_entries) > MAX_TESTS:
        print(f"WARNING: Truncating test generation to first {MAX_TESTS} tests (out of {len(test_entries)}). "
              f"Remove MAX_TESTS cap in generate_test_cases_split() when ready.", file=sys.stderr)
        test_entries = test_entries[:MAX_TESTS]

    # Split entries into chunks
    chunks = [test_entries[i:i + tests_per_file] for i in range(0, len(test_entries), tests_per_file)]

    generated_files = []
    for chunk_idx, chunk in enumerate(chunks):
        filename = f"script_vm_auto_{chunk_idx}.cpp"
        filepath = os.path.join(output_dir, filename)

        first_idx = chunk_idx * tests_per_file

        file_header = f'''// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Auto-generated VM script tests - DO NOT EDIT
// Generated from script_tests.json - chunk {chunk_idx} (tests {first_idx} to {first_idx + len(chunk) - 1})

#include <test_helpers.hpp>
#include "{header_include}"

using namespace kth;
using namespace kd;
using namespace kth::domain::chain;
using namespace kth::domain::machine;

extern void run_bchn_test(bchn_script_test const& test);

'''
        lines = []
        for i, test_info in enumerate(chunk):
            global_idx = first_idx + i
            lines.append(format_test_case(test_info, global_idx))

        content = file_header + '\n\n'.join(lines) + '\n'

        with open(filepath, 'w') as f:
            f.write(content)

        generated_files.append(filename)

    # Write a summary file with errors and skip info
    summary_path = os.path.join(output_dir, "script_vm_auto_summary.txt")
    with open(summary_path, 'w') as f:
        f.write(f"Generated {len(generated_files)} files with {len(test_entries)} tests ({tests_per_file} per file)\n")
        if errors:
            f.write(f"\nPARSING ERRORS ({len(errors)}):\n")
            for error in errors:
                f.write(f"  {error}\n")
        if skip_tests:
            f.write(f"\nSKIPPED TESTS ({len(skip_tests)}):\n")
            for i, skip_test in enumerate(skip_tests, 1):
                f.write(f"  {i}. sig=\"{skip_test.get('script_sig', '')}\" pub=\"{skip_test.get('script_pub_key', '')}\" reason={skip_test.get('reason', '')}\n")

    return generated_files


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

namespace kth::domain {
    using script_flags_t = uint64_t;
}

namespace kth::domain::machine {
    enum script_flags : kth::domain::script_flags_t;
}

/**
 * Structure representing a single script test case.
 */
struct script_test {
    std::string script_sig;         ///< The scriptSig (input script)
    std::string script_pub_key;     ///< The scriptPubKey (output script)  
    kth::domain::script_flags_t flags;     ///< Active flag rules (OR of kth::domain::machine::script_flags values)
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
        header += f'bchn_script_test_list const script_tests_from_json_{i}{{\n'
        
        if chunk:
            # Remove comma from the last entry in this chunk
            last_entry = chunk[-1]
            if last_entry.find('},') != -1:
                chunk[-1] = last_entry.replace('},', '}', 1)
            
            header += '\n'.join(chunk) + '\n'
        
        header += '};\n\n'
    
    # Generate array of arrays
    header += '/**\n * Array of all test chunks for easy iteration\n */\n'
    header += 'std::vector<bchn_script_test_list const*> const all_script_test_chunks{\n'
    
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
        std::println("Testing chunk {} ({} tests)...", chunk_idx,
                  all_script_test_chunks[chunk_idx]->size());
        
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
        header += 'bchn_script_test_list const& script_tests_from_json = script_tests_from_json_0;\n\n'
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
        arrays_content += f'bchn_script_test_list const script_tests_from_json_{i}{{\n'
        
        if chunk:
            # Remove comma from the last entry in this chunk
            last_entry = chunk[-1]
            if last_entry.find('},') != -1:
                chunk[-1] = last_entry.replace('},', '}', 1)
            
            arrays_content += '\n'.join(chunk) + '\n'
        
        arrays_content += '};\n\n'
    
    # Generate array of arrays (all chunks enabled by default)
    arrays_content += '/**\n * Array of all test chunks for easy iteration\n */\n'
    arrays_content += 'std::vector<bchn_script_test_list const*> const all_script_test_chunks{\n'

    for i in range(len(chunks)):
        comma = ',' if i < len(chunks) - 1 else ''
        arrays_content += f'    &script_tests_from_json_{i}{comma}\n'

    arrays_content += '};\n\n'

    # Generate helper function for testing
    arrays_content += '''/**
 * Run tests on a specific chunk with progress output
 */
template<typename TestFunc>
void run_script_tests_chunked(TestFunc test_func) {
    for (size_t chunk_idx = 0; chunk_idx < all_script_test_chunks.size(); ++chunk_idx) {
        std::println("Testing chunk {} ({} tests)...", chunk_idx,
                  all_script_test_chunks[chunk_idx]->size());

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

    # Backward compatibility
    arrays_content += '/**\n * Backward compatibility: reference to first chunk\n */\n'
    if chunks:
        arrays_content += 'bchn_script_test_list const& script_tests_from_json = script_tests_from_json_0;\n\n'
    else:
        arrays_content += 'bchn_script_test_list const script_tests_from_json{};\n\n'

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
    parser.add_argument('--format', choices=['arrays', 'test_cases', 'test_cases_split'], default='arrays',
                        help='Output format: arrays (chunked vectors, default), test_cases (single file), or test_cases_split (multiple .cpp files)')
    parser.add_argument('--tests-per-file', type=int, default=100,
                        help='Number of tests per file in test_cases_split mode (default: 100)')
    parser.add_argument('--header-include', default='script.hpp',
                        help='Header include path for generated .cpp files (default: script.hpp)')

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
                for ovr_idx, override in enumerate(override_data.get('overrides', [])):
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
                    
                    # Build a compact single-line summary of the override for annotations
                    ovr_summary_parts = []
                    if 'knuth_error' in override:
                        ovr_summary_parts.append(f"error={override['knuth_error']}")
                    if 'knuth_script_sig' in override:
                        ovr_summary_parts.append(f"sig={override['knuth_script_sig']}")
                    if 'knuth_script_pub_key' in override:
                        ovr_summary_parts.append(f"pub={override['knuth_script_pub_key']}")
                    if 'knuth_fork' in override:
                        ovr_summary_parts.append(f"fork={override['knuth_fork']}")
                    if 'reason' in override:
                        ovr_summary_parts.append(f"reason={override['reason']}")
                    ovr_summary = ", ".join(ovr_summary_parts) if ovr_summary_parts else "no fields"

                    # Support both old format (just error) and new format (full override data)
                    if 'knuth_error' in override and len([k for k in override.keys() if k.startswith('knuth_')]) == 1:
                        # Old format: just error override
                        knuth_overrides[full_key] = {'_ovr_value': override['knuth_error'], '_ovr_idx': ovr_idx, '_ovr_summary': ovr_summary}
                    else:
                        # New format: store the entire override data
                        override_info = {'_ovr_idx': ovr_idx, '_ovr_summary': ovr_summary}
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
            test_info, errors, was_manually_skipped = process_test_entry(entry, line_num, knuth_overrides, skip_tests)
            if test_info:
                test_entries.append(test_info)
            if was_manually_skipped:
                manually_skipped_count += 1
            all_errors.extend(errors)

        # Choose generation mode based on arguments
        if args.format == 'test_cases_split':
            if not args.output_dir:
                print("Error: output_dir is required for test_cases_split format", file=sys.stderr)
                return 1
            generated_files = generate_test_cases_split(
                test_entries, all_errors, skip_tests,
                output_dir=args.output_dir,
                tests_per_file=args.tests_per_file,
                header_include=args.header_include)
            print(f"Generated {len(generated_files)} files in {args.output_dir}:")
            for f in generated_files:
                print(f"  {f}")
            print(f"Processed {len(test_entries)} test cases ({args.tests_per_file} per file)")
            if manually_skipped_count > 0:
                print(f"Manually skipped {manually_skipped_count} test cases")
        elif args.replace_in_file:
            if args.format == 'test_cases':
                generated_content = generate_test_cases_only(test_entries, all_errors, skip_tests)
            else:
                array_lines = [format_array_entry(info) for info in test_entries]
                generated_content = generate_arrays_only(array_lines, all_errors, skip_tests)

            if replace_in_file(args.replace_in_file, generated_content):
                print(f"Updated {args.replace_in_file}")
                print(f"Processed {len(test_entries)} test cases ({args.format} format)")

                if manually_skipped_count > 0:
                    print(f"Manually skipped {manually_skipped_count} test cases")
            else:
                return 1
        else:
            if args.format == 'test_cases':
                generated_content = generate_test_cases_only(test_entries, all_errors, skip_tests)
            else:
                array_lines = [format_array_entry(info) for info in test_entries]
                generated_content = generate_header(array_lines, all_errors, skip_tests)

            # Write output file
            with open(output_file, 'w') as f:
                f.write(generated_content)

            print(f"Generated {output_file}")
            print(f"Processed {len(test_entries)} test cases ({args.format} format)")

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
