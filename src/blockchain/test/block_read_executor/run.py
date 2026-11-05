#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Hold a real disk read until the fetch caller's executor proves progress."""
import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--binary', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='block-read-gate-') as temp:
        hook = Path(temp) / 'gate.so'
        subprocess.run([args.compiler, '-std=c23', '-shared', '-fPIC',
                        str(Path(__file__).with_name('gate.c')), '-ldl', '-o', str(hook)], check=True)
        entered_r, entered_w = os.pipe()
        progress_r, progress_w = os.pipe()
        release_r, release_w = os.pipe()
        trigger_r, trigger_w = os.pipe()
        environment = os.environ.copy()
        # An instrumented executable's dynamic ASan runtime must remain first.
        linked = subprocess.check_output(['ldd', args.binary], text=True)
        asan = re.search(r'(?:libasan[^ ]*|libclang_rt\.asan[^ ]*) => (\S+)', linked)
        preload = ([asan.group(1)] if asan else [])
        if environment.get('LD_PRELOAD'):
            preload.append(environment['LD_PRELOAD'])
        environment.update(LD_PRELOAD=':'.join([*preload, str(hook)]),
                           KTH_BLOCK_READ_ENTERED_FD=str(entered_w),
                           KTH_BLOCK_READ_PROGRESS_FD=str(progress_w),
                           KTH_BLOCK_READ_RELEASE_FD=str(release_r), KTH_BLOCK_READ_TRIGGER_FD=str(trigger_r))
        child = subprocess.Popen([args.binary, '[.disk executor]', '--reporter', 'compact'],
                                 env=environment, pass_fds=(entered_w, progress_w, release_r, trigger_r),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        os.close(entered_w); os.close(progress_w); os.close(release_r); os.close(trigger_r)
        entered = progressed = False
        selector = selectors.DefaultSelector()
        selector.register(entered_r, selectors.EVENT_READ, 'entered')
        selector.register(progress_r, selectors.EVENT_READ, 'progress')
        deadline = time.monotonic() + 15
        try:
            while not (entered and progressed) and time.monotonic() < deadline:
                events = selector.select(max(0, deadline - time.monotonic()))
                if not events:
                    break
                for key, _ in events:
                    value = os.read(key.fd, 1)
                    selector.unregister(key.fd)
                    if not value:
                        continue
                    if key.data == 'entered':
                        entered = True
                        # Ask for progress only once the real disk read is held.
                        os.write(trigger_w, b'Q')
                    else:
                        progressed = True
        finally:
            # Always let the disk call finish before checking the observation.
            # The failing control therefore still closes and joins its real chain.
            if not entered:
                try:
                    os.write(trigger_w, b'Q')
                except BrokenPipeError:
                    pass
            try:
                os.write(release_w, b'G')
            except BrokenPipeError:
                pass
            selector.close()
            for descriptor in (release_w, trigger_w):
                os.close(descriptor)
        try:
            output, _ = child.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            child.kill()
            output, _ = child.communicate()
            print(output)
            os.close(entered_r)
            os.close(progress_r)
            raise RuntimeError('child did not finish after releasing its disk operation')
        os.close(entered_r)
        os.close(progress_r)
        print(output, end='')
        diagnostics = re.search(r'ERROR: AddressSanitizer|WARNING: ThreadSanitizer|runtime error:', output)
        if child.returncode or not entered or not progressed or diagnostics:
            print(f'FAIL: disk entered={entered}, caller progressed before release={progressed}, exit={child.returncode}')
            return 1
        print('PASS: caller executor progressed while its disk read was held')
        return 0


if __name__ == '__main__':
    raise SystemExit(main())
