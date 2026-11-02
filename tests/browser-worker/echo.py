"""Feasibility fixture: byte echo only, not the BCH bridge."""
import asyncio
from websockets.asyncio.server import serve

async def echo(socket):
    async for message in socket:
        await socket.send(message)

async def main():
    async with serve(echo, '127.0.0.1', 18765, max_size=65536):
        await asyncio.Future()

asyncio.run(main())
