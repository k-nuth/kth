#!/usr/bin/env python3
"""Time only imported-vector object compilation in a configured Ninja build.

Run the same script against separate base/fix builds, with identical configure
arguments and dependencies. Libraries, linking, discovery and execution are
excluded. The build must export compile_commands.json. No compiler cache should
be enabled. Output includes every exact compiler invocation and the Ninja command.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--ninja', default='ninja')
    parser.add_argument('--rebuild', action='store_true', help='Remove only the selected objects before timing')
    args = parser.parse_args()
    build = args.build.resolve()
    commands = json.loads((build / 'compile_commands.json').read_text())
    selected = [entry for entry in commands
                if Path(entry['file']).name.startswith(('script_vm_auto_', 'vmb_auto_', 'imported_'))
                or Path(entry['file']).name == 'vmb_tests.cpp'
                or entry['file'].endswith('/test/chain/script.cpp')]
    if not selected:
        parser.error('No imported-vector commands found')
    objects = []
    for entry in selected:
        command = shlex.split(entry['command'])
        target = command[command.index('-o') + 1]
        path = (Path(entry['directory']) / target).resolve()
        if not path.is_relative_to(build):
            parser.error(f'Object is outside the build directory: {path}')
        if path.exists():
            if not args.rebuild:
                parser.error('Selected objects already exist; use a clean build or --rebuild')
            path.unlink()
        objects.append(str(path.relative_to(build)))
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'compiler-commands.json').write_text(json.dumps(selected, indent=2) + '\n')
    command = [args.ninja, '-C', str(build), f'-j{args.jobs}', *objects]
    start = time.monotonic()
    with (args.output / 'compile.log').open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
    summary = dict(seconds=time.monotonic() - start, returncode=result.returncode,
                   units=len(objects), jobs=args.jobs, command=command)
    (args.output / 'measurement.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({key: summary[key] for key in ('seconds', 'returncode', 'units', 'jobs')}))
    raise SystemExit(result.returncode)


if __name__ == '__main__':
    main()
