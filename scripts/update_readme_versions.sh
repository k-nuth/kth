#!/usr/bin/env bash
set -euo pipefail

# Run from the repository root, as scripts/release.sh does, before committing
# the release branch. Normalize legacy component packages as well as versions.
if [ "$#" -ne 1 ] || ! [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Usage: $0 <version in x.y.z format>" >&2
    exit 1
fi

python3 - "$1" <<'PY'
from pathlib import Path
import re
import sys

version = sys.argv[1]
packages = r'(?:kth|blockchain|consensus|database|domain|infrastructure|network|node|c-api|secp256k1)'
# Both CLI --requires= and conanfile.txt examples (literal \\n or real lines).
requirement = re.compile(r'(--requires=|\\n|^)' + packages + r'/[0-9]+\.[0-9]+\.[0-9]+', re.MULTILINE)
shared = re.compile(r'(?<![\w/-])' + packages + r'(?:/\*)?:shared=')
for name in ('node-exe', 'blockchain', 'consensus', 'database', 'domain',
             'infrastructure', 'network', 'node', 'c-api', 'secp256k1'):
    path = Path('src') / name / 'README.md'
    if not path.exists():
        continue  # Some monorepo components do not have a README.
    before = path.read_text()
    after = requirement.sub(lambda match: match[1] + 'kth/' + version, before)
    # The escaped newline in printf examples is part of the input text.
    after = re.sub(r'(\\n)' + packages + r'(?:/\*)?:shared=', r'\1kth/*:shared=', after)
    after = shared.sub('kth/*:shared=', after)
    if name == 'consensus':
        lines = after.splitlines(keepends=True)
        for index, line in enumerate(lines):
            if 'conan install --requires=kth/' in line and ':consensus=' not in line:
                lines[index] = line.rstrip('\r\n') + " -o 'kth/*:consensus=True'" + ('\n' if line.endswith('\n') else '')
        after = ''.join(lines)
    if after != before:
        path.write_text(after)
        print(f'Updated {path}: kth/{version}')
PY
