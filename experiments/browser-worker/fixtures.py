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
import time

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


def version_payload(height, agent=b"/kth-ci-peer/"):
    address = struct.pack("<Q", 1) + bytes(10) + b"\xff\xff" + bytes([127, 0, 0, 1]) + struct.pack(">H", 18444)
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

    async def handshake(self, agent=b"/kth-ci-peer/"):
        command, _ = await self.receive()
        assert command == "version", command
        await self.send("version", version_payload(len(self.headers) - 1, agent))
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


def wsaddr_payload(request_id, urls):
    """A discovery response: each endpoint as transport, expiry, port (all
    little-endian), host and path."""
    out = bytes([1]) + struct.pack("<Q", request_id) + bytes([len(urls)])
    for scheme, host, port, path in urls:
        out += bytes([2 if scheme == "wss" else 1]) + struct.pack("<IH", int(time.time()) + 600, port)
        out += bytes([len(host)]) + host.encode() + struct.pack("<H", len(path)) + path.encode()
    return out


@quiet
async def bch_discovery(socket):
    # Negotiates discovery and serves the 2500 headers; asked for endpoints,
    # it advertises the longer chain's peer (a loopback endpoint: the client
    # must allow private ones).
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake(agent=b"/kth-ci-peer/wsdisc:1/")
    while True:
        command, payload = await session.receive()
        if command == "getheaders":
            await session.send("headers", session.headers_after(payload))
        elif command == "getwsaddr":
            request_id = struct.unpack_from("<Q", payload, 2)[0]
            await session.send("wsaddr", wsaddr_payload(request_id, [("ws", "127.0.0.1", 18785, "/")]))


@quiet
async def bch_flood(socket):
    # Headers past the client's capacity: the ones that fit are valid, the
    # first past it has invalid proof of work.
    session = bch_session(socket, BCH_FLOOD)
    await session.handshake()
    await session.serve_headers(until_empty=False)


@quiet
async def bch_short(socket):
    # 50 valid headers: within a small client's capacity.
    session = bch_session(socket, BCH_CHAIN[:51])
    await session.handshake()
    await session.serve_headers(until_empty=False)


@quiet
async def bch_discovery_close(socket):
    # Asked for discovery; closes after its last headers, without answering.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake(agent=b"/kth-ci-peer/wsdisc:1/")
    while True:
        command, payload = await session.receive()
        if command == "getheaders":
            answer = session.headers_after(payload)
            await session.send("headers", answer)
            if answer == varint(0):
                await asyncio.sleep(0.3)
                return
        elif command == "ping":
            await session.send("pong", payload)


@quiet
async def bch_slow(socket):
    # 200 valid headers, 25 per answer, the first answer late: by then another
    # peer has delivered them all, and every answer brings known headers.
    session = bch_session(socket, BCH_CHAIN[:201])
    await session.handshake()
    first = True
    while True:
        command, payload = await session.receive()
        if command == "getheaders":
            if first:
                await asyncio.sleep(2)
                first = False
            answer = session.headers_after(payload)
            count, start = read_varint(answer, 0)
            count = min(count, 25)
            # Each record is the 80-byte header and an empty transaction count.
            await session.send("headers", varint(count) + answer[start:start + 81 * count])
        elif command == "ping":
            await session.send("pong", payload)


@quiet
async def bch_alternate(socket):
    # Answers every request with one of two early headers, by turns: they are
    # valid, and soon known, and never advance anything.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake()
    turn = 0
    while True:
        command, payload = await session.receive()
        if command == "getheaders":
            await session.send("headers", varint(1) + BCH_CHAIN[1 + turn % 2] + b"\0")
            turn += 1
        elif command == "ping":
            await session.send("pong", payload)


