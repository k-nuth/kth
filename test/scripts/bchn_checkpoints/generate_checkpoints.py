#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Extracts the checkpoint tables of Bitcoin Cash Node's src/chainparams.cpp (MIT, Copyright (c) The Bitcoin
# developers) into a C++ table. The BCHN checkout (the commit named in SOURCE.json) is an argument.
#   generate_checkpoints.py <bchn-checkout> <output.hpp>

import re
import sys

NETWORKS = ['mainnet', 'testnet', 'testnet4', 'scalenet', 'chipnet']


def main():
    root, out = sys.argv[1], sys.argv[2]
    text = open(root + '/src/chainparams.cpp', encoding='utf8').read()
    blocks = re.findall(r'checkpointData = \{\s*/\* \.mapCheckpoints = \*/ \{(.*?)\n\s*\}\s*\};', text, re.S)
    assert len(blocks) >= 5, len(blocks)
    lines = ['// Auto-generated from Bitcoin Cash Node src/chainparams.cpp (MIT) - DO NOT EDIT',
             '// See test/scripts/bchn_checkpoints/SOURCE.json for the exact upstream commit.', '',
             '#pragma once', '', '#include <cstddef>', '', 'namespace bchn_checkpoints {', '',
             'struct row { char const* network; size_t height; char const* hash; };', '',
             'inline row const rows[] = {']
    for net, block in zip(NETWORKS, blocks):
        for height, parts in re.findall(r'\{(\d+),\s*BlockHash::fromHex\(((?:"[0-9a-f]*"\s*)+)\)\}', block):
            h = ''.join(re.findall(r'"([0-9a-f]*)"', parts))
            assert len(h) == 64, (net, height, h)
            lines.append('    {"%s", %s, "%s"},' % (net, height, h))
    lines += ['};', '', '} // namespace bchn_checkpoints', '']
    open(out, 'w').write('\n'.join(lines))


if __name__ == '__main__':
    main()
