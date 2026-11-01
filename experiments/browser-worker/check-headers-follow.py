#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying LICENSE file.
"""Require one persistent WS session, optional tip advancement, and joined Stop."""
import argparse
import asyncio
import json
import hashlib
from pathlib import Path
from playwright.async_api import async_playwright


async def main(args):
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch(headless=True)
        page = await browser.new_page()
        sockets, errors = [], []
        refreshed = asyncio.Event()
        def opened(socket):
            sockets.append(socket.url)
            def sent(payload):
                if isinstance(payload, bytes) and payload[4:16].rstrip(b'\0') == b'getheaders':
                    refreshed.set()
            socket.on('framesent', sent)
        page.on('websocket', opened)
        page.on('pageerror', lambda error: errors.append(str(error)))
        await page.goto(args.url)
        await page.evaluate('''config => {
            window.results = [];
            window.control = new Worker('headers-api-follow-worker.js');
            control.onmessage = event => results.push(event.data);
            control.onerror = event => results.push({type:'failure', message:event.message});
            control.postMessage(config);
        }''', {'method': args.method, 'bridge': args.bridge, 'network': args.network})
        try:
            await page.wait_for_function(
                "results.some(x => x.status === 'caught_up' || x.type === 'failure')", timeout=180000)
            await page.wait_for_timeout(args.hold_seconds * 1000)
            events = await page.evaluate('results')
            assert not errors, errors
            assert not any(x['type'] in ('terminal', 'failure') for x in events), events[-5:]
            first = next(x['height'] for x in events if x.get('status') == 'caught_up')
            print(json.dumps({'caught_up': first}), flush=True)
            if args.headers:
                await page.evaluate("control.postMessage({type:'headers'})")
                await page.wait_for_function("results.some(x => x.type === 'headers')", timeout=5000)
                sample = await page.evaluate("results.find(x => x.type === 'headers')")
                assert sample['absent'] == sample['absentHash'] == '', sample
                for header in sample['headers']:
                    raw = bytes.fromhex(header['hex'])
                    assert len(raw) == 80, header
                    digest = hashlib.sha256(hashlib.sha256(raw).digest()).digest()[::-1].hex()
                    assert digest == header['hash'], header
                if first > 0:
                    assert bytes.fromhex(sample['headers'][0]['hex'])[4:36][::-1].hex() == sample['headers'][1]['hash']
            if args.refresh:
                refreshed.clear()
                await page.evaluate("control.postMessage({type:'resume'})")
                await asyncio.wait_for(refreshed.wait(), timeout=5)
            if args.new_header:
                await page.wait_for_function('height => results.some(x => x.height > height)',
                                             arg=first, timeout=180000)
            await page.evaluate("control.postMessage({type:'stop'})")
            await page.wait_for_function("results.some(x => x.type === 'closed')", timeout=10000)
            events = await page.evaluate('results')
            assert not any(x['type'] == 'failure' for x in events), events[-5:]
            assert len(sockets) == 1, sockets
            assert any(x['type'] == 'terminal' and x['status'] == 'stopped' for x in events), events[-5:]
            result = {'method': args.method, 'first_height': first,
                      'last_height': max(x.get('height', 0) for x in events),
                      'websockets': sockets, 'events': events, 'errors': errors}
            args.output.write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps({key: value for key, value in result.items() if key != 'events'}))
        except Exception:
            args.output.write_text(json.dumps({'events': await page.evaluate('results'),
                                              'websockets': sockets, 'errors': errors}, indent=2))
            raise
        finally:
            await browser.close()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True, help='Page directory serving the generated application module')
    parser.add_argument('--bridge', required=True)
    parser.add_argument('--network', default='regtest')
    parser.add_argument('--method', default='asyncFollow', help='asyncRun reproduces baseline early termination')
    parser.add_argument('--hold-seconds', type=float, default=1.5, help='125 crosses the idle refresh deadline')
    parser.add_argument('--headers', action='store_true', help='Check active-chain wire headers, hashes and range bounds')
    parser.add_argument('--refresh', action='store_true', help='Require a locator refresh on the same session')
    parser.add_argument('--new-header', action='store_true', help='Fixture must advance its tip after caught_up')
    parser.add_argument('--output', type=Path, required=True)
    asyncio.run(main(parser.parse_args()))
