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
    for _ in range(65):
        await socket.send(b'x')
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
