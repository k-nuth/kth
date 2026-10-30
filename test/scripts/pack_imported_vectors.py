#!/usr/bin/env python3
"""Pack the existing BCHN/libauth C++ import output into data-only TUs.

This is a second import step: it deliberately consumes adapted cases rather
than reinterpreting upstream JSON, flags, overrides, or exclusions. Unsupported
input fails closed. See imported_vectors.md for the reproducible workflow.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re

COPYRIGHT = '''// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Packed imported vectors - DO NOT EDIT; see test/scripts/imported_vectors.md.
'''


def digest(rows):
    return hashlib.sha256(''.join(row + '\n' for row in rows).encode()).hexdigest()


def original_groups(source):
    groups = []
    for path in sorted(source.glob('script_vm_auto_*.cpp')):
        rows, indices, notes = [], [], []
        for line in path.read_text().splitlines():
            if line.startswith('TEST_CASE('):
                match = re.fullmatch(r'TEST_CASE\("VM-AUTO #(\d+) .*? \{ run_bchn_test\(\{(.*)\}\); \}.*', line)
                if not match:
                    raise ValueError(f'Unsupported BCHN case: {path}')
                index, body = match.groups()
                rows.append(f'    {{{index}, {body}}},')
                indices.append(int(index))
            elif line.startswith('// KNUTH_OVERRIDE') or line.startswith('// SKIPPED') or line.startswith('// TEST_CASE'):
                notes.append(line)
        if not rows:
            raise ValueError(f'No active BCHN cases in {path}')
        groups.append(dict(kind='bchn', source=path.name, suite='bchn', rows=rows,
                           indices=indices, notes=notes))
    for path in sorted(source.glob('vmb_auto_*.cpp')):
        text = path.read_text()
        suite = re.search(r'Auto-generated VMB tests \((\w+)\)', text)[1]
        origin = re.search(r'// Source: (.+)', text)[1]
        flags = re.search(r'static constexpr auto flags = (.*);', text)[1]
        expect = re.search(r'flags, (true|false), e.script_only', text)[1]
        rows = [line for line in text.splitlines() if line.startswith('    {"')]
        notes = [line for line in text.splitlines() if 'SKIPPED:' in line]
        # One loop, one invocation, one static array: no unaccounted test bodies.
        if text.count('TEST_CASE(') != 1 or text.count('run_vmb_test(') != 1 or not rows:
            raise ValueError(f'Unsupported VMB file: {path}')
        groups.append(dict(kind='vmb', source=origin, suite=suite, flags=flags,
                           expect=expect, rows=rows, notes=notes))
    if not groups:
        raise ValueError('No imported vectors found')
    return groups


def manifest(groups):
    return [dict(kind=g['kind'], source=g['source'], suite=g['suite'],
                 flags=g.get('flags'), expect=g.get('expect'),
                 count=len(g['rows']), sha256=digest(g['rows']),
                 notes_sha256=digest(g['notes'])) for g in groups]


def pack(groups, output):
    output.mkdir(parents=True, exist_ok=True)
    suites = collections.defaultdict(list)
    for group in groups:
        suites[group['suite']].append(group)
    for suite, entries in suites.items():
        kind = entries[0]['kind']
        header = 'bchn_vectors.hpp' if kind == 'bchn' else 'vmb_vectors.hpp'
        includes, references = [], []
        for i, group in enumerate(entries):
            # Preserve the original filenames and literal rows for a small,
            # reviewable diff. These files are data fragments, not compiled TUs.
            filename = (group['source'] if kind == 'bchn' else
                        'vmb_auto_' + suite + '_' +
                        Path(group['source']).name.removesuffix('.vmb_tests.json')
                        .replace('.', '_').replace('-', '_').replace(' ', '_') + '.cpp')
            namespace = f'data_{kind}_{suite}_{i}'
            extra = '' if kind == 'bchn' else f', {group["flags"]}, {group["expect"]}'
            lines = [COPYRIGHT, f'#include "{header}"',
                     '#include <kth/domain/machine/script_flags.hpp>', '',
                     f'namespace kth::test::{namespace} {{',
                     f'// Source: {group["source"]}', *group['notes'],
                     f'static constexpr {kind}_vector rows_0[] = {{',
                     *group['rows'], '};',
                     f'static constexpr {kind}_vector_group group = {{',
                     f'    "{group["source"]}", rows_0{extra}',
                     '};', '} // namespace kth::test::' + namespace, '']
            (output / filename).write_text('\n'.join(lines))
            includes.append(f'#include "{filename}"')
            references.append(f'    {namespace}::group,')
        function = 'bchn_vectors' if kind == 'bchn' else f'vmb_{suite}_vectors'
        lines = [COPYRIGHT, '// Each data fragment below has exactly one owning translation unit.',
                 f'#include "{header}"', *includes, '', 'namespace kth::test {',
                 'namespace {', f'static constexpr {kind}_vector_group groups[] = {{',
                 *references, '};', '} // namespace', '',
                 f'std::span<{kind}_vector_group const> {function}() {{',
                 '    return groups;', '}', '} // namespace kth::test', '']
        (output / f'imported_{suite}_data.cpp').write_text('\n'.join(lines))


def packed_groups(source):
    groups = []
    for path in sorted(source.glob('imported_*_data.cpp')):
        text = path.read_text()
        text = re.sub(r'^#include "((?:script_vm_auto_|vmb_auto_).*?\.cpp)"$',
                      lambda match: (source / match[1]).read_text(), text, flags=re.M)
        kind = 'bchn' if path.name == 'imported_bchn_data.cpp' else 'vmb'
        suite = path.name[len('imported_'):-len('_data.cpp')]
        arrays = re.findall(r'// Source: ([^\n]+)\n(.*?)static constexpr \w+_vector rows_(\d+)\[\] = \{\n(.*?)\n\};', text, re.S)
        metadata = re.findall(r'^    "([^"\n]+)", rows_(\d+)(.*?)$', text, re.M)
        if len(arrays) != len(metadata) or not arrays:
            raise ValueError(f'Unsupported packed file: {path}')
        for (origin, notes, array_index, data), (meta_origin, row_index, extra) in zip(arrays, metadata):
            if origin != meta_origin or array_index != row_index:
                raise ValueError(f'Group order mismatch: {path}')
            group = dict(kind=kind, source=origin, suite=suite, rows=data.splitlines(),
                         notes=notes.splitlines())
            if kind == 'vmb':
                flags, expect = extra.removeprefix(', ').rsplit(', ', 1)
                group.update(flags=flags, expect=expect)
            groups.append(group)
    return sorted(groups, key=lambda g: (g['kind'], g['source']))


def check(source, inventory, trace=None):
    groups = packed_groups(source)
    actual = sorted(manifest(groups), key=lambda g: (g['kind'], g['source']))
    expected = sorted(json.loads(inventory.read_text()), key=lambda g: (g['kind'], g['source']))
    if actual != expected:
        raise ValueError('Imported vector inventory differs (cases, flags, expectations, or exclusions)')
    # Retained architectural regression: the old per-case registration layout fails.
    cases = sum(sum(line.startswith('TEST_CASE(') for line in path.read_text().splitlines())
                for path in source.glob('imported_*_data.cpp'))
    if cases:
        raise ValueError('Data TUs must not register tests')
    if trace:
        wanted = []
        for group in groups:
            for index, row in enumerate(group['rows']):
                if group['kind'] == 'bchn':
                    index = int(re.match(r'    \{(\d+),', row)[1])
                wanted.append(f'{group["source"]}:{index}')
        observed = trace.read_text().splitlines()
        if collections.Counter(wanted) != collections.Counter(observed):
            missing = collections.Counter(wanted) - collections.Counter(observed)
            extra = collections.Counter(observed) - collections.Counter(wanted)
            raise ValueError(f'Execution differs: missing={list(missing.items())[:5]}, extra={list(extra.items())[:5]}')
    print(f'Inventory verified: {sum(g["count"] for g in actual)} cases in {len(actual)} source groups')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--trace', type=Path)
    args = parser.parse_args()
    if args.check:
        check(args.source, args.inventory, args.trace)
    else:
        if not args.output or args.source.resolve() == args.output.resolve():
            parser.error('Pack into a separate --output directory; retain original import evidence')
        groups = original_groups(args.source)
        pack(groups, args.output)
        args.inventory.write_text(json.dumps(manifest(groups), indent=2) + '\n')
        check(args.output, args.inventory)


if __name__ == '__main__':
    main()
