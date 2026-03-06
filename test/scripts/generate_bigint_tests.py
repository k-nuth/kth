#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Transforms BCHN's BigInt test data into kth's test format.

Reads BCHN's bigint_test_vectors.json and the OpenSSL-format JSON files
(bigint_sum_tests.json, bigint_mul_tests.json, etc.) and generates Catch2
test files for kth's big_number class.

Usage:
    python transform_bchn_bigint_tests.py <bchn_test_data_dir> <output_dir>

Example:
    python transform_bchn_bigint_tests.py \
        /path/to/bchn/src/test/data \
        /path/to/kth-master/src/infrastructure/test/machine/
"""

import json
import os
import sys
import textwrap


# ─── Configuration ───────────────────────────────────────────────────────────

# kth class name for BigInt
KTH_BIGINT_CLASS = 'big_number'

# Max tests per file to avoid huge compilation units
MAX_TESTS_PER_SECTION = 200


# ─── File header ─────────────────────────────────────────────────────────────

FILE_HEADER = '''\
// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// AUTO-GENERATED from BCHN bigint test data by transform_bchn_bigint_tests.py
// Do not edit manually. Re-run the script to regenerate.

#include <test_helpers.hpp>

#include <kth/infrastructure/machine/big_number.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace kth::machine;

namespace {

using bytes_t = std::vector<uint8_t>;

// Helper: parse hex string to bytes (little-endian CScriptNum format)
bytes_t hex_to_bytes(std::string_view hex) {
    bytes_t result;
    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        auto byte_str = std::string(hex.substr(i, 2));
        result.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
    }
    return result;
}

} // anonymous namespace
'''


# ─── JSON loading ────────────────────────────────────────────────────────────

def load_json_relaxed(filepath):
    """Load a JSON file, stripping comment strings (BCHN style)."""
    with open(filepath, 'r') as f:
        text = f.read()

    # BCHN JSON files have string entries that are comments (start with #)
    # and bare string entries for titles. The standard JSON parser handles
    # arrays with mixed types fine, we just skip non-dict entries.
    return json.loads(text)


def load_test_vectors(data_dir):
    """Load bigint_test_vectors.json."""
    path = os.path.join(data_dir, 'bigint_test_vectors.json')
    if not os.path.exists(path):
        print(f"ERROR: {path} not found")
        sys.exit(1)
    return json.load(open(path))


def load_openssl_tests(data_dir, filename):
    """Load an OpenSSL-format JSON test file (sum, mul, mod, shift, exp)."""
    path = os.path.join(data_dir, filename)
    if not os.path.exists(path):
        return None
    data = load_json_relaxed(path)
    # Filter out comment strings, keep only dict entries
    return [entry for entry in data if isinstance(entry, dict)]


# ─── Code generation: bigint_test_vectors.json tests ─────────────────────────

def generate_binary_op_tests(numbers, op_key, op_symbol, entries):
    """Generate tests for binary operations (+, -, *, /, %, &, |, ^)."""
    lines = []
    cpp_op = op_symbol
    test_name = {
        '+': 'addition', '-': 'subtraction', '*': 'multiplication',
        '/': 'division', '%': 'modulo',
        '&': 'bitwise_and', '|': 'bitwise_or', '^': 'bitwise_xor',
    }.get(op_key, op_key)

    lines.append(f'TEST_CASE("BigInt [gen]: {test_name}", "[bigint][gen]") {{')

    for i, entry in enumerate(entries):
        op1_idx, op2_idx = entry[0], entry[1]
        expected = entry[2]
        op1_str = numbers[op1_idx]
        op2_str = numbers[op2_idx]

        if expected == "exception":
            lines.append(f'    SECTION("#{i}: {op_key} exception") {{')
            lines.append(f'        {KTH_BIGINT_CLASS} a("{op1_str}");')
            lines.append(f'        {KTH_BIGINT_CLASS} b("{op2_str}");')
            lines.append(f'        CHECK_THROWS(a {cpp_op} b);')
            lines.append(f'    }}')
        else:
            lines.append(f'    SECTION("#{i}: {op_key}") {{')
            lines.append(f'        {KTH_BIGINT_CLASS} a("{op1_str}");')
            lines.append(f'        {KTH_BIGINT_CLASS} b("{op2_str}");')
            lines.append(f'        auto result = a {cpp_op} b;')
            lines.append(f'        CHECK(result.to_string() == "{expected}");')
            # Also test compound assignment
            lines.append(f'        {KTH_BIGINT_CLASS} a2("{op1_str}");')
            lines.append(f'        a2 {cpp_op}= b;')
            lines.append(f'        CHECK(a2.to_string() == "{expected}");')
            lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_comparison_tests(numbers, entries):
    """Generate comparison tests (<=>)."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: comparison", "[bigint][gen]") {{')

    for i, entry in enumerate(entries):
        op1_idx, op2_idx, cmp = entry[0], entry[1], entry[2]
        op1_str = numbers[op1_idx]
        op2_str = numbers[op2_idx]

        lines.append(f'    SECTION("#{i}: <=>") {{')
        lines.append(f'        {KTH_BIGINT_CLASS} a("{op1_str}");')
        lines.append(f'        {KTH_BIGINT_CLASS} b("{op2_str}");')
        lines.append(f'        CHECK(a.compare(b) == {cmp});')

        if cmp == 0:
            lines.append(f'        CHECK(a == b);')
            lines.append(f'        CHECK_FALSE(a != b);')
            lines.append(f'        CHECK_FALSE(a < b);')
            lines.append(f'        CHECK(a <= b);')
            lines.append(f'        CHECK_FALSE(a > b);')
            lines.append(f'        CHECK(a >= b);')
        elif cmp < 0:
            lines.append(f'        CHECK_FALSE(a == b);')
            lines.append(f'        CHECK(a != b);')
            lines.append(f'        CHECK(a < b);')
            lines.append(f'        CHECK(a <= b);')
            lines.append(f'        CHECK_FALSE(a > b);')
            lines.append(f'        CHECK_FALSE(a >= b);')
        else:
            lines.append(f'        CHECK_FALSE(a == b);')
            lines.append(f'        CHECK(a != b);')
            lines.append(f'        CHECK_FALSE(a < b);')
            lines.append(f'        CHECK_FALSE(a <= b);')
            lines.append(f'        CHECK(a > b);')
            lines.append(f'        CHECK(a >= b);')

        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_shift_tests(numbers, op_key, entries):
    """Generate shift tests (<< or >>)."""
    lines = []
    direction = 'left_shift' if op_key == '<<' else 'right_shift'
    cpp_op = op_key

    lines.append(f'TEST_CASE("BigInt [gen]: {direction}", "[bigint][gen]") {{')

    for i, entry in enumerate(entries):
        op1_idx, shift_amt = entry[0], entry[1]
        expected = entry[2]
        op1_str = numbers[op1_idx]

        lines.append(f'    SECTION("#{i}: {op_key}") {{')
        lines.append(f'        {KTH_BIGINT_CLASS} a("{op1_str}");')
        lines.append(f'        auto result = a {cpp_op} {shift_amt};')
        lines.append(f'        CHECK(result.to_string() == "{expected}");')
        lines.append(f'        a {cpp_op}= {shift_amt};')
        lines.append(f'        CHECK(a.to_string() == "{expected}");')
        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_inc_dec_tests(numbers, op_key, entries):
    """Generate increment/decrement tests (++ or --)."""
    lines = []
    name = 'increment' if op_key == '++' else 'decrement'

    lines.append(f'TEST_CASE("BigInt [gen]: {name}", "[bigint][gen]") {{')

    for i, entry in enumerate(entries):
        op_idx = entry[0]
        expected = entry[1]
        op_str = numbers[op_idx]

        lines.append(f'    SECTION("#{i}: {op_key}") {{')
        lines.append(f'        {KTH_BIGINT_CLASS} a("{op_str}");')

        if op_key == '++':
            lines.append(f'        // post-increment')
            lines.append(f'        {KTH_BIGINT_CLASS} a2("{op_str}");')
            lines.append(f'        auto prev = a2++;')
            lines.append(f'        CHECK(prev.to_string() == "{op_str}");')
            lines.append(f'        CHECK(a2.to_string() == "{expected}");')
            lines.append(f'        // pre-increment')
            lines.append(f'        {KTH_BIGINT_CLASS} a3("{op_str}");')
            lines.append(f'        auto& ref = ++a3;')
            lines.append(f'        CHECK(ref.to_string() == "{expected}");')
        else:
            lines.append(f'        // post-decrement')
            lines.append(f'        {KTH_BIGINT_CLASS} a2("{op_str}");')
            lines.append(f'        auto prev = a2--;')
            lines.append(f'        CHECK(prev.to_string() == "{op_str}");')
            lines.append(f'        CHECK(a2.to_string() == "{expected}");')
            lines.append(f'        // pre-decrement')
            lines.append(f'        {KTH_BIGINT_CLASS} a3("{op_str}");')
            lines.append(f'        auto& ref = --a3;')
            lines.append(f'        CHECK(ref.to_string() == "{expected}");')

        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_boolean_op_tests(numbers, op_key, entries):
    """Generate boolean operation tests (&& or ||)."""
    lines = []
    name = 'logical_and' if op_key == '&&' else 'logical_or'

    lines.append(f'TEST_CASE("BigInt [gen]: {name}", "[bigint][gen]") {{')

    for i, entry in enumerate(entries):
        op1_idx, op2_idx = entry[0], entry[1]
        expected = 'true' if entry[2] else 'false'
        op1_str = numbers[op1_idx]
        op2_str = numbers[op2_idx]

        lines.append(f'    SECTION("#{i}: {op_key}") {{')
        lines.append(f'        {KTH_BIGINT_CLASS} a("{op1_str}");')
        lines.append(f'        {KTH_BIGINT_CLASS} b("{op2_str}");')

        if op_key == '&&':
            lines.append(f'        CHECK((a.is_nonzero() && b.is_nonzero()) == {expected});')
        else:
            lines.append(f'        CHECK((a.is_nonzero() || b.is_nonzero()) == {expected});')

        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_serialization_tests(numbers):
    """Generate serialization round-trip tests for all numbers in the vector set."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: serialization round-trip", "[bigint][gen]") {{')

    for i, num_str in enumerate(numbers):
        lines.append(f'    SECTION("#{i}") {{')
        lines.append(f'        {KTH_BIGINT_CLASS} a("{num_str}");')
        lines.append(f'        CHECK(a.to_string() == "{num_str}");')
        lines.append(f'        auto bytes = a.serialize();')
        lines.append(f'        {KTH_BIGINT_CLASS} b;')
        lines.append(f'        b.deserialize(bytes);')
        lines.append(f'        CHECK(b.to_string() == "{num_str}");')
        lines.append(f'        CHECK(a == b);')
        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


# ─── Code generation: OpenSSL-format tests (sum, mul, mod, exp, shift) ────────

def generate_openssl_sum_tests(entries):
    """Generate tests from bigint_sum_tests.json."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: OpenSSL sum tests", "[bigint][gen][openssl]") {{')

    for i, entry in enumerate(entries):
        a_hex = entry.get('A', '')
        b_hex = entry.get('B', '')
        sum_hex = entry.get('Sum', '')

        lines.append(f'    SECTION("#{i}") {{')
        lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
        lines.append(f'        auto b = {KTH_BIGINT_CLASS}::from_hex("{b_hex}");')
        lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{sum_hex}");')
        lines.append(f'        CHECK(a + b == expected);')
        lines.append(f'        CHECK(expected - b == a);')
        lines.append(f'        CHECK(expected - a == b);')
        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_openssl_mul_tests(entries):
    """Generate tests from bigint_mul_tests.json (Square, Product, Quotient)."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: OpenSSL mul/square tests", "[bigint][gen][openssl]") {{')

    for i, entry in enumerate(entries):
        if 'Square' in entry and 'A' in entry:
            a_hex = entry['A']
            sq_hex = entry['Square']
            lines.append(f'    SECTION("square #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{sq_hex}");')
            lines.append(f'        CHECK(a * a == expected);')
            lines.append(f'    }}')
        elif 'Product' in entry:
            a_hex = entry.get('A', '')
            b_hex = entry.get('B', '')
            prod_hex = entry['Product']
            lines.append(f'    SECTION("product #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto b = {KTH_BIGINT_CLASS}::from_hex("{b_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{prod_hex}");')
            lines.append(f'        CHECK(a * b == expected);')
            lines.append(f'    }}')
        elif 'Quotient' in entry:
            a_hex = entry.get('A', '')
            b_hex = entry.get('B', '')
            q_hex = entry['Quotient']
            r_hex = entry.get('Remainder', '0')
            lines.append(f'    SECTION("quotient #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto b = {KTH_BIGINT_CLASS}::from_hex("{b_hex}");')
            lines.append(f'        auto expected_q = {KTH_BIGINT_CLASS}::from_hex("{q_hex}");')
            lines.append(f'        auto expected_r = {KTH_BIGINT_CLASS}::from_hex("{r_hex}");')
            lines.append(f'        CHECK(a / b == expected_q);')
            lines.append(f'        CHECK(a % b == expected_r);')
            lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_openssl_exp_tests(entries):
    """Generate tests from bigint_exp_tests.json."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: OpenSSL exp tests", "[bigint][gen][openssl]") {{')

    for i, entry in enumerate(entries):
        a_hex = entry.get('A', '')
        e_hex = entry.get('E', '')
        exp_hex = entry.get('Exp', '')

        lines.append(f'    SECTION("#{i}") {{')
        lines.append(f'        auto base = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
        lines.append(f'        auto exp = {KTH_BIGINT_CLASS}::from_hex("{e_hex}");')
        lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{exp_hex}");')
        lines.append(f'        CHECK(base.pow(exp) == expected);')
        lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_openssl_mod_tests(entries):
    """Generate tests from bigint_mod_tests.json (ModMul, ModExp, ModSqrt)."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: OpenSSL mod tests", "[bigint][gen][openssl]") {{')

    for i, entry in enumerate(entries):
        m_hex = entry.get('M', '')
        if 'ModMul' in entry:
            a_hex = entry.get('A', '')
            b_hex = entry.get('B', '')
            result_hex = entry['ModMul']
            lines.append(f'    SECTION("modmul #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto b = {KTH_BIGINT_CLASS}::from_hex("{b_hex}");')
            lines.append(f'        auto m = {KTH_BIGINT_CLASS}::from_hex("{m_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{result_hex}");')
            lines.append(f'        CHECK((a * b).math_modulo(m) == expected);')
            lines.append(f'    }}')
        elif 'ModExp' in entry:
            a_hex = entry.get('A', '')
            e_hex = entry.get('E', '')
            result_hex = entry['ModExp']
            lines.append(f'    SECTION("modexp #{i}") {{')
            lines.append(f'        auto base = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto exp = {KTH_BIGINT_CLASS}::from_hex("{e_hex}");')
            lines.append(f'        auto m = {KTH_BIGINT_CLASS}::from_hex("{m_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{result_hex}");')
            lines.append(f'        CHECK(base.pow_mod(exp, m) == expected);')
            lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


def generate_openssl_shift_tests(entries):
    """Generate tests from bigint_shift_tests.json."""
    lines = []
    lines.append(f'TEST_CASE("BigInt [gen]: OpenSSL shift tests", "[bigint][gen][openssl]") {{')

    for i, entry in enumerate(entries):
        if 'LShift1' in entry:
            a_hex = entry.get('A', '')
            result_hex = entry['LShift1']
            lines.append(f'    SECTION("lshift1 #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{result_hex}");')
            lines.append(f'        CHECK((a << 1) == expected);')
            lines.append(f'    }}')
        elif 'LShift' in entry:
            a_hex = entry.get('A', '')
            n_hex = entry.get('N', '0')
            n = int(n_hex, 16) if isinstance(n_hex, str) else n_hex
            result_hex = entry['LShift']
            lines.append(f'    SECTION("lshift #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{result_hex}");')
            lines.append(f'        CHECK((a << {n}) == expected);')
            lines.append(f'    }}')
        elif 'RShift' in entry:
            a_hex = entry.get('A', '')
            n_hex = entry.get('N', '0')
            n = int(n_hex, 16) if isinstance(n_hex, str) else n_hex
            result_hex = entry['RShift']
            lines.append(f'    SECTION("rshift #{i}") {{')
            lines.append(f'        auto a = {KTH_BIGINT_CLASS}::from_hex("{a_hex}");')
            lines.append(f'        auto expected = {KTH_BIGINT_CLASS}::from_hex("{result_hex}");')
            lines.append(f'        CHECK((a >> {n}) == expected);')
            lines.append(f'    }}')

    lines.append('}')
    lines.append('')
    return '\n'.join(lines)


# ─── File splitting ──────────────────────────────────────────────────────────

def write_file(filepath, content):
    """Write content to file."""
    with open(filepath, 'w') as f:
        f.write(content)
    print(f"  Generated: {filepath}")


# ─── Main ────────────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <json_data_dir> <output_dir>")
        print(f"Example: {sys.argv[0]} test/scripts/ src/infrastructure/test/machine/")
        sys.exit(1)

    data_dir = sys.argv[1]
    output_dir = sys.argv[2]

    if not os.path.isdir(data_dir):
        print(f"ERROR: {data_dir} is not a directory")
        sys.exit(1)

    os.makedirs(output_dir, exist_ok=True)

    # ── Load test vectors ──
    print("Loading bigint_test_vectors.json...")
    tv = load_test_vectors(data_dir)
    numbers = tv['numbers']
    print(f"  {len(numbers)} numbers loaded")

    total_tests = 0
    file_count = 0

    # ── Generate one file per operation from bigint_test_vectors.json ──

    # Binary ops — one file each
    for op_key, op_sym, fname in [
        ('+', '+', 'addition'), ('-', '-', 'subtraction'),
        ('*', '*', 'multiplication'), ('/', '/', 'division'),
        ('%', '%', 'modulo'),
        ('&', '&', 'bitwise_and'), ('|', '|', 'bitwise_or'),
        ('^', '^', 'bitwise_xor'),
    ]:
        if op_key in tv:
            entries = tv[op_key]
            print(f"  {op_key}: {len(entries)} test vectors")
            content = FILE_HEADER + '\n' + generate_binary_op_tests(numbers, op_key, op_sym, entries)
            write_file(os.path.join(output_dir, f'big_number_gen_{fname}.cpp'), content)
            total_tests += len(entries)
            file_count += 1

    # Comparisons
    if '<=>' in tv:
        entries = tv['<=>']
        print(f"  <=>: {len(entries)} test vectors")
        content = FILE_HEADER + '\n' + generate_comparison_tests(numbers, entries)
        write_file(os.path.join(output_dir, 'big_number_gen_comparison.cpp'), content)
        total_tests += len(entries)
        file_count += 1

    # Shifts
    for op_key, fname in [('<<', 'left_shift'), ('>>', 'right_shift')]:
        if op_key in tv:
            entries = tv[op_key]
            print(f"  {op_key}: {len(entries)} test vectors")
            content = FILE_HEADER + '\n' + generate_shift_tests(numbers, op_key, entries)
            write_file(os.path.join(output_dir, f'big_number_gen_{fname}.cpp'), content)
            total_tests += len(entries)
            file_count += 1

    # Increment/decrement
    for op_key, fname in [('++', 'increment'), ('--', 'decrement')]:
        if op_key in tv:
            entries = tv[op_key]
            print(f"  {op_key}: {len(entries)} test vectors")
            content = FILE_HEADER + '\n' + generate_inc_dec_tests(numbers, op_key, entries)
            write_file(os.path.join(output_dir, f'big_number_gen_{fname}.cpp'), content)
            total_tests += len(entries)
            file_count += 1

    # Boolean ops
    for op_key, fname in [('&&', 'logical_and'), ('||', 'logical_or')]:
        if op_key in tv:
            entries = tv[op_key]
            print(f"  {op_key}: {len(entries)} test vectors")
            content = FILE_HEADER + '\n' + generate_boolean_op_tests(numbers, op_key, entries)
            write_file(os.path.join(output_dir, f'big_number_gen_{fname}.cpp'), content)
            total_tests += len(entries)
            file_count += 1

    # Serialization round-trip
    print(f"  serialization: {len(numbers)} round-trip tests")
    content = FILE_HEADER + '\n' + generate_serialization_tests(numbers)
    write_file(os.path.join(output_dir, 'big_number_gen_serialization.cpp'), content)
    total_tests += len(numbers)
    file_count += 1

    # ── Load and generate OpenSSL-format tests (one file per JSON) ──

    openssl_total = 0

    sum_entries = load_openssl_tests(data_dir, 'bigint_sum_tests.json')
    if sum_entries:
        print(f"  OpenSSL sum: {len(sum_entries)} tests")
        content = FILE_HEADER + '\n' + generate_openssl_sum_tests(sum_entries)
        write_file(os.path.join(output_dir, 'big_number_gen_openssl_sum.cpp'), content)
        openssl_total += len(sum_entries)
        file_count += 1

    mul_entries = load_openssl_tests(data_dir, 'bigint_mul_tests.json')
    if mul_entries:
        print(f"  OpenSSL mul: {len(mul_entries)} tests")
        content = FILE_HEADER + '\n' + generate_openssl_mul_tests(mul_entries)
        write_file(os.path.join(output_dir, 'big_number_gen_openssl_mul.cpp'), content)
        openssl_total += len(mul_entries)
        file_count += 1

    exp_entries = load_openssl_tests(data_dir, 'bigint_exp_tests.json')
    if exp_entries:
        print(f"  OpenSSL exp: {len(exp_entries)} tests")
        content = FILE_HEADER + '\n' + generate_openssl_exp_tests(exp_entries)
        write_file(os.path.join(output_dir, 'big_number_gen_openssl_exp.cpp'), content)
        openssl_total += len(exp_entries)
        file_count += 1

    mod_entries = load_openssl_tests(data_dir, 'bigint_mod_tests.json')
    if mod_entries:
        print(f"  OpenSSL mod: {len(mod_entries)} tests")
        content = FILE_HEADER + '\n' + generate_openssl_mod_tests(mod_entries)
        write_file(os.path.join(output_dir, 'big_number_gen_openssl_mod.cpp'), content)
        openssl_total += len(mod_entries)
        file_count += 1

    shift_entries = load_openssl_tests(data_dir, 'bigint_shift_tests.json')
    if shift_entries:
        print(f"  OpenSSL shift: {len(shift_entries)} tests")
        content = FILE_HEADER + '\n' + generate_openssl_shift_tests(shift_entries)
        write_file(os.path.join(output_dir, 'big_number_gen_openssl_shift.cpp'), content)
        openssl_total += len(shift_entries)
        file_count += 1

    # ── Summary ──
    print(f"\nSummary:")
    print(f"  bigint_test_vectors.json: {total_tests} tests")
    print(f"  OpenSSL format tests: {openssl_total} tests")
    print(f"  Total: {total_tests + openssl_total} tests in {file_count} files")


if __name__ == '__main__':
    main()
