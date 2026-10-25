#!/usr/bin/env python3
"""Black-box shutdown regression through the actual node executable.

Run: python3 rpc_shutdown.py /absolute/path/to/kth /new/evidence/directory
Retains the fresh regtest database and logs, including on failure.
"""
import argparse
import base64
import json
import pathlib
import selectors
import signal
import socket
import subprocess
import sys
import tempfile
import time

if sys.platform == 'linux':
    import resource

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary', type=pathlib.Path)
parser.add_argument('evidence', type=pathlib.Path)
parser.add_argument('--unique-evidence', action='store_true',
                    help='Retain each invocation in a new subdirectory of the evidence root')
parser.add_argument('--existing-database', type=pathlib.Path,
                    help='Reuse an explicitly selected, stopped mainnet database without initializing it')
parser.add_argument('--modes', nargs='+', default=['rpc-off', 'rpc-cli-off', 'rpc-idle', 'rpc-partial-http', 'rpc-full-admission'],
                    choices=['rpc-off', 'rpc-cli-off', 'rpc-idle', 'rpc-partial-http', 'rpc-full-admission', 'rpc-accept-recovery'])
args = parser.parse_args()
if args.unique_evidence:
    args.evidence.mkdir(parents=True, exist_ok=True)
    args.evidence = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=args.evidence))
else:
    args.evidence.mkdir(parents=True, exist_ok=False)
