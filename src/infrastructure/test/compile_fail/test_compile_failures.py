#!/usr/bin/env python3
# Copyright (c) 2016-2025 Knuth Project developers.
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
from typing import List


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


def generate_test_file(expression: str, include_dir: str) -> str:
    """Generate a temporary C++ file with the test expression."""
    return f'''
#include <kth/infrastructure.hpp>
using namespace kth;

int main() {{
    [[maybe_unused]] auto x = {expression};
    return 0;
}}
'''


def try_compile(source: str, compiler: str, include_dir: str, std: str = "c++23") -> tuple[bool, str]:
    """
    Try to compile the source code.
    Returns (success, error_message).
    """
    with tempfile.NamedTemporaryFile(mode='w', suffix='.cpp', delete=False) as f:
        f.write(source)
        temp_file = f.name

    try:
        result = subprocess.run(
            [compiler, f"-std={std}", "-fsyntax-only", f"-I{include_dir}", temp_file],
            capture_output=True,
            text=True,
            timeout=30
        )
        success = result.returncode == 0
        error_msg = result.stderr if not success else ""
        return success, error_msg
    except subprocess.TimeoutExpired:
        return False, "Compilation timed out"
    except Exception as e:
        return False, str(e)
    finally:
        os.unlink(temp_file)


def run_tests(
    fail_tests: List[CompileFailTest],
    success_tests: List[CompileFailTest],
    compiler: str,
    include_dir: str,
    verbose: bool = False
) -> tuple[int, int]:
    """
    Run all tests and return (passed, failed) counts.
    """
    passed = 0
    failed = 0

    # Test cases that should FAIL to compile
    print("\n=== Testing expressions that should FAIL to compile ===")
    for test in fail_tests:
        source = generate_test_file(test.expression, include_dir)
        compiles, error = try_compile(source, compiler, include_dir)

        if not compiles:
            print(f"  PASS: {test.name} - {test.description}")
            if verbose:
                print(f"        Expression: {test.expression}")
            passed += 1
        else:
            print(f"  FAIL: {test.name} - {test.description}")
            print(f"        Expected compilation to FAIL but it SUCCEEDED")
            print(f"        Expression: {test.expression}")
            failed += 1

    # Test cases that should SUCCEED to compile (sanity checks)
    print("\n=== Testing expressions that should SUCCEED to compile ===")
    for test in success_tests:
        source = generate_test_file(test.expression, include_dir)
        compiles, error = try_compile(source, compiler, include_dir)

        if compiles:
            print(f"  PASS: {test.name} - {test.description}")
            if verbose:
                print(f"        Expression: {test.expression}")
            passed += 1
        else:
            print(f"  FAIL: {test.name} - {test.description}")
            print(f"        Expected compilation to SUCCEED but it FAILED")
            print(f"        Expression: {test.expression}")
            if verbose:
                print(f"        Error: {error[:200]}...")
            failed += 1

    return passed, failed


def main():
    parser = argparse.ArgumentParser(description="Test compile-time failures for UDL operators")
    parser.add_argument("--compiler", default="clang++", help="C++ compiler to use")
    parser.add_argument("--include-dir", required=True, help="Path to infrastructure include directory")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--with-success-tests", action="store_true",
                        help="Also run success tests (requires full build environment)")
    args = parser.parse_args()

    print(f"Using compiler: {args.compiler}")
    print(f"Include directory: {args.include_dir}")

    all_fail_tests = BASE16_FAIL_TESTS + HASH_FAIL_TESTS
    all_success_tests = BASE16_SUCCESS_TESTS + HASH_SUCCESS_TESTS if args.with_success_tests else []

    passed, failed = run_tests(
        all_fail_tests,
        all_success_tests,
        args.compiler,
        args.include_dir,
        args.verbose
    )

    print(f"\n=== Summary ===")
    print(f"Passed: {passed}")
    print(f"Failed: {failed}")
    print(f"Total:  {passed + failed}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
