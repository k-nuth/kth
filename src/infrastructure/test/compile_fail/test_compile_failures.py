#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Compile-time failure tests for UDL operators.

This script tests that invalid UDL expressions fail to compile.
Each test case generates a temporary C++ file and verifies that
compilation fails as expected.
"""

import subprocess
import tempfile
import os
import sys
import argparse
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import List, Optional


@dataclass
class CompileFailTest:
    name: str
    expression: str
    description: str


# Test cases that should FAIL to compile
BASE16_FAIL_TESTS = [
    CompileFailTest("base16_odd_length_3", '"abc"_base16', "odd length (3 chars)"),
    CompileFailTest("base16_odd_length_5", '"abcde"_base16', "odd length (5 chars)"),
    CompileFailTest("base16_invalid_chars", '"xyz"_base16', "invalid characters"),
    CompileFailTest("base16_invalid_char_middle", '"deadbeXf"_base16', "invalid character in middle"),
    CompileFailTest("base16_all_invalid", '"ghijklmn"_base16', "all invalid characters"),
]

HASH_FAIL_TESTS = [
    CompileFailTest("hash_too_short_8", '"deadbeef"_hash', "too short (8 chars, need 64)"),
    CompileFailTest("hash_empty", '""_hash', "empty string"),
    CompileFailTest("hash_too_short_2", '"00"_hash', "too short (2 chars)"),
    CompileFailTest("hash_63_chars", '"000000000000000000000000000000000000000000000000000000000000000"_hash', "63 chars (odd)"),
    CompileFailTest("hash_65_chars", '"00000000000000000000000000000000000000000000000000000000000000000"_hash', "65 chars (too long)"),
    CompileFailTest("hash_invalid_char", '"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26X"_hash', "invalid character"),
]

AMOUNT_FAIL_TESTS = [
    CompileFailTest("bitcoin_negative", '\"-1\"_bitcoin', "unsigned amount"),
    CompileFailTest("bitcoin_precision", '\"0.000000001\"_bitcoin', "fractional satoshi"),
    CompileFailTest("bitcoin_overflow", '\"184467440737.09551616\"_bitcoin', "overflow"),
    CompileFailTest("bitcoin_numeric_precision", '0.000000001_bitcoin', "fractional satoshi"),
    CompileFailTest("bitcoin_empty", '\"\"_bitcoin', "empty literal"),
    CompileFailTest("base64_bad_alphabet", '\"!!!!\"_base64', "invalid alphabet"),
    CompileFailTest("base64_bad_tail", '\"Z\"_base64', "incomplete tail"),
]

AMOUNT_SUCCESS_TESTS = [
    CompileFailTest("bitcoin_exact", '1.5_bitcoin', "exact decimal literal"),
    CompileFailTest("bitcoin_string", '\"1.5\"_bitcoin', "exact string literal"),
    CompileFailTest("base64_valid", '\"Zm9v\"_base64', "valid base64"),
]

# Test cases that should SUCCEED to compile (sanity checks)
BASE16_SUCCESS_TESTS = [
    CompileFailTest("base16_valid_lower", '"deadbeef"_base16', "valid lowercase"),
    CompileFailTest("base16_valid_upper", '"DEADBEEF"_base16', "valid uppercase"),
    CompileFailTest("base16_valid_empty", '""_base16', "valid empty"),
    CompileFailTest("base16_valid_single", '"00"_base16', "valid single byte"),
]

HASH_SUCCESS_TESTS = [
    CompileFailTest("hash_valid_zeros", '"0000000000000000000000000000000000000000000000000000000000000000"_hash', "valid 64 zeros"),
    CompileFailTest("hash_valid_genesis", '"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"_hash', "valid genesis hash"),
]


CODEC_FAIL_TESTS = [
    CompileFailTest("base10_empty", '""_base10', "empty integer literal"),
    CompileFailTest("base10_negative", '"-1"_base10', "unsigned integer literal"),
    CompileFailTest("base10_fraction", '"1.0"_base10', "integer literal has no decimal point"),
    CompileFailTest("base10_overflow", '"18446744073709551616"_base10', "uint64 overflow"),
    CompileFailTest("base10_space", '" 1"_base10', "whitespace is not a decimal digit"),
    CompileFailTest("base10_nul", r'"1\0"_base10', "embedded NUL"),
    CompileFailTest("base58_zero", '"0"_base58', "character outside base58 alphabet"),
    CompileFailTest("base58_upper_o", '"O"_base58', "character outside base58 alphabet"),
    CompileFailTest("base58_upper_i", '"I"_base58', "character outside base58 alphabet"),
    CompileFailTest("base58_nul", r'"2\0g"_base58', "embedded NUL"),
    CompileFailTest("base85_length", '"Hello!"_base85', "length not divisible by five"),
    CompileFailTest("base85_newline", r'"Test\n"_base85', "invalid character"),
    CompileFailTest("base85_nul", r'"He\0lo"_base85', "embedded NUL"),
    CompileFailTest("base64_padding", '"Zg="_base64', "invalid padding"),
]

CODEC_SUCCESS_TESTS = [
    CompileFailTest("base10_valid", '"42"_base10', "unsigned integer"),
    CompileFailTest("base10_max", '"18446744073709551615"_base10', "uint64 max"),
    CompileFailTest("base10_leading_zeros", '"00042"_base10', "leading zeros"),
    CompileFailTest("base58_valid", '"2g"_base58', "exact decoded size"),
    CompileFailTest("base58_zero_prefix", '"112g"_base58', "leading zero bytes"),
    CompileFailTest("base58_empty", '""_base58', "empty bytes"),
    CompileFailTest("base85_valid", '"HelloWorld"_base85', "Z85 reference vector"),
    CompileFailTest("base85_empty", '""_base85', "empty bytes"),
]


def generate_test_file(expression: str, include_dir: str) -> str:
    """Generate a temporary C++ file with the test expression."""
    return f'''
#include <kth/infrastructure/formats/base_10.hpp>
#include <kth/infrastructure/formats/base_16.hpp>
#include <kth/infrastructure/formats/base_58.hpp>
#include <kth/infrastructure/formats/base_64.hpp>
#include <kth/infrastructure/formats/base_85.hpp>
#include <kth/infrastructure/formats/bitcoin.hpp>
using namespace kth;

int main() {{
    [[maybe_unused]] auto x = {expression};
    return 0;
}}
'''


def try_compile(source: str, compiler: str, include_dir: str, std: str = "c++23", compiler_options_file: Optional[str] = None) -> tuple[Optional[bool], str]:
    """
    Try to compile the source code.
    Returns (compiled, diagnostics); compiled is None for driver failures.
    """
    with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f:
        f.write(source)
        temp_file = f.name

    try:
        msvc = Path(compiler).stem.lower() in ('cl', 'clang-cl')
        options = ['/std:c++latest', '/Zs', '/I' + include_dir] if msvc else [f'-std={std}', '-fsyntax-only', '-I' + include_dir]
        result = subprocess.run(
            [compiler, *(["@" + compiler_options_file] if compiler_options_file else []), *options, temp_file],
            capture_output=True,
            text=True,
            timeout=30
        )
        diagnostics = result.stdout + result.stderr
        if result.returncode < 0:
            return None, f"Compiler terminated by signal {-result.returncode}\n{diagnostics}"
        return result.returncode == 0, diagnostics
    except subprocess.TimeoutExpired:
        return None, "Compilation timed out"
    except Exception as e:
        return None, str(e)
    finally:
        os.unlink(temp_file)


def run_tests(
    fail_tests: List[CompileFailTest],
    success_tests: List[CompileFailTest],
    compiler: str,
    include_dir: str,
    verbose: bool = False,
    compiler_options_file: Optional[str] = None,
    jobs: int = 2
) -> tuple[int, int]:
    """
    Run all tests and return (passed, failed) counts.
    """
    passed = 0
    failed = 0

    def compile_case(test):
        source = generate_test_file(test.expression, include_dir)
        return try_compile(source, compiler, include_dir, compiler_options_file=compiler_options_file)

    # Validate the compiler environment first: a missing header must not make
    # every negative literal appear to pass. Independent compilations have a
    # small fixed concurrency bound and never share temporary source files.
    with ThreadPoolExecutor(max_workers=jobs) as workers:
        for expected, cases in [(True, success_tests), (False, fail_tests)]:
            print(f"\n=== Expressions that should {'SUCCEED' if expected else 'FAIL'} to compile ===", flush=True)
            for test, (compiles, error) in zip(cases, workers.map(compile_case, cases)):
                good = compiles is expected
                print(f"  {'PASS' if good else 'FAIL'}: {test.name} - {test.description}", flush=True)
                if good:
                    passed += 1
                else:
                    failed += 1
                    print(f"        Expression: {test.expression}\n{error}", flush=True)
                if verbose and good:
                    print(f"        Expression: {test.expression}", flush=True)
            if expected and failed:
                print("Negative checks not run: positive compiler controls failed.", flush=True)
                break

    return passed, failed


def main():
    parser = argparse.ArgumentParser(description="Test compile-time failures for UDL operators")
    parser.add_argument("--compiler", default="clang++", help="C++ compiler to use")
    parser.add_argument("--include-dir", required=True, help="Path to infrastructure include directory")
    parser.add_argument("--compiler-options-file", help="Compiler response file with the actual target includes and definitions")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--with-success-tests", action="store_true",
                        help="Also run success tests (requires full build environment)")
    parser.add_argument('--jobs', type=int, default=2, choices=range(1, 9), help='Bound independent compiler processes (default: 2)')
    args = parser.parse_args()

    print(f"Using compiler: {args.compiler}")
    print(f"Include directory: {args.include_dir}")

    all_fail_tests = BASE16_FAIL_TESTS + HASH_FAIL_TESTS + AMOUNT_FAIL_TESTS + CODEC_FAIL_TESTS
    all_success_tests = BASE16_SUCCESS_TESTS + HASH_SUCCESS_TESTS + AMOUNT_SUCCESS_TESTS + CODEC_SUCCESS_TESTS if args.with_success_tests else BASE16_SUCCESS_TESTS[:1]

    passed, failed = run_tests(
        all_fail_tests,
        all_success_tests,
        args.compiler,
        args.include_dir,
        args.verbose,
        args.compiler_options_file,
        args.jobs
    )

    print(f"\n=== Summary ===")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Total:  {passed + failed}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
