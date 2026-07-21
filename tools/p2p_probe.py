#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""External BCH P2P probe — validate a node from outside.

Reproduces Melroy van den Berg's Go probe: complete the version/verack
handshake, then send `mempool` + `getaddr` + `ping` and report every frame the
node sends back (or an immediate EOF). Use it to compare a Knuth node against
BCHN, and Knuth 0.83.0 (line 0) against master (line 1).

    python3 tools/p2p_probe.py <host> [port] [--testnet] [--secs N]

A well-behaved peer answers the probe with (in some order): version/verack,
pong (to our ping), addr/addrv2 (to getaddr), inv (to mempool, BIP-35), and may
proactively send sendheaders / sendcmpct / feefilter. An EOF right after verack
is the bug we are chasing.

Pure stdlib; no dependencies.
"""

import socket
import struct
import hashlib
import time
import sys

# Network magic (mainnet / testnet3). Sent byte-for-byte.
MAGIC = {
    'mainnet': bytes.fromhex('e3e1f3e8'),
    'testnet': bytes.fromhex('f4e5f3f4'),
}
DEFAULT_PORT = {'mainnet': 8333, 'testnet': 18333}
PROTOCOL_VERSION = 70015
USER_AGENT = b'/kth-probe:0.1/'


def dsha256(b):
    return hashlib.sha256(hashlib.sha256(b).digest()).digest()


def varint(n):
    if n < 0xfd:
        return struct.pack('<B', n)
    if n <= 0xffff:
        return b'\xfd' + struct.pack('<H', n)
    if n <= 0xffffffff:
        return b'\xfe' + struct.pack('<I', n)
    return b'\xff' + struct.pack('<Q', n)


def varstr(b):
    return varint(len(b)) + b


def frame(magic, command, payload):
    cmd = command.encode() + b'\x00' * (12 - len(command))
    return magic + cmd + struct.pack('<I', len(payload)) + dsha256(payload)[:4] + payload


def net_addr(services=0, ip='0.0.0.0', port=0):
    # 26-byte address (no timestamp), IPv4-mapped IPv6.
    ip6 = b'\x00' * 10 + b'\xff\xff' + socket.inet_aton(ip)
    return struct.pack('<Q', services) + ip6 + struct.pack('>H', port)


def version_payload(host, port):
    now = 1_700_000_000  # fixed timestamp (Date.now() unavailable is fine; nodes don't care)
    p = struct.pack('<i', PROTOCOL_VERSION)
    p += struct.pack('<Q', 0)               # services
    p += struct.pack('<q', now)             # timestamp
    p += net_addr(0, host, port)            # addr_recv
    p += net_addr(0, '0.0.0.0', 0)          # addr_from
    p += struct.pack('<Q', 0x1234567890abcdef)  # nonce
    p += varstr(USER_AGENT)
    p += struct.pack('<i', 0)               # start_height
    p += b'\x01'                            # relay
    return p


class Reader:
    """Frame reader over a socket, with a receive buffer."""
    def __init__(self, sock, magic):
        self.sock = sock
        self.magic = magic
        self.buf = b''

    def _fill(self, n):
        while len(self.buf) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError('EOF (node closed the connection)')
            self.buf += chunk

    def read_frame(self):
        self._fill(24)  # header
        magic, cmd, length = self.buf[:4], self.buf[4:16], struct.unpack('<I', self.buf[16:20])[0]
        if magic != self.magic:
            raise ValueError(f'bad magic {magic.hex()}')
        self._fill(24 + length)
        payload = self.buf[24:24 + length]
        self.buf = self.buf[24 + length:]
        command = cmd.rstrip(b'\x00').decode('ascii', 'replace')
        return command, payload


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = {a for a in sys.argv[1:] if a.startswith('--')}
    net = 'testnet' if '--testnet' in flags else 'mainnet'
    secs = 10.0
    for f in flags:
        if f.startswith('--secs='):
            secs = float(f.split('=', 1)[1])
    if not args:
        print(__doc__)
        return 2
    host = args[0]
    port = int(args[1]) if len(args) > 1 else DEFAULT_PORT[net]
    magic = MAGIC[net]

    print(f'=== probing {host}:{port} ({net}) ===')
    sock = socket.create_connection((host, port), timeout=10)
    sock.settimeout(secs)
    rd = Reader(sock, magic)

    def send(command, payload=b''):
        sock.sendall(frame(magic, command, payload))
        print(f'  -> {command} ({len(payload)} bytes)')

    got_version = got_verack = False
    try:
        send('version', version_payload(host, port))
        # Handshake: read until we have peer's version and verack.
        while not (got_version and got_verack):
            command, payload = rd.read_frame()
            print(f'  <- {command} ({len(payload)} bytes)')
            if command == 'version':
                got_version = True
                send('verack')
            elif command == 'verack':
                got_verack = True
            # tolerate sendaddrv2 / others during handshake

        print('*** HANDSHAKE COMPLETE — sending mempool + getaddr + ping ***')
        send('mempool')
        send('getaddr')
        nonce = struct.pack('<Q', 0xdeadbeefcafef00d)
        send('ping', nonce)

        # Report everything the node sends back until EOF or the time budget.
        deadline = time.monotonic() + secs
        counts = {}
        while time.monotonic() < deadline:
            try:
                command, payload = rd.read_frame()
            except socket.timeout:
                break
            counts[command] = counts.get(command, 0) + 1
            print(f'  <- {command} ({len(payload)} bytes)')
            if command == 'ping':
                send('pong', payload[:8])

        print('\n=== RESULT ===')
        if counts:
            print('post-handshake frames received: ' +
                  ', '.join(f'{k}x{v}' for k, v in counts.items()))
            for expected in ('pong', 'addr', 'addrv2', 'inv'):
                mark = 'yes' if expected in counts else 'NO'
                print(f'  responded to probe with {expected}: {mark}')
        else:
            print('NO post-handshake frames — node stayed silent (missing service handlers)')
        return 0

    except EOFError as e:
        stage = 'DURING handshake' if not (got_version and got_verack) else 'right AFTER handshake'
        print(f'\n=== RESULT ===\n{e} — {stage}. This is the "node drops the connection" bug.')
        return 1
    finally:
        sock.close()


if __name__ == '__main__':
    sys.exit(main())
