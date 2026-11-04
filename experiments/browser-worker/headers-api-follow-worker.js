// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying LICENSE file.
let client;
self.onmessage = async ({ data }) => {
    if (data.type === 'stop') {
        await client.asyncRequestStop();
        return;
    }
    if (data.type === 'headers') {
        const tip = client.lastHeight();
        const heights = [tip, tip > 0n ? tip - 1n : 0n, 0n];
        postMessage({ type: 'headers', headers: heights.map(height => ({
            height: Number(height), hex: client.headerHex(height), hash: client.headerHash(height),
        })), absent: client.headerHex(tip + 1n), absentHash: client.headerHash(18446744073709551615n) });
        return;
    }
    if (data.type === 'resume') {
        await client.asyncRequestRefresh();
        postMessage({ type: 'refreshed' });
        return;
    }
    importScripts('browser_headers_api_control.js');
    const core = await createHeaders();
    client = new core.HeaderSync(data.bridge, data.network, data.capacity ?? 1500000);
    let terminal = false;
    const run = client[data.method]().then(() => { terminal = true; });
    const observe = setInterval(() => postMessage({
        type: 'state', height: Number(client.lastHeight()), status: client.status(), terminal,
    }), 100);
    try {
        await run;
        postMessage({ type: 'terminal', status: client.status() });
    } catch (error) {
        postMessage({ type: 'failure', message: String(error) });
    } finally {
        clearInterval(observe);
        client.dispose();
        postMessage({ type: 'closed' });
    }
};
