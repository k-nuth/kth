#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Raw, keyword-free catalog of every test case declared in a Bitcoin Cash Node
# checkout: BOOST_*_TEST_CASE / TEST_CASE macros of src/test and src/*/test,
# and the `def` test methods and subtests of test/functional/*.py.
# It does NOT classify anything; coverage.json is reviewed by hand.
#   catalog_raw.py <bchn-checkout> <output.json>

import json
import os
import re
import subprocess
import sys

# Every Boost.Test case macro form, including data-driven and template ones (BCHN uses only AUTO and FIXTURE
# today; the wider pattern keeps the catalog honest if that changes).
CASE_RE = re.compile(r'^\s*BOOST_(?:AUTO|FIXTURE)_TEST_CASE(?:_TEMPLATE|_WITH_DECOR)?\s*\(\s*([A-Za-z0-9_]+)|^\s*BOOST_DATA_TEST_CASE(?:_F)?\s*\(\s*([A-Za-z0-9_]+)|^\s*BOOST_FIXTURE_TEST_CASE_TEMPLATE\s*\(\s*([A-Za-z0-9_]+)')
SUITE_RE = re.compile(r'^\s*BOOST_(?:FIXTURE_)?TEST_SUITE\s*\(\s*([A-Za-z0-9_]+)')
PY_CLASS_RE = re.compile(r'^class\s+(\w+)')
PY_DEF_RE = re.compile(r'^    def\s+(\w+)\s*\(')
# Mechanical markers of scenarios inside a functional test body, in file order.
SUB_PATTERNS = [
    ('log_info', re.compile(r'self\.log\.info\(\s*(?:f?"([^"]*)"|f?\'([^\']*)\')?')),
    ('next_block', re.compile(r'self\.next_block\(\s*([^,)]+)')),
    ('reject_reason', re.compile(r'reject_reason\s*=\s*(?:"([^"]*)"|\'([^\']*)\')')),
    ('rpc_error', re.compile(r'assert_raises_rpc_error\(\s*(-?\d+|None)\s*,\s*(?:"([^"]*)"|\'([^\']*)\')?')),
    ('expect_error', re.compile(r'expected_error\s*=\s*(?:"([^"]*)"|\'([^\']*)\')')),
    ('txid_reject', re.compile(r"'reject-reason'\s*:\s*'([^']*)'")),
]


def main():
    root, out = sys.argv[1], sys.argv[2]
    sha = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD']).decode().strip()
    tree = subprocess.check_output(['git', '-C', root, 'rev-parse', 'HEAD^{tree}']).decode().strip()
    cases = []
    for base, _, files in os.walk(os.path.join(root, 'src')):
        for fn in sorted(files):
            if not (fn.endswith('.cpp') and ('/test/' in base + '/') and '/fuzz' not in base):
                continue
            path = os.path.join(base, fn)
            rel = os.path.relpath(path, root)
            suite = None
            with open(path, encoding='utf8', errors='replace') as f:
                for i, line in enumerate(f, 1):
                    m = SUITE_RE.match(line)
                    if m:
                        suite = m.group(1)
                    m = CASE_RE.match(line)
                    if m:
                        cases.append({'file': rel, 'suite': suite, 'case': next(g for g in m.groups() if g), 'line': i, 'kind': 'unit'})
    # Fuzz targets (one test_one_input per file), benchmarks and the bitcoin-tx tool vectors.
    for fn in sorted(os.listdir(os.path.join(root, 'src', 'test', 'fuzz'))):
        if fn.endswith('.cpp') and fn not in ('fuzz.cpp',):
            with open(os.path.join(root, 'src', 'test', 'fuzz', fn), encoding='utf8', errors='replace') as f:
                for i, line in enumerate(f, 1):
                    if line.startswith('void test_one_input'):
                        cases.append({'file': 'src/test/fuzz/' + fn, 'suite': None, 'case': fn[:-4], 'line': i, 'kind': 'fuzz'})
    bench_re = re.compile(r'^BENCHMARK\(\s*([A-Za-z0-9_]+)')
    for fn in sorted(os.listdir(os.path.join(root, 'src', 'bench'))):
        if fn.endswith('.cpp'):
            with open(os.path.join(root, 'src', 'bench', fn), encoding='utf8', errors='replace') as f:
                for i, line in enumerate(f, 1):
                    m = bench_re.match(line)
                    if m:
                        cases.append({'file': 'src/bench/' + fn, 'suite': None, 'case': m.group(1), 'line': i, 'kind': 'bench'})
    with open(os.path.join(root, 'test', 'util', 'data', 'bitcoin-util-test.json'), encoding='utf8') as f:
        for i, entry in enumerate(json.load(f)):
            cases.append({'file': 'test/util/data/bitcoin-util-test.json', 'suite': entry.get('exec'), 'case': 'entry %d' % i,
                          'description': entry.get('description', ''), 'line': 0, 'kind': 'util_tool'})
    fdir = os.path.join(root, 'test', 'functional')
    functional_subcases = {}
    for fn in sorted(os.listdir(fdir)):
        if not fn.endswith('.py'):
            continue
        rel = os.path.join('test/functional', fn)
        cls = None
        subcases = []
        with open(os.path.join(fdir, fn), encoding='utf8', errors='replace') as f:
            for i, line in enumerate(f, 1):
                for kind, rx in SUB_PATTERNS:
                    sm = rx.search(line)
                    if sm:
                        text = next((g for g in sm.groups() if g), '')
                        subcases.append({'line': i, 'kind': kind, 'text': text.strip()})
                m = PY_CLASS_RE.match(line)
                if m:
                    cls = m.group(1)
                m = PY_DEF_RE.match(line)
                if m and (m.group(1).startswith('test_') or m.group(1) in ('run_test', 'get_tests')
                          or m.group(1).startswith('_test') or m.group(1).startswith('check_')):
                    cases.append({'file': rel, 'suite': cls, 'case': m.group(1), 'line': i, 'kind': 'functional'})
        functional_subcases[rel] = subcases
    cases.sort(key=lambda c: (c['file'], c['line']))
    with open(out, 'w', encoding='utf8') as f:
        json.dump({'upstreamSha': sha, 'upstreamTree': tree, 'cases': cases,
                   'functionalSubcases': functional_subcases}, f, indent=1)
    print(len(cases), 'cases', sum(len(v) for v in functional_subcases.values()), 'functional markers', sha)


if __name__ == '__main__':
    main()