failed = False
for mode in args.modes:
    directory = args.evidence / mode
    directory.mkdir()
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    config = directory / 'node.cfg'
    database = args.existing_database or directory/'database'
    config.write_text(f'db.directory = {database}\ndb.db_mode = blocks\nnode.display = log\nnet.inbound_connections = 0\nnet.inbound_port = 0\nnet.outbound_connections = 0\nrpc.enabled = {str(mode != "rpc-off").lower()}\nrpc.bind = 127.0.0.1\nrpc.port = {port}\nrpc.user = kth\nrpc.password = test\n')
    argv = [str(args.binary), '--network', 'mainnet' if args.existing_database else 'regtest', '--config', str(config)]
    if not args.existing_database:
        argv.append('--init_run')
    if mode == 'rpc-cli-off':
        argv.append('--rpc.enabled=false')
    process = subprocess.Popen(argv, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = bytearray()
    connections = []
    previous_fd_limit = None
    result = {'mode': mode, 'evidence': str(directory)}
    try:
        deadline = time.monotonic() + 120
        marker = b'Node started successfully' if mode == 'rpc-off' else (
            b'JSON-RPC server disabled' if mode == 'rpc-cli-off' else b'JSON-RPC server listening')
        while marker not in output:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('node did not become ready before failure deadline')
            if not selector.select(remaining):
                raise RuntimeError('readiness event timed out')
            chunk = process.stdout.read(65536)
            if not chunk:
                raise RuntimeError('node exited before readiness')
            output.extend(chunk)
        if mode == 'rpc-idle':
            connections.append(socket.create_connection(('127.0.0.1', port), timeout=5))
        if mode in ('rpc-partial-http', 'rpc-full-admission'):
            for _ in range(64 if mode == 'rpc-full-admission' else 1):
                connection = socket.create_connection(('127.0.0.1', port), timeout=5)
                connections.append(connection)
                # A response proves admission, not merely a kernel backlog entry.
                body = b'{"jsonrpc":"1.0","id":1,"method":"getblockcount","params":[]}'
                authorization = base64.b64encode(b'kth:test')
                connection.sendall(b'POST / HTTP/1.1\r\nHost: localhost\r\nAuthorization: Basic ' + authorization +
                    b'\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
                response = bytearray()
                while b'\r\n\r\n' not in response:
                    chunk = connection.recv(8192)
                    if not chunk:
                        raise RuntimeError('connection closed before authenticated response')
                    response.extend(chunk)
                headers, received_body = response.split(b'\r\n\r\n', 1)
                if not headers.startswith(b'HTTP/1.1 200 ') or b'connection: keep-alive' not in headers.lower():
                    raise RuntimeError('authenticated request did not keep the connection admitted')
                length = int(next(line.split(b':', 1)[1] for line in headers.lower().split(b'\r\n')
                                  if line.startswith(b'content-length:')))
                while len(received_body) < length:
                    chunk = connection.recv(8192)
                    if not chunk:
                        raise RuntimeError('connection closed during authenticated response')
                    received_body.extend(chunk)
                connection.sendall(b'POST / HTTP/1.1\r\nHost: localhost\r\n')
            result['authenticated_admitted'] = len(connections)
        if mode == 'rpc-accept-recovery':
            if sys.platform != 'linux':
                raise RuntimeError('accept recovery control requires Linux per-child prlimit and /proc')
            # RPC binding can precede the other core tasks' first suspension.
            # Do not starve unrelated startup (or sanitizer runtime setup): wait
            # for the real all-tasks-started event before injecting the shortage.
            while b'tasks spawned, running...' not in output:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise RuntimeError('core tasks did not become ready before descriptor injection')
                chunk = process.stdout.read(65536)
                if not chunk:
                    raise RuntimeError('node exited before core task readiness')
                output.extend(chunk)
            # Exhaust only this owned child's descriptor allowance, not the host
            # or the test runner. The real async_accept must report EMFILE.
            previous_fd_limit = resource.prlimit(process.pid, resource.RLIMIT_NOFILE)
            descriptors = len(list(pathlib.Path(f'/proc/{process.pid}/fd').iterdir()))
            resource.prlimit(process.pid, resource.RLIMIT_NOFILE,
                             (descriptors + 4, previous_fd_limit[1]))
            observed_from = len(output)
            for _ in range(12):
                try:
                    connections.append(socket.create_connection(('127.0.0.1', port), timeout=5))
                except ConnectionRefusedError:
                    # The defect can close the listener during admission. Still
                    # consume its actual completion event before diagnosing it.
                    break
            deadline = time.monotonic() + 10
            while not any(marker in output[observed_from:] for marker in
                          (b'[rpc] accept failed:', b'[rpc] Listener stopped')):
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not selector.select(remaining):
                    raise RuntimeError('no observable accept failure before deadline')
                chunk = process.stdout.read(65536)
                if not chunk:
                    raise RuntimeError('node exited before accept failure')
                output.extend(chunk)
            accept_observation = output[observed_from:]
            if b'[rpc] accept failed:' in accept_observation and b'Too many open files' not in accept_observation:
                raise RuntimeError('accept failed for a reason other than the injected descriptor shortage')
            resource.prlimit(process.pid, resource.RLIMIT_NOFILE, previous_fd_limit)
            previous_fd_limit = None
            for connection in connections:
                connection.close()
            connections.clear()
            # Backlog entries are not evidence: require a response after the
            # resource shortage has gone away, from the same live listener.
            connection = socket.create_connection(('127.0.0.1', port), timeout=5)
            connections.append(connection)
            connection.sendall(b'POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n')
            if not connection.recv(8192).startswith(b'HTTP/1.1 401 '):
                raise RuntimeError('RPC did not recover after the descriptor shortage')
            result['accept_recovered'] = True
        if mode == 'rpc-off' and not args.existing_database:
            busy = subprocess.run([str(args.binary), '--network', 'regtest', '--config', str(config), '--compact-utxo'], cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
            (directory/'compact-busy.log').write_bytes(busy.stdout)
            if busy.returncode == 0 or b'Another instance already holds' not in busy.stdout:
                raise RuntimeError('compaction did not refuse the live database for the expected lock reason')
        started = time.monotonic()
        process.send_signal(signal.SIGINT)
        # Timeout bounds failure only; success is actual process exit and clean log.
        tail, _ = process.communicate(timeout=15)
        output.extend(tail)
        result.update(shutdown_s=time.monotonic()-started, returncode=process.returncode)
        if process.returncode != 0 or b'Node stopped successfully' not in output:
            raise RuntimeError('node did not report successful orderly shutdown')
        if mode in ('rpc-partial-http', 'rpc-full-admission'):
            marker = f'closing listener and {len(connections)} connections'.encode()
            if marker not in output or b'all connections joined' not in output:
                raise RuntimeError('shutdown did not observe and join every held connection')
        if mode in ('rpc-off', 'rpc-cli-off') and b'JSON-RPC server listening' in output:
            raise RuntimeError('RPC was enabled in a disabled control')
        if mode == 'rpc-off' and not args.existing_database:
            mempool = database/'mempool.dat'
            before = mempool.read_bytes()
            compact = subprocess.run([str(args.binary), '--network', 'regtest', '--config', str(config), '--compact-utxo'], cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
            (directory/'compact-offline.log').write_bytes(compact.stdout)
            if compact.returncode != 0 or b'UTXO compaction complete' not in compact.stdout:
                raise RuntimeError('offline compaction did not finish successfully')
            if b'Node started successfully' in compact.stdout or before != mempool.read_bytes():
                raise RuntimeError('maintenance started services or changed the saved mempool')
        result['passed'] = True
    except Exception as error:
        failed = True
        result.update(passed=False, error=str(error))
    finally:
        if previous_fd_limit is not None and process.poll() is None:
            try:
                resource.prlimit(process.pid, resource.RLIMIT_NOFILE, previous_fd_limit)
            except ProcessLookupError:
                pass  # The owned child exited between poll() and restoration.
        for connection in connections:
            connection.close()
        if process.poll() is None:
            process.kill()
            tail, _ = process.communicate()
            output.extend(tail)
        selector.close()
        (directory/'node.log').write_bytes(output)
        (directory/'result.json').write_text(json.dumps(result, indent=2))
        print(json.dumps(result), flush=True)
    if failed:
        # CTest/CI must retain the diagnostic itself, not just our return code
        # and a path on an ephemeral runner. The raw log is also kept on disk.
        print(output.decode('utf-8', errors='replace'), file=sys.stderr, flush=True)
        break  # Retain the first failure after cleanup; bound the CTest failure path.
raise SystemExit(1 if failed else 0)