@quiet
async def bch_discovery_late(socket):
    # As bch_discovery, but its discovery answer comes after its last headers.
    session = bch_session(socket, BCH_CHAIN)
    await session.handshake(agent=b"/kth-ci-peer/wsdisc:1/")
    pending = None
    while True:
        command, payload = await session.receive()
        if command == "getwsaddr":
            pending = struct.unpack_from("<Q", payload, 2)[0]
        elif command == "getheaders":
            answer = session.headers_after(payload)
            await session.send("headers", answer)
            if answer == varint(0) and pending is not None:
                await asyncio.sleep(0.3)
                await session.send("wsaddr", wsaddr_payload(pending, [("ws", "127.0.0.1", 18785, "/")]))
                pending = None


def coinbase(height):
    """A coinbase that pays the subsidy to OP_TRUE, with the height in its input
    script (BIP34) and padding to the 100-byte minimum transaction size."""
    script = bytes([4]) + struct.pack("<I", height) + bytes([40]) + b"kth browser node control".ljust(40, b".")
    return (struct.pack("<i", 2) + varint(1) + bytes(32) + struct.pack("<I", 0xFFFFFFFF) +
            varint(len(script)) + script + struct.pack("<I", 0xFFFFFFFF) +
            varint(1) + struct.pack("<q", 50 * 100_000_000) + varint(1) + b"\x51" + struct.pack("<I", 0))


def blocks(length):
    """A regtest chain of full blocks, each with only its coinbase."""
    result = [(regtest_genesis(), None)]
    for height in range(1, length + 1):
        transaction = coinbase(height)
        merkle = dsha(transaction)
        prev = dsha(result[-1][0])
        nonce = 0
        while True:
            header = header_bytes(0x20000000, prev, merkle, GENESIS_TIME + 600 * height, REGTEST_BITS, nonce)
            if int.from_bytes(dsha(header), "little") <= REGTEST_TARGET:
                break
            nonce += 1
        result.append((header, header + varint(1) + transaction))
    return result


# A structurally valid transaction to announce (its input spends nothing real).
LOOSE_TX = (struct.pack("<i", 2) + varint(1) + dsha(b"kth") + struct.pack("<I", 0) + varint(60) + bytes(60) +
            struct.pack("<I", 0xFFFFFFFF) + varint(1) + struct.pack("<q", 1000) + varint(1) + b"\x51" +
            struct.pack("<I", 0))


def loose_tx(index, outputs=1):
    """A structurally valid transaction (its input spends nothing real)."""
    return (struct.pack("<i", 2) + varint(1) + dsha(b"kth") + struct.pack("<I", index) + varint(60) + bytes(60) +
            struct.pack("<I", 0xFFFFFFFF) + varint(outputs) + (struct.pack("<q", 1000) + varint(1) + b"\x51") * outputs +
            struct.pack("<I", 0))


async def serve_content(session, chain_blocks, corrupt_at=None, announce=None, missing_at=None,
                        transactions=(), answered=None, answer_delay=0, disown=()):
    """Headers and the blocks asked for (one with a body that does not match
    its header, at `corrupt_at`; none at `missing_at`); once caught up, the
    announced transactions, of which only those in `answered` (all, when
    None) are served, `answer_delay` seconds after asked for. A second after
    caught up, a notfound nobody asked for, for the transactions in `disown`."""
    if announce:
        transactions = (announce,)
    served = {dsha(tx): tx for tx in transactions if answered is None or tx in answered}
    by_hash = {}
    for height, (header, block) in enumerate(chain_blocks[1:], 1):
        if height == corrupt_at:
            block = block[:-5] + bytes([block[-5] ^ 1]) + block[-4:]
        if height != missing_at:
            by_hash[dsha(header)] = block
    announced = False
    while True:
        command, payload = await session.receive()
        if command == "getheaders":
            answer = session.headers_after(payload)
            await session.send("headers", answer)
            if disown and answer == varint(0):
                await asyncio.sleep(1)
                await session.send("notfound", varint(len(disown)) +
                                   b"".join(struct.pack("<I", 1) + dsha(tx) for tx in disown))
                disown = ()
            if transactions and not announced and answer == varint(0):
                announced = True
                await session.send("inv", varint(len(transactions)) +
                                   b"".join(struct.pack("<I", 1) + dsha(tx) for tx in transactions))
        elif command == "getdata":
            count, at = read_varint(payload, 0)
            missing = []
            for index in range(count):
                kind = struct.unpack_from("<I", payload, at + 36 * index)[0]
                hash_ = payload[at + 36 * index + 4:at + 36 * index + 36]
                if kind == 2 and hash_ in by_hash:
                    await session.send("block", by_hash[hash_])
                elif kind == 1 and hash_ in served:
                    await asyncio.sleep(answer_delay)
                    await session.send("tx", served[hash_])
                elif kind == 1 and any(dsha(tx) == hash_ for tx in transactions):
                    pass  # asked for, never answered
                else:
                    missing.append(payload[at + 36 * index:at + 36 * index + 36])
            if missing:
                await session.send("notfound", varint(len(missing)) + b"".join(missing))
        elif command == "ping":
            await session.send("pong", payload)


