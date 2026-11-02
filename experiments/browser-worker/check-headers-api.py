#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Check the real application binding against an operator-provided WS bridge.

Use a silent peer for lifecycle, a 3005-header regtest peer for reconnect,
and a deliberately invalid-PoW peer for rejection. These are separate fixtures.
"""
import argparse
import asyncio
import json
from pathlib import Path
from urllib.parse import urlencode, urljoin
from playwright.async_api import async_playwright


async def run(args):
    args.output.mkdir(parents=True, exist_ok=True)
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch(headless=True)
        page = await browser.new_page(viewport={'width': 1100, 'height': 850})
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        report = {}
        try:
            url = urljoin(args.url, 'headers-api.html') + '?' + urlencode({
                'bridge': args.bridge, 'network': args.network})
            await page.goto(url)
            if args.scenario in ('lifecycle', 'reconnect', 'construction'):
                worker = {'lifecycle': 'headers-api-contract-worker.js',
                          'reconnect': 'headers-api-reconnect-worker.js',
                          'construction': 'headers-api-construction-worker.js'}[args.scenario]
                await page.evaluate('''({worker, bridge}) => {
                    window.results = [];
                    const control = new Worker(worker);
                    control.onmessage = event => results.push(event.data);
                    control.onerror = event => results.push({type:'failure', message:event.message});
                    control.postMessage({bridge});
                }''', {'worker': worker, 'bridge': args.bridge})
                await page.wait_for_function("results.some(x => x.type === 'done' || x.type === 'failure')",
                                             timeout=args.timeout * 1000)
                report = {'messages': await page.evaluate('results')}
                assert report['messages'][-1]['type'] == 'done', report
            else:
                await page.click('#start')
                await page.wait_for_function("['complete', 'failed'].includes(document.querySelector('#status').dataset.state)",
                                             timeout=args.timeout * 1000)
                report = {'state': await page.locator('#status').get_attribute('data-state'),
                          'height': await page.locator('#height').inner_text(),
                          'status': await page.locator('#status').inner_text(),
                          'message': json.loads(await page.locator('#log').inner_text())}
                if args.scenario == 'rejection':
                    assert report['state'] == 'failed', report
                    assert report['status'] == 'proof of work invalid', report
                else:
                    assert report['state'] == 'complete', report
                    assert report['status'] == 'caught_up', report
                    height = int(report['height'].replace(',', ''))
                    assert height > 0, report
                    if args.expected_height is not None:
                        assert height == args.expected_height, report
            assert not errors, errors
        finally:
            report['errors'] = errors
            (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            await page.screenshot(path=str(args.output / 'browser.png'), full_page=True)
            await browser.close()
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True, help='Directory URL serving the page and WASM controls')
    parser.add_argument('--bridge', required=True)
    parser.add_argument('--network', choices=('mainnet', 'regtest'), default='regtest')
    parser.add_argument('--scenario', choices=('sync', 'rejection', 'lifecycle', 'reconnect', 'construction'), default='sync')
    parser.add_argument('--expected-height', type=int)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--output', type=Path, required=True)
    asyncio.run(run(parser.parse_args()))
