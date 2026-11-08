"""Loopback byte-stream controls. These fixtures are not BCH peers."""
import argparse
import asyncio
from contextlib import AsyncExitStack
from websockets.asyncio.server import serve

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

async def main(scenario):
    controls = {'echo': (18765, echo), 'stall': (18767, stall),
                'cap': (18768, cap), 'eof': (18769, eof), 'abrupt': (18770, abrupt)}
    selected = controls if scenario == 'all' else {scenario: controls[scenario]}
    async with AsyncExitStack() as stack:
        for port, handler in selected.values():
            await stack.enter_async_context(serve(handler, '127.0.0.1', port, max_size=65536))
        await asyncio.Event().wait()

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('scenario', choices=['all', 'echo', 'stall', 'cap', 'eof', 'abrupt'], default='all', nargs='?')
    asyncio.run(main(parser.parse_args().scenario))