@quiet
async def bch_content(socket):
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS, announce=LOOSE_TX)


@quiet
async def bch_bad_body(socket):
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS[:21]])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS[:21], corrupt_at=20)


@quiet
async def bch_tx_silent(socket):
    # 17 transactions announced; the first 16 asked for are never answered.
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS[:21]])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS[:21], transactions=SILENT_TXS, answered=SILENT_TXS[16:])


@quiet
async def bch_big_tx(socket):
    # One announced transaction of about 300 KiB.
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS[:21]])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS[:21], transactions=(BIG_TX,))


@quiet
async def bch_missing_block(socket):
    # The tip's body is not available; the others are.
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS, missing_at=150)


@quiet
async def bch_tx_late(socket):
    # One announced transaction, served late.
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS[:21]])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS[:21], transactions=(LATE_TX,), answer_delay=2.5)


@quiet
async def bch_tx_disown(socket):
    # The same chain; once caught up, a notfound for that transaction.
    session = bch_session(socket, [header for header, _ in CONTENT_BLOCKS[:21]])
    await session.handshake()
    await serve_content(session, CONTENT_BLOCKS[:21], disown=(LATE_TX,))


LATE_TX = loose_tx(77)
SILENT_TXS = tuple(loose_tx(index) for index in range(17))
BIG_TX = loose_tx(99, outputs=30000)
BCH_CHAIN = chain(2500)
CONTENT_BLOCKS = blocks(150)
BCH_FLOOD = chain(2100, invalid_at=100)
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
                'bch_discovery': (18789, bch_discovery), 'bch_flood': (18803, bch_flood),
                'bch_short': (18804, bch_short), 'bch_alternate': (18805, bch_alternate),
                'bch_discovery_late': (18806, bch_discovery_late), 'bch_slow': (18810, bch_slow),
                'bch_discovery_close': (18813, bch_discovery_close),
                'bch_content': (18800, bch_content), 'bch_bad_body': (18801, bch_bad_body),
                'bch_tx_silent': (18807, bch_tx_silent), 'bch_big_tx': (18808, bch_big_tx),
                'bch_missing_block': (18809, bch_missing_block),
                'bch_tx_late': (18811, bch_tx_late), 'bch_tx_disown': (18812, bch_tx_disown),
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
                                             'bch_reorg', 'bch_retry', 'bch_longer', 'bch_fork',
                                             'bch_discovery', 'bch_flood', 'bch_short', 'bch_alternate',
                                             'bch_discovery_late', 'bch_slow', 'bch_discovery_close', 'bch_content', 'bch_bad_body',
                                             'bch_tx_silent', 'bch_big_tx', 'bch_missing_block',
                                             'bch_tx_late', 'bch_tx_disown'],
                        default='all', nargs='?')
    asyncio.run(main(parser.parse_args().scenario))
