#!/usr/bin/env python3
"""Real Chromium interoperability control for the native P2P WS transport.

Requires Playwright + Chromium. TLS here is only a test reverse proxy; it doesn't
implement BCH or WebSocket. Node fixtures run the real KTH session/dispatcher.
"""
import argparse
import asyncio
from contextlib import AsyncExitStack
import json
import hashlib
import struct
import time
from pathlib import Path
import ssl
import tempfile

from playwright.async_api import async_playwright

ROOT = Path(__file__).resolve().parent


async def relay_tls(reader, writer, target):
    upstream_writer = None
    tasks = []
    try:
        upstream_reader, upstream_writer = await asyncio.open_connection('127.0.0.1', target)

        async def copy(src, dst):
            while data := await src.read(65536):
                dst.write(data)
                await dst.drain()
        tasks = [asyncio.create_task(copy(reader, upstream_writer)),
                 asyncio.create_task(copy(upstream_reader, writer))]
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    except ConnectionError:
        pass
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        writer.close()
        try:
            await writer.wait_closed()
        except ConnectionError:
            pass
        if upstream_writer:
            upstream_writer.close()
            try:
                await upstream_writer.wait_closed()
            except ConnectionError:
                pass


async def page_server(reader, writer):
    try:
        request = await reader.readuntil(b'\r\n\r\n')
        path = request.split(b' ')[1]
        body = ((ROOT / 'peer.js').read_bytes() if path == b'/peer.js'
                else b'<!doctype html><meta charset="utf-8"><title>Native P2P control</title><script src="/peer.js"></script>')
        content = b'application/javascript' if path == b'/peer.js' else b'text/html'
        writer.write(b'HTTP/1.1 200 OK\r\nContent-Type: ' + content +
                     b'\r\nContent-Length: ' + str(len(body)).encode() + b'\r\nConnection: close\r\n\r\n' + body)
        await writer.drain()
    except (ConnectionError, asyncio.IncompleteReadError):
        pass
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except ConnectionError:
            pass


async def legacy_tcp_peer(port):
    magic = bytes.fromhex('dab5bffa')
    commands = []
    digest = lambda payload: hashlib.sha256(hashlib.sha256(payload).digest()).digest()
    def packet(command, payload=b''):
        return magic + command.encode().ljust(12, b'\x00') + struct.pack('<I', len(payload)) + digest(payload)[:4] + payload
    reader, writer = await asyncio.open_connection('127.0.0.1', port)
    async def receive():
        while True:
            header = await reader.readexactly(24)
            assert header[:4] == magic
            size = struct.unpack_from('<I', header, 16)[0]
            assert size <= 4 * 1024 * 1024
            payload = await reader.readexactly(size)
            assert digest(payload)[:4] == header[20:24]
            command = header[4:16].rstrip(b'\x00').decode()
            commands.append(command)
            assert command not in ('getwsaddr', 'wsaddr'), 'Unnegotiated discovery reached legacy TCP peer'
            if command == 'ping':
                writer.write(packet('pong', payload))
                await writer.drain()
            else:
                return command, payload
    try:
        async with asyncio.timeout(10):
            agent = b'/LegacyNativeProbe:1/'
            version = struct.pack('<iQq', 70016, 0, int(time.time())) + bytes(52) + struct.pack('<Q', 1234512345)
            version += bytes([len(agent)]) + agent + struct.pack('<iB', 0, 0)
            writer.write(packet('version', version))
            await writer.drain()
            got_version = got_verack = False
            while not (got_version and got_verack):
                command, _ = await receive()
                if command == 'version':
                    got_version = True
                    writer.write(packet('verack'))
                    await writer.drain()
                if command == 'verack':
                    got_verack = True
            genesis = bytes.fromhex('0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206')[::-1]
            nonce = struct.pack('<Q', 0x1234123412341234)
            writer.write(packet('ping', nonce) + packet('getheaders', struct.pack('<iB', 70016, 1) + genesis + bytes(32)))
            await writer.drain()
            pong = headers = False
            while not (pong and headers):
                command, payload = await receive()
                if command == 'pong':
                    assert payload == nonce
                    pong = True
                if command == 'headers':
                    assert len(payload) == 244 and payload[0] == 3
                    headers = True
            return {'headers': 3, 'commands': commands}
    finally:
        writer.close()
        try:
            await writer.wait_closed()
        except ConnectionError:
            pass


