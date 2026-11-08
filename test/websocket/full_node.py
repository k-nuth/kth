import argparse
import asyncio
import json
import tempfile
from pathlib import Path
from browser import page_server
from playwright.async_api import async_playwright

async def main(args):
    with tempfile.TemporaryDirectory(prefix='kth-fullnode-ws-') as temporary:
        root = Path(temporary)
        async with await asyncio.start_server(page_server, '127.0.0.1', 0) as http:
            origin = f'http://127.0.0.1:{http.sockets[0].getsockname()[1]}'
            process = await asyncio.create_subprocess_exec(str(args.node.resolve()),
                '--network', 'regtest', '--init_run', '--display', 'log',
                '--db.directory', str(root/'chain'), '--net.inbound_port', '0',
                '--net.outbound_connections', '0', '--net.host_pool_capacity', '0',
                '--net.ws_enabled=true', '--net.ws_bind', '127.0.0.1', '--net.ws_port', str(args.ws_port),
                '--net.ws_origins', origin, '--rpc.enabled=false',
                cwd=root, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
            ready = asyncio.Event()
            lines = []
            async def collect():
                while line := await process.stdout.readline():
                    line = line.decode(errors='replace')
                    lines.append(line)
                    if 'WebSocket P2P listening' in line:
                        ready.set()
            collector = asyncio.create_task(collect())
            try:
                await asyncio.wait_for(ready.wait(), 30)
                async with async_playwright() as playwright:
                    browser = await playwright.chromium.launch(headless=True)
                    try:
                        page = await browser.new_page()
                        await page.goto(origin)
                        result = await page.evaluate('''async url => {
                            const peer = await new Peer(url, false).open();
                            await peer.ping();
                            const genesis = Uint8Array.from('0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206'.match(/../g), b=>parseInt(b,16)).reverse();
                            await peer.send('getheaders', testUtils.join(testUtils.u32(70016), new Uint8Array([0]), genesis));
                            const answer = await peer.wait('headers');
                            if (answer.length !== 82 || answer[0] !== 1) throw new Error('Full node did not serve genesis header');
                            await peer.close();
                            return {genesisHeaders:1, peer:peer.remoteAgent};
                        }''', f'ws://127.0.0.1:{args.ws_port}/p2p')
                    finally:
                        await browser.close()
            finally:
                if process.returncode is None:
                    process.terminate()
                try:
                    await asyncio.wait_for(process.wait(), 15)
                except TimeoutError:
                    process.kill()
                    await process.wait()
                    raise
                await collector
                if args.log:
                    args.log.write_text(''.join(lines))
                if process.returncode:
                    raise RuntimeError(f'Full node exited {process.returncode}: '+''.join(lines[-20:]))
            print(json.dumps(result))
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Chromium smoke control against the production full node executable')
    parser.add_argument('--node', type=Path, required=True)
    parser.add_argument('--ws-port', type=int, default=19930)
    parser.add_argument('--log', type=Path)
    asyncio.run(main(parser.parse_args()))
