# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""Small deterministic regtest P2P peers; no RPC, bridge or external chain."""
import hashlib
import struct

MAGIC = bytes.fromhex('dab5bffa')
GENESIS = bytes.fromhex(
    '01000000' + '00' * 32 +
    '3ba3edfd7a7b12b27ac72c3e67768f617fc81bc3888a51323a9fb8aa4b1e5e4a' +
    'dae5494dffff7f2002000000')


def digest(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def mine(parent, height, branch=0):
    prefix = (struct.pack('<I', 4) + digest(parent) + digest(bytes([height, branch])) +
              struct.pack('<II', 1296688602 + height * 600 + branch, 0x207fffff))
    for nonce in range(10000):
        header = prefix + struct.pack('<I', nonce)
        if int.from_bytes(digest(header), 'little') <= 0x7fffff << (8 * 29):
            return header
    raise AssertionError('regtest mining exhausted')


A = [GENESIS]
for height in range(1, 4):
    A.append(mine(A[-1], height))
B = A[:2]
for height in range(2, 6):
    B.append(mine(B[-1], height, 1))


def packet(command, payload=b''):
    return MAGIC + command.encode().ljust(12, b'\0') + struct.pack('<I', len(payload)) + digest(payload)[:4] + payload


def headers(chain):
    assert len(chain) < 253
    return packet('headers', bytes([len(chain)]) + b''.join(h + b'\0' for h in chain))


async def messages(socket):
    buffered = bytearray()
    async for frame in socket:
        buffered.extend(frame)
        while len(buffered) >= 24:
            size = int.from_bytes(buffered[16:20], 'little')
            assert buffered[:4] == MAGIC and size <= 65536
            if len(buffered) < 24 + size:
                break
            command = bytes(buffered[4:16]).rstrip(b'\0')
            payload = bytes(buffered[24:24 + size])
            assert buffered[20:24] == digest(payload)[:4]
            del buffered[:24 + size]
            yield command, payload


async def session(socket, busy=False):
    # WS frames are only a transport: accumulate the ordinary BCH byte stream.
    stream = messages(socket)
    requests = 0
    async for command, payload in stream:
        if command == b'version':
            address = b'\0' * 26
            agent = b'/KTH-regtest-fixture/'
            version = (struct.pack('<iQq', 70016, 0, 1700000000) + address * 2 +
                       struct.pack('<Q', 123456789) + bytes([len(agent)]) + agent +
                       struct.pack('<iB', 5, 0))
            await socket.send(packet('version', version) + packet('verack'))
        elif command == b'getheaders':
            requests += 1
            if busy and requests == 1:
                # Valid unrelated traffic must not make this peer malicious.
                for nonce in range(130):
                    await socket.send(packet('ping', struct.pack('<Q', nonce)))
                    # Wait for its pong so this tests the message-count policy,
                    # not unrelated overflow of the peer's bounded read queue.
                    async for reply, data in stream:
                        if reply == b'pong' and data == struct.pack('<Q', nonce):
                            break
                    else:
                        return
            if requests == 1:
                await socket.send(headers(A[1:]))
            elif requests == 2:
                await socket.send(headers(A[1:]))  # Duplicate already accepted batch.
            elif requests == 3:
                await socket.send(headers(B[2:]))  # Heavier sibling branch.
            else:
                await socket.send(headers([]))


async def reorg(socket):
    await session(socket)


async def busy(socket):
    await session(socket, busy=True)
