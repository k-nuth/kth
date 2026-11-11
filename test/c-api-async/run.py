#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying file COPYING.
"""Run actual C wrappers with real Asio/channels and controlled chain dependencies."""
import argparse
from pathlib import Path
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--asio-include', type=Path, required=True)
p.add_argument('--source-root', type=Path, default=ROOT)
p.add_argument('--cxx', default='c++')
p.add_argument('--sanitize', action='store_true')
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='capi-async-') as d:
    output = Path(d) / 'control'
    sources = [a.source_root / 'src/c-api/src' / name for name in ('chain/chain_async.cpp','chain/chain_other.cpp','domain/chain/block_list.cpp')]
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-O1', '-g'] if a.sanitize else []
    subprocess.run([a.cxx, *flags,'-std=c++23','-pthread','-DASIO_STANDALONE','-DASIO_DISABLE_AWAITABLE_FRAME_RECYCLING','-DKTH_LIB_STATIC',
                    '-I'+str(HERE/'stubs'),'-I'+str(a.asio_include),'-I'+str(ROOT/'src/c-api/include'),
                    *map(str,sources),str(HERE/'main.cpp'),'-o',str(output)],check=True)
    subprocess.run([str(output)],check=True,timeout=30)
