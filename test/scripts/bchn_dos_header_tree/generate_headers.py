#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Copies the headers of Bitcoin Cash Node's test/functional/data/blockheader_testnet3.hex (MIT, Copyright (c) The Bitcoin
# developers) into a C++ table. Lines starting with "fork:" are the two headers that fork off the genesis; the others are the
# chain from height 1 to the first checkpoint. The BCHN checkout (the commit named in SOURCE.json) is an argument.
#   generate_headers.py <bchn-checkout> <output.hpp>

import sys


def main():
    root, out = sys.argv[1], sys.argv[2]
    lines = [l.strip() for l in open(root + '/test/functional/data/blockheader_testnet3.hex', encoding='utf8') if l.strip()]
    fork = [l[len('fork:'):] for l in lines if l.startswith('fork:')]
    chain = [l for l in lines if not l.startswith('fork:')]
    for h in fork + chain:
        assert len(h) == 160 and all(c in '0123456789abcdef' for c in h), h
    o = ['// Auto-generated from Bitcoin Cash Node test/functional/data/blockheader_testnet3.hex (MIT) - DO NOT EDIT',
         '// See test/scripts/bchn_dos_header_tree/SOURCE.json for the exact upstream commit.', '',
         '#pragma once', '', '#include <cstddef>', '', 'namespace bchn_testnet3_headers {', '',
         'inline char const* const fork_headers[] = {']
    o += ['    "%s",' % h for h in fork]
    o += ['};', '', 'inline char const* const chain_headers[] = {']
    o += ['    "%s",' % h for h in chain]
    o += ['};', '', 'inline constexpr size_t chain_length = %d;' % len(chain), '', '} // namespace bchn_testnet3_headers', '']
    open(out, 'w').write('\n'.join(o))


if __name__ == '__main__':
    main()
