#!/usr/bin/env python3
# Copyright (c) 2016-present Knuth Project developers.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Execute real WASM controls in Chromium Workers with loopback fixtures."""
import argparse
import asyncio
import json
import mimetypes
from contextlib import AsyncExitStack
from pathlib import Path

from playwright.async_api import async_playwright
from websockets.asyncio.server import serve
import fixtures

CONTROLS = ("stream", "cap", "eof", "select", "terminal", "headers", "persist", "peers", "content")
WORKER_TEST = r"""async name => {
    const lines = [];
    return await new Promise(resolve => {
        const worker = new Worker(`worker.js?module=browser_${name}_control.js`);
        let joined = false, idle = false;
        const finish = ok => {
            clearTimeout(deadline);
            worker.terminate();
            resolve({name, ok, lines});
        };
        const deadline = setTimeout(() => {
            lines.push('FAILED: control deadline exceeded');
            finish(false);
        }, 60000);
        worker.onerror = event => {
            lines.push('FAILED: ' + event.message);
            finish(false);
        };
        worker.onmessage = event => {
            const text = String(event.data);
            lines.push(text);
            if (/FAILED|UNEXPECTED|ERROR:/.test(text)) return finish(false);
            // A control prints "JSON <document>" for what must parse as JSON.
            if (text.startsWith('JSON ')) {
                try { JSON.parse(text.slice(5)); }
                catch (error) { lines.push('FAILED: invalid JSON: ' + error.message); return finish(false); }
            }
            joined ||= text.includes(`${name} control joined`);
            idle ||= text.includes('OWNER QUIESCENT timers=0 streams=0');
            if (joined && idle) finish(true);
        };
    });
}"""


async def run(args):
    source = Path(__file__).resolve().parent
    artifacts = Path(args.artifacts).resolve()
    files = {"/": b"<!doctype html><title>Worker transport CI</title>",
             "/worker.js": (source / "worker.js").read_bytes()}
    for name in CONTROLS:
        for extension in ("js", "wasm"):
            path = artifacts / f"browser_{name}_control.{extension}"
            files["/" + path.name] = path.read_bytes()

    async def http(reader, writer):
        try:
            request = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 5)
            method, target, _ = request.split(b"\r\n", 1)[0].decode().split()
            target = target.split("?", 1)[0]
            body = files.get(target) if method == "GET" else None
            status = "200 OK" if body is not None else "404 Not Found"
            body = body if body is not None else b"Not found"
            content_type = mimetypes.guess_type(target)[0] or "text/html"
            writer.write((f"HTTP/1.1 {status}\r\nContent-Length: {len(body)}\r\n"
                          f"Content-Type: {content_type}\r\nConnection: close\r\n\r\n").encode() + body)
            await writer.drain()
        finally:
            writer.close()
            await writer.wait_closed()

    results = []
    async with AsyncExitStack() as stack:
        if not args.external_fixtures:
            for port, handler in ((18765, fixtures.echo), (18767, fixtures.stall),
                                  (18768, fixtures.cap), (18769, fixtures.eof),
                                  (18770, fixtures.abrupt), (18780, fixtures.bch_sync),
                                  (18781, fixtures.bch_invalid), (18782, fixtures.bch_follow),
                                  (18783, fixtures.bch_oversize), (18787, fixtures.bch_reorg),
                                  (18788, fixtures.bch_retry), (18785, fixtures.bch_longer),
                                  (18786, fixtures.bch_fork),
                                  (18789, fixtures.bch_discovery),
                                  (18803, fixtures.bch_flood), (18804, fixtures.bch_short),
                                  (18805, fixtures.bch_alternate), (18806, fixtures.bch_discovery_late),
                                  (18810, fixtures.bch_slow), (18813, fixtures.bch_discovery_close),
                                  (18800, fixtures.bch_content), (18801, fixtures.bch_bad_body),
                                  (18807, fixtures.bch_tx_silent), (18808, fixtures.bch_big_tx),
                                  (18809, fixtures.bch_missing_block),
                                  (18811, fixtures.bch_tx_late), (18812, fixtures.bch_tx_disown)):
                await stack.enter_async_context(serve(handler, "127.0.0.1", port, max_size=65536))
        server = await asyncio.start_server(http, "127.0.0.1", 0)
        await stack.enter_async_context(server)
        port = server.sockets[0].getsockname()[1]
        playwright = await stack.enter_async_context(async_playwright())
        browser = await playwright.chromium.launch()
        stack.push_async_callback(browser.close)
        page = await browser.new_page()
        await page.goto(f"http://127.0.0.1:{port}/")
        for name in CONTROLS:
            result = await page.evaluate(WORKER_TEST, name)
            results.append(result)
            print(json.dumps(result), flush=True)
    Path(args.report).write_text(json.dumps(results, indent=2) + "\n")
    if not all(result["ok"] for result in results):
        raise SystemExit("Browser Worker controls failed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifacts", help="CMake experiments/browser-worker output directory")
    parser.add_argument("--report", default="browser-results.json")
    parser.add_argument("--external-fixtures", action="store_true",
                        help="use already running loopback fixtures during local development")
    asyncio.run(run(parser.parse_args()))
