#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test-only TCP proxy: corrupt a header target, preserving BCH message framing/checksum.

Place between the WebSocket bridge and an isolated BCHN regtest peer. The final
header in the first nonempty headers batch receives a zero target. Earlier
headers retain their hashes/linkage and all transport checksums remain valid.
"""
import argparse
import asyncio
import hashlib
import struct


async def serve(listen, upstream):
    async def connection(reader, writer):
        remote_reader, remote_writer = await asyncio.open_connection('127.0.0.1', upstream)

        async def requests():
            while data := await reader.read(65536):
                remote_writer.write(data)
                await remote_writer.drain()

        async def responses():
            changed = False
            while True:
                heading = bytearray(await remote_reader.readexactly(24))
                size = struct.unpack_from('<I', heading, 16)[0]
                if size > 1024 * 1024:
                    raise ValueError('fixture response limit exceeded')
                payload = await remote_reader.readexactly(size)
                if not changed and heading[4:16].rstrip(b'\0') == b'headers' and size > 81:
                    payload = bytearray(payload)
                    # Serialized headers end in an 80-byte header plus a zero transaction count.
                    assert payload[-1] == 0
                    payload[-9:-5] = b'\0' * 4
                    heading[20:24] = hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]
                    changed = True
                    print('Injected zero target into final header; valid transport checksum', flush=True)
                writer.write(heading)
                writer.write(payload)
                await writer.drain()

        tasks = [asyncio.create_task(requests()), asyncio.create_task(responses())]
        try:
            await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
        finally:
            for task in tasks:
                task.cancel()
            await asyncio.gather(*tasks, return_exceptions=True)
            writer.close()
            remote_writer.close()
            await asyncio.gather(writer.wait_closed(), remote_writer.wait_closed(), return_exceptions=True)

    server = await asyncio.start_server(connection, '127.0.0.1', listen)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--listen', type=int, default=19455)
    parser.add_argument('--upstream', type=int, default=19454)
    args = parser.parse_args()
    asyncio.run(serve(args.listen, args.upstream))
