"""Loopback fixtures: byte-stream controls, and a scripted BCH regtest peer."""
import argparse
import asyncio
from contextlib import AsyncExitStack
from websockets.asyncio.server import serve
from websockets.exceptions import ConnectionClosed

async def echo(socket):
    async for message in socket:
        await socket.send(message)

async def stall(socket):
    await socket.wait_closed()

async def cap(socket):
    # Many small messages are a stream, not a reason to stop: 1000 single bytes
    # (far more messages than any frame count) stay within the byte bound...
    for _ in range(1000):
        await socket.send(b's')
    # ...then, once the client has read them all and says so, more than the
    # 1 MiB it admits unread, in frames of the bridge cap.
    await socket.send(b'm')
    await socket.recv()
    for _ in range(17):
        await socket.send(b'x' * 65536)
    await socket.wait_closed()

async def eof(socket):
    await socket.send(b'c' * 60000)
    await socket.send(b'c' * 40000)
    await socket.close()

async def abrupt(socket):
    await socket.send(b'c' * 60000)
    await socket.send(b'c' * 40000)
    socket.transport.abort()

# -----------------------------------------------------------------------------
# A scripted BCH regtest peer, speaking P2P over WebSocket as a bridge would
# relay it. It serves a chain it mines itself (regtest proof of work is cheap).
# -----------------------------------------------------------------------------

import hashlib
import struct

REGTEST_MAGIC = bytes.fromhex("dab5bffa")  # BCH regtest, 0xfabfb5da on the wire little-endian
GENESIS_TIME = 1296688602
REGTEST_BITS = 0x207FFFFF
REGTEST_TARGET = 0x7FFFFF << (8 * (0x20 - 3))


