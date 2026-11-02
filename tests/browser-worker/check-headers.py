#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Run the real Chromium/WASM header control against an operator-provided bridge."""
import argparse
import asyncio
import json
from pathlib import Path
from urllib.parse import urlencode
from playwright.async_api import async_playwright


async def run(args):
    async with async_playwright() as playwright:
        browser = await playwright.chromium.launch(headless=True)
        page = await browser.new_page()
        errors = []
        page.on('pageerror', lambda error: errors.append(str(error)))
        try:
            url = args.url + '?' + urlencode({'network': args.network, 'bridge': args.bridge, 'stop': '1' if args.expect_stopped else '0'})
            await page.goto(url)
            await page.click('#start')
            await page.wait_for_function(
                "document.querySelector('#log').textContent.includes('OWNER QUIESCENT timers=0 streams=0')",
                timeout=args.timeout * 1000)
        finally:
            report = {'state': await page.locator('#status').get_attribute('data-state'),
                      'height': await page.locator('#height').inner_text(),
                      'log': await page.locator('#log').inner_text(), 'errors': errors}
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
            await page.screenshot(path=str(args.output / 'browser.png'), full_page=True)
            await browser.close()
        assert not errors, errors
        if args.expect_stopped:
            assert report['state'] == 'stopped', report
            assert 'HEADERS caught_up' not in report['log'], report
        elif args.expect_rejection:
            assert report['state'] == 'failed', report
            assert 'HEADERS FAILED proof of work invalid' in report['log'], report
            assert 'HEADERS caught_up' not in report['log'], report
        else:
            assert report['state'] == 'complete', report
            height = int(report['height'].replace(',', ''))
            if args.expected_height is not None:
                assert height == args.expected_height, report
            assert 'HEADERS joined' in report['log'], report
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:18765/headers.html')
    parser.add_argument('--bridge', required=True)
    parser.add_argument('--network', choices=('mainnet', 'regtest'), default='regtest')
    parser.add_argument('--expected-height', type=int)
    parser.add_argument('--expect-rejection', action='store_true')
    parser.add_argument('--expect-stopped', action='store_true')
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--output', type=Path, required=True)
    asyncio.run(run(parser.parse_args()))