async def main(args):
    async with AsyncExitStack() as stack:
        temp = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='kth-ws-browser-')))
        cert, key = temp / 'cert.pem', temp / 'key.pem'
        process = await asyncio.create_subprocess_exec('openssl', 'req', '-x509', '-newkey', 'rsa:2048',
            '-nodes', '-keyout', str(key), '-out', str(cert), '-days', '1', '-subj', '/CN=localhost',
            stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
        if await process.wait():
            raise RuntimeError('TLS fixture certificate creation failed')
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(cert, key)
        connections = set()
        connection_errors = []
        def tracked(handler):
            def accept(reader, writer):
                async def invoke():
                    try:
                        await handler(reader, writer)
                    except Exception as error:
                        connection_errors.append(error)
                    finally:
                        connections.discard(asyncio.current_task())
                connections.add(asyncio.create_task(invoke()))
            return accept
        async def drain_connections():
            if connections:
                await asyncio.wait_for(asyncio.gather(*list(connections)), 10)
            if connection_errors:
                raise RuntimeError(f'Test server task failed: {connection_errors}')
        # Registered first: listeners stop before this join runs on stack exit.
        stack.push_async_callback(drain_connections)
        http = await stack.enter_async_context(await asyncio.start_server(tracked(page_server), '127.0.0.1', 0))
        https = await stack.enter_async_context(await asyncio.start_server(tracked(page_server), '127.0.0.1', 0, ssl=tls))
        origin = f'http://127.0.0.1:{http.sockets[0].getsockname()[1]}'
        secure_origin = f'https://127.0.0.1:{https.sockets[0].getsockname()[1]}'
        proxy = await stack.enter_async_context(await asyncio.start_server(
            tracked(lambda r, w: relay_tls(r, w, args.ws_port + 1)), '127.0.0.1', 0, ssl=tls))
        wss_port = proxy.sockets[0].getsockname()[1]
        first = f'ws://127.0.0.1:{args.ws_port}/p2p'
        second = f'wss://127.0.0.1:{wss_port}/p2p'

        raw_clients = []
        shutdown_times = []
        async def close_raw_clients():
            for reader, writer in raw_clients:
                # Fixtures must have closed the transport before this cleanup.
                if await asyncio.wait_for(reader.read(), 1) != b'':
                    raise RuntimeError('Unexpected partial HTTP response during shutdown')
                writer.close()
                await writer.wait_closed()
        stack.push_async_callback(close_raw_clients)
        async def slow_upgrade():
            reader, writer = await asyncio.open_connection('127.0.0.1', args.ws_port)
            writer.write(b'GET /p2p HTTP/1.1\r\n')
            await writer.drain()
            raw_clients.append((reader, writer))
        async def fixture(tcp, ws, *endpoints, learn=False):
            process = await asyncio.create_subprocess_exec(str(args.fixture), str(tcp), str(ws),
                origin + ',' + secure_origin, *endpoints, stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT)
            ready = asyncio.Event()
            learned = asyncio.Event()
            output = []

            async def collect():
                while line := await process.stdout.readline():
                    text = line.decode(errors='replace')
                    output.append(text)
                    if 'WebSocket P2P listening' in text:
                        ready.set()
                    if '[ws_discovery] Accepted endpoint response' in text:
                        learned.set()
            collector = asyncio.create_task(collect())

            async def close():
                started_closing = asyncio.get_running_loop().time()
                if process.returncode is None:
                    process.terminate()
                    try:
                        await asyncio.wait_for(process.wait(), 8)
                    except TimeoutError:
                        process.kill()
                        await process.wait()
                        raise RuntimeError('Fixture shutdown stalled: ' + ''.join(output[-20:]))
                await collector
                shutdown_times.append(asyncio.get_running_loop().time() - started_closing)
                if process.returncode != 0:
                    raise RuntimeError(f'Fixture exited {process.returncode}: ' + ''.join(output[-20:]))
            stack.push_async_callback(close)
            try:
                await asyncio.wait_for(ready.wait(), 30)
                if learn:
                    await asyncio.wait_for(learned.wait(), 15)
            except TimeoutError:
                raise RuntimeError('Fixture did not start: ' + ''.join(output[-20:]))
            return process

        # Node one discovers node two's WSS endpoint over their ordinary TCP link.
        await fixture(args.tcp_port + 1, args.ws_port + 1, second)
        await fixture(args.tcp_port, args.ws_port, first, f'tcp://127.0.0.1:{args.tcp_port+1}', learn=True)
        playwright = await stack.enter_async_context(async_playwright())
        browser = await playwright.chromium.launch(headless=True)
        stack.push_async_callback(browser.close)
        context = await browser.new_context(ignore_https_errors=True)
        page = await context.new_page()
        await page.goto(origin)
        await slow_upgrade()
        result = await asyncio.wait_for(page.evaluate('''async ({first, second}) => {
            const peer = await new Peer(first).open();
            await peer.ping();
            const headers = await peer.headers();
            await peer.serveHeaders();
            const endpoints = await peer.endpoints();
            if (!endpoints.includes(first) || !endpoints.includes(second)) throw new Error('Did not discover both WS and TCP-relayed WSS destinations: ' + endpoints);
            if (peer.remoteAgent.includes('wsdisc:')) throw new Error('Capability leaked into user agent');
            if (!peer.remoteDiscovery) throw new Error('No discovery announcement');
            window.activePeer = peer;
            return {remoteAgent:peer.remoteAgent, headers, browserServedHeaders:true, endpoints, commands:peer.commands};
        }''', {'first': first, 'second': second}), 5)
        # Keep the SAME session across the old experiment's 15-second reconnect period.
        await page.evaluate('''async () => {
            await new Promise(resolve => setTimeout(resolve, 16000));
            await activePeer.ping();
            await activePeer.headers();
            await activePeer.close();
        }''')
        secure_page = await context.new_page()
        await secure_page.goto(secure_origin)
        secure = await secure_page.evaluate('''async url => {
            const peer = await new Peer(url).open();
            await peer.ping();
            const headers = await peer.headers();
            const endpoints = await peer.endpoints(2);
            if (!endpoints.length || endpoints.some(url => !url.startsWith('wss://'))) throw new Error('Transport filter failed');
            await peer.close();
            return {headers, endpoints, remoteAgent:peer.remoteAgent};
        }''', second)
        legacy = await page.evaluate('''async url => {
            const peer = await new Peer(url, false).open();
            await peer.ping();
            await peer.headers();
            await peer.close();
            if (peer.commands.some(c => c === 'wsaddr' || c === 'getwsaddr')) throw new Error('Unnegotiated discovery traffic');
            return {commands:peer.commands};
        }''', first)
        for _ in range(6):
            await page.evaluate('''async url => { const peer = await new Peer(url).open(); await peer.ping(); await peer.close(); }''', first)
        failures = await page.evaluate('''async first => {
            for (const kind of ['text', 'oversize', 'unnegotiated', 'rate']) {
                const peer = await new Peer(first, kind !== 'unnegotiated').open();
                const closed = new Promise((resolve, reject) => {
                    const timer = setTimeout(() => reject(new Error('Rejected connection did not close: ' + kind)), 10000);
                    peer.ws.addEventListener('close', event => { clearTimeout(timer); resolve(event.code); }, {once:true});
                });
                if (kind === 'text') peer.ws.send('not binary');
                if (kind === 'oversize') peer.ws.send(new Uint8Array(65537));
                if (kind === 'unnegotiated') await peer.send('getwsaddr', testUtils.join(new Uint8Array([1, 3]), testUtils.u64(7)));
                if (kind === 'rate') { await peer.endpoints(); await peer.send('getwsaddr', testUtils.join(new Uint8Array([1, 3]), testUtils.u64(8))); }
                const code = await closed;
                if (kind === 'text' && code !== 1003) throw new Error('Text close code: ' + code);
                if (kind === 'oversize' && code !== 1009) throw new Error('Oversize close code: ' + code);
            }
            let refused = false;
            try { await new Peer(first + '-wrong').open(); } catch { refused = true; }
            if (!refused) throw new Error('Wrong path accepted');
            return {text:true, oversize:true, unnegotiated:true, rate:true, path:true};
        }''', first)
        denied_page = await context.new_page()
        await denied_page.goto(origin.replace('127.0.0.1', 'localhost'))
        denied = await denied_page.evaluate('''async url => {
            try { await new Peer(url).open(); return false; } catch { return true; }
        }''', first)
        if not denied:
            raise RuntimeError('Unapproved browser Origin admitted')
        tcp_legacy = await legacy_tcp_peer(args.tcp_port)
        await slow_upgrade()  # Remains open while the native listener stops.
        report = {'slow_http_did_not_block': True, 'fixture_shutdown_seconds': shutdown_times, 'rejections': failures, 'origin_rejected': denied, 'legacy_tcp': tcp_legacy, 'persistent_session_seconds': 16, 'quick_normal_reconnects': 6, 'ws': result, 'wss_from_https': secure, 'legacy': legacy}
    # Emit success only after every process, listener and test task was joined.
    print(json.dumps(report, indent=2))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--tcp-port', type=int, default=19810)
    parser.add_argument('--ws-port', type=int, default=19820)
    parser.add_argument('--output', type=Path)
    asyncio.run(main(parser.parse_args()))
