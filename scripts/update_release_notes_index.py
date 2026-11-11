#!/usr/bin/env python3
"""Keep the release-note index in sync, including after post-release prepends."""

from collections import OrderedDict
from pathlib import Path
import re
import sys

START = '<!-- release-notes-index:start -->'
END = '<!-- release-notes-index:end -->'
VERSION = re.compile(r'^#{1,2} version (\d+\.\d+\.\d+)\s*$')


def update_index(text):
    text = re.sub(re.escape(START) + r'.*?' + re.escape(END) + r'\n*', '', text,
                  flags=re.S)
    lines = []
    versions = []
    legacy = False
    fence = None
    contributors_level = None
    for line in text.splitlines():
        marker = re.match(r'^\s*(`{3,}|~{3,})', line)
        if marker:
            token = marker[1]
            if fence is None:
                fence = token
            elif token[0] == fence[0] and len(token) >= len(fence):
                fence = None
            lines.append(line)
            continue
        if fence:
            lines.append(line)
            continue
        heading = re.match(r'^(#{1,6}) (.+)$', line)
        if contributors_level is not None:
            if heading and len(heading[1]) <= contributors_level:
                contributors_level = None
            else:
                continue
        if (heading and heading[2].strip().lower() == 'new contributors'
                and versions and versions[-1] == '0.68.0'):
            contributors_level = len(heading[1])
            continue
        if re.match(r'^(?:You|ou) can install Knuth node version\b', line):
            continue
        if re.match(r'^\*\*Full Changelog\*\*:', line):
            continue
        version = VERSION.match(line)
        if version:
            value = version[1]
            if value in versions:
                raise ValueError(f'Duplicate release version: {value}')
            versions.append(value)
            legacy = line.startswith('# ')
            line = f'## version {value}'
        elif line.startswith('# '):
            legacy = True
            line = '#' + line
        elif legacy and re.match(r'^#{2,5} ', line):
            line = '#' + line
        lines.append(line)
    if not versions:
        raise ValueError('No release headings found')

    groups = OrderedDict()
    for value in versions:
        major, minor, _ = map(int, value.split('.'))
        group = f'{major}.x' if major else f'0.{minor // 10 * 10}–0.{minor // 10 * 10 + 9}'
        groups.setdefault(group, []).append(f'[{value}](#version-{value.replace(".", "")})')
    index = [START, '# Release notes', '',
             'Dates are UTC publication dates of the linked GitHub releases. Before unified',
             'versioning, component release dates could differ.', '', '## Contents', '']
    index.extend(f'- **{group}:** ' + ' · '.join(links) for group, links in groups.items())
    if any(line == '## Before Knuth 0.1.0' for line in lines):
        index.append('- [Before Knuth 0.1.0: Bitprim and libbitcoin](#before-knuth-010)')
    if any(line.startswith('## Versioning History Note') for line in lines):
        index.append('- [Versioning history: 0.59.0–0.67.0](#versioning-history-note-v0590---v0670)')
    index.extend(['', END])
    return '\n'.join(index) + '\n\n' + '\n'.join(lines).strip() + '\n'


if __name__ == '__main__':
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('docs/release-notes/release-notes.md')
    result = update_index(path.read_text())
    path.write_text(result)
