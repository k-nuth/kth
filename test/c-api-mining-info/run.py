#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Compile the actual C boundary with real Asio and a controlled chain double."""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asio-include', type=Path, required=True)
parser.add_argument('--source-root', type=Path, default=ROOT)
parser.add_argument('--cxx', default='c++')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='capi-mining-info-') as directory:
    executable = Path(directory) / 'control'
    subprocess.run([args.cxx, '-std=c++23', '-pthread', '-DASIO_STANDALONE', '-DKTH_LIB_STATIC',
                    '-I' + str(HERE / 'stubs'), '-I' + str(args.asio_include),
                    '-I' + str(ROOT / 'src/c-api/include'),
                    str(args.source_root / 'src/c-api/src/chain/chain_mining.cpp'),
                    str(HERE / 'main.cpp'), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
