#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Compile positive address literals and reject malformed literals deliberately."""
import argparse
from pathlib import Path
import subprocess
import tempfile

NORMAL = 'bitcoincash:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvmtevrfgz'
TOKEN = 'bitcoincash:zpzz8n7jp6847yyx8t33matrgcsdx6c0cvup2jd0h3'
PREAMBLE = r"""
#include <kth/domain/wallet/payment_address.hpp>
#include <kth/domain/wallet/cashaddr.hpp>
using namespace kth;
using namespace kth::domain::wallet;
template <size_t Count, uint8_t First = 0, uint8_t Second = 0, uint8_t Last = 0>
consteval auto malformed_text() {
    data_chunk payload(Count, 0);
    if constexpr (Count > 0) { payload.front() = First; payload.back() = Last; }
    if constexpr (Count > 1) payload[1] = Second;
    auto wire = cashaddr::encode("bitcoincash", payload);
    char text[11 + 1 + Count + 8 + 1]{};
    for (size_t i = 0; i < wire.size(); ++i) text[i] = wire[i];
    return fixed_string{text};
}
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--include-dir', required=True)
    parser.add_argument('--compiler-options-file', required=True)
    args = parser.parse_args()
    positive = [
        ('mainnet_p2pkh', f'"{NORMAL}"_cashaddr'),
        ('uppercase', f'"{NORMAL.upper()}"_cashaddr'),
        ('mainnet_token', f'"{TOKEN}"_tokenaddr'),
        ('testnet', '"bchtest:qpzz8n7jp6847yyx8t33matrgcsdx6c0cvleatp707"_cashaddr'),
        ('regtest_token_p2sh', '"bchreg:rpzz8n7jp6847yyx8t33matrgcsdx6c0cv429mtggk"_tokenaddr'),
        ('regtest_p2sh32', '"bchreg:pvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jqd5ydkgsy"_cashaddr'),
        ('regtest_token_p2sh32', '"bchreg:rvqsyqcyq5rqwzqfpg9scrgwpugpzysnzs23v9ccrydpk8qarc0jql8cv0ff0"_tokenaddr'),
    ]
    negative = [
        ('empty', '""_cashaddr'),
        ('no_prefix', f'"{NORMAL.split(":")[1]}"_cashaddr'),
        ('unknown_prefix', f'"unknown:{NORMAL.split(":")[1]}"_cashaddr'),
        ('legacy', '"17DHrHvtmMRs9ciersFCPNhvJtryd5NWbT"_cashaddr'),
        ('mixed_case', f'"Bitcoincash:{NORMAL.split(":")[1]}"_cashaddr'),
        ('bad_checksum', f'"{NORMAL[:-1]}q"_cashaddr'),
        ('invalid_character', '"bitcoincash:!"_cashaddr'),
        ('embedded_nul', '"bitcoincash:q\\0"_cashaddr'),
        ('normal_suffix_on_token', f'"{TOKEN}"_cashaddr'),
        ('token_suffix_on_normal', f'"{NORMAL}"_tokenaddr'),
        ('empty_payload', 'operator""_cashaddr<malformed_text<0>()>()'),
        ('reserved_version', 'operator""_cashaddr<malformed_text<34, 16>()>()'),
        ('unsupported_type', 'operator""_cashaddr<malformed_text<34, 4>()>()'),
        ('nonzero_padding', 'operator""_cashaddr<malformed_text<34, 0, 0, 1>()>()'),
        ('wrong_hash_length', 'operator""_cashaddr<malformed_text<32>()>()'),
        ('unsupported_hash_size', 'operator""_cashaddr<malformed_text<40, 0, 4>()>()'),
        ('p2pkh32', 'operator""_cashaddr<malformed_text<53, 0, 12>()>()'),
    ]
    failed = 0
    with tempfile.TemporaryDirectory(prefix='address-literals-') as folder:
        source = Path(folder) / 'literal.cpp'
        for expected, cases in [(True, positive), (False, negative)]:
            for name, expression in cases:
                source.write_text(PREAMBLE + '\nconstexpr auto value = ' + expression + ';\n')
                msvc_driver = Path(args.compiler).stem.lower() in ('cl', 'clang-cl')
                options = ['/std:c++latest', '/Zs', '/I' + args.include_dir] if msvc_driver else ['-std=c++23', '-fsyntax-only', '-I' + args.include_dir]
                command = [args.compiler, '@' + args.compiler_options_file, *options, str(source)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=60)
                assertion_failed = 'static assertion failed' in result.stderr or 'static_assert failed' in result.stderr
                passed = result.returncode == 0 if expected else (result.returncode != 0 and assertion_failed and 'address literal' in result.stderr)
                print(('PASS' if passed else 'FAIL') + ': ' + name, flush=True)
                if not passed:
                    failed += 1
                    print(result.stderr)
    print(f'{len(positive) + len(negative) - failed} passed, {failed} failed', flush=True)
    return bool(failed)

if __name__ == '__main__':
    raise SystemExit(main())