def dsha(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()


def header_bytes(version, prev, merkle, time, bits, nonce):
    return struct.pack("<i32s32sIII", version, prev, merkle, time, bits, nonce)


def regtest_genesis():
    merkle = bytes.fromhex("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b")[::-1]
    return header_bytes(1, bytes(32), merkle, GENESIS_TIME, REGTEST_BITS, 2)


def mine(prev_header, height, valid=True, branch=0):
    prev = dsha(prev_header)
    merkle = hashlib.sha256(struct.pack("<I", height) + (struct.pack("<I", branch) if branch else b"")).digest()
    nonce = 0
    while True:
        header = header_bytes(0x20000000, prev, merkle, GENESIS_TIME + 600 * height, REGTEST_BITS, nonce)
        if (int.from_bytes(dsha(header), "little") <= REGTEST_TARGET) == valid:
            return header
        nonce += 1


def chain(length, invalid_at=None):
    headers = [regtest_genesis()]
    for height in range(1, length + 1):
        headers.append(mine(headers[-1], height, valid=height != invalid_at))
    return headers


def extend(headers, length, branch=0, invalid_at=None):
    """The chain `headers` grown to `length`; with a branch number, a fork."""
    result = list(headers)
    for height in range(len(result), length + 1):
        result.append(mine(result[-1], height, valid=height != invalid_at, branch=branch))
    return result


def varint(value):
    if value < 0xFD:
        return bytes([value])
    if value <= 0xFFFF:
        return b"\xfd" + struct.pack("<H", value)
    return b"\xfe" + struct.pack("<I", value)


def read_varint(data, at):
    first = data[at]
    if first < 0xFD:
        return first, at + 1
    if first == 0xFD:
        return struct.unpack_from("<H", data, at + 1)[0], at + 3
    return struct.unpack_from("<I", data, at + 1)[0], at + 5


def message(command, payload):
    return (REGTEST_MAGIC + command.encode().ljust(12, b"\0") +
            struct.pack("<I", len(payload)) + dsha(payload)[:4] + payload)


def version_payload(height):
    address = struct.pack("<Q", 1) + bytes(10) + b"\xff\xff" + bytes([127, 0, 0, 1]) + struct.pack(">H", 18444)
    agent = b"/kth-ci-peer/"
    return (struct.pack("<iQq", 70016, 1, GENESIS_TIME) + address + address +
            struct.pack("<Q", 0x5EED) + varint(len(agent)) + agent + struct.pack("<i?", height, False))


class bch_session:
    """One client: framing over the WebSocket byte stream, then the script."""

    def __init__(self, socket, headers):
        self.socket = socket
        self.headers = headers
        self.buffer = b""

    async def send(self, command, payload=b""):
        # As a bridge relays a TCP stream: in WebSocket messages of at most the
        # 65536-byte frame cap the client admits.
        data = message(command, payload)
        for at in range(0, len(data), 65536):
            await self.socket.send(data[at:at + 65536])

    async def receive(self):
        while True:
            if len(self.buffer) >= 24:
                length = struct.unpack_from("<I", self.buffer, 16)[0]
                if len(self.buffer) >= 24 + length:
                    command = self.buffer[4:16].rstrip(b"\0").decode()
                    payload = self.buffer[24:24 + length]
                    self.buffer = self.buffer[24 + length:]
                    return command, payload
            self.buffer += await self.socket.recv()

    async def handshake(self):
        command, _ = await self.receive()
        assert command == "version", command
        await self.send("version", version_payload(len(self.headers) - 1))
        await self.send("verack")

    def headers_after(self, payload):
        count, at = read_varint(payload, 4)
        known = {dsha(header): height for height, header in enumerate(self.headers)}
        start = 0
        for index in range(count):
            hash_ = payload[at + 32 * index:at + 32 * index + 32]
            if hash_ in known:
                start = known[hash_] + 1
                break
        selected = self.headers[start:start + 2000]
        return varint(len(selected)) + b"".join(header + b"\0" for header in selected)

    async def serve_headers(self, until_empty=True, ping=None):
        """Answer getheaders until one is answered empty. Before the first
        answer, a ping that must be answered with its pong."""
        pinged = ping is None
        while True:
            command, payload = await self.receive()
            if command == "getheaders":
                if not pinged:
                    await self.send("ping", struct.pack("<Q", ping))
                    while True:
                        reply, nonce = await self.receive()
                        if reply == "pong":
                            assert struct.unpack("<Q", nonce)[0] == ping, "pong for another ping"
                            break
                    pinged = True
                answer = self.headers_after(payload)
                await self.send("headers", answer)
                if until_empty and answer == varint(0):
                    return


def quiet(handler):
    """A client that ends its session closes the connection: not a failure."""
    async def run(socket):
        try:
            await handler(socket)
        except ConnectionClosed:
            pass
    return run


@quiet
async def bch_sync(socket):
    # 2500 headers: more than one batch of 2000.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake()
    await session.serve_headers()
    await socket.wait_closed()


@quiet
async def bch_invalid(socket):
    session = bch_session(socket, BCH_INVALID)
    await session.handshake()
    await session.serve_headers(until_empty=False)


@quiet
async def bch_follow(socket):
    # The client catches up, answering a ping on the way; then a block is
    # announced on the same connection, and the client asks for it.
    session = bch_session(socket, BCH_CHAIN[:2001])
    await session.handshake()
    await session.serve_headers(ping=0xC0FFEE)
    session.headers = BCH_CHAIN[:2006]
    await session.send("inv", varint(1) + struct.pack("<I", 2) + dsha(session.headers[-1]))
    await session.serve_headers(until_empty=False)


@quiet
async def bch_oversize(socket):
    # A heading that declares a 1 MiB payload, which is never sent: a client
    # that bounds what it accepts ends the session at the heading, without
    # waiting for (or allocating) the payload.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake()
    while (await session.receive())[0] != "getheaders":
        pass
    heading = (REGTEST_MAGIC + b"headers".ljust(12, b"\0") + struct.pack("<I", 1 << 20) + bytes(4))
    await socket.send(heading)
    await socket.wait_closed()


@quiet
async def bch_reorg(socket):
    # The client catches up at 2500; then a heavier branch from 2495 to 2510
    # is announced on the same connection.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake()
    await session.serve_headers()
    session.headers = BCH_REORG
    await session.send("inv", varint(1) + struct.pack("<I", 2) + dsha(BCH_REORG[-1]))
    await session.serve_headers(until_empty=False)


RETRY_CONNECTIONS = 0


@quiet
async def bch_retry(socket):
    # First connection: the chain to 2500, then a heavier branch from 2495
    # whose header at 2505 has invalid proof of work. Next connection: the
    # branch only to 2504, every header of it already stored by the client.
    global RETRY_CONNECTIONS
    RETRY_CONNECTIONS += 1
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake()
    if RETRY_CONNECTIONS == 1:
        await session.serve_headers()
        session.headers = BCH_RETRY_INVALID
        await session.send("inv", varint(1) + struct.pack("<I", 2) + dsha(BCH_RETRY_INVALID[-1]))
    else:
        session.headers = BCH_RETRY_INVALID[:2505]
    await session.serve_headers(until_empty=False)


@quiet
async def bch_longer(socket):
    # The same chain, 100 headers longer.
    session = bch_session(socket, BCH_LONGER)
    await session.handshake()
    await session.serve_headers()
    await socket.wait_closed()


@quiet
async def bch_fork(socket):
    # A longer chain that leaves BCH_LONGER at height 2595, five deep.
    session = bch_session(socket, BCH_FORK)
    await session.handshake()
    await session.serve_headers()
    await socket.wait_closed()


BCH_CHAIN = chain(2500)
BCH_REORG = extend(BCH_CHAIN[:2496], 2510, branch=1)
BCH_RETRY_INVALID = extend(BCH_CHAIN[:2496], 2510, branch=2, invalid_at=2505)
BCH_LONGER = extend(BCH_CHAIN, 2600)
BCH_FORK = extend(BCH_LONGER[:2596], 2610, branch=1)
BCH_INVALID = chain(20, invalid_at=10)


async def main(scenario):
    controls = {'echo': (18765, echo), 'stall': (18767, stall),
                'cap': (18768, cap), 'eof': (18769, eof), 'abrupt': (18770, abrupt),
                'bch_sync': (18780, bch_sync), 'bch_invalid': (18781, bch_invalid),
                'bch_follow': (18782, bch_follow), 'bch_oversize': (18783, bch_oversize),
                'bch_reorg': (18787, bch_reorg), 'bch_retry': (18788, bch_retry),
                'bch_longer': (18785, bch_longer), 'bch_fork': (18786, bch_fork)}
    selected = controls if scenario == 'all' else {scenario: controls[scenario]}
    async with AsyncExitStack() as stack:
        for port, handler in selected.values():
            await stack.enter_async_context(serve(handler, '127.0.0.1', port, max_size=65536))
        await asyncio.Event().wait()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('scenario', choices=['all', 'echo', 'stall', 'cap', 'eof', 'abrupt',
                                             'bch_sync', 'bch_invalid', 'bch_follow', 'bch_oversize',
                                             'bch_reorg', 'bch_retry', 'bch_longer', 'bch_fork'],
                        default='all', nargs='?')
    asyncio.run(main(parser.parse_args().scenario))
