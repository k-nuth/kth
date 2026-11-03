// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying LICENSE file.
// The Worker owns the native client and joins all outstanding callbacks before disposal.
let started = false;
let stopRequested = false;
let client;
let pendingRun;
let pendingStop;
let pendingRefresh;
let wakeNext;
let nextTimer;

function wakeRetry() {
    if (!wakeNext) return false;
    clearTimeout(nextTimer);
    const wake = wakeNext;
    wakeNext = undefined;
    wake();
    return true;
}

self.onmessage = ({ data }) => {
    if (data.type === 'getHeaders') {
        const result = { type: 'headerWindow', requestId: data.requestId, ready: !!client, headers: [] };
        if (client) {
            const tip = Number(client.lastHeight());
            const top = data.height == null ? tip : Number(data.height);
            result.tip = tip;
            result.tipHash = client.headerHash(BigInt(tip));
            if (Number.isSafeInteger(top) && top >= 0 && top <= tip) {
                for (let height = top; height >= 0 && result.headers.length < Math.min(50, data.count ?? 25); --height) {
                    result.headers.push({ height, hex: client.headerHex(BigInt(height)), hash: client.headerHash(BigInt(height)) });
                }
            }
        }
        postMessage(result);
    } else if (data.type === 'stop') {
        stopRequested = true;
        wakeRetry();
        if (client && pendingRun) {
            pendingStop ??= client.asyncRequestStop();
            pendingStop.catch(error => postMessage({ type: 'diagnostic', message: String(error) }));
        }
    } else if (data.type === 'resume' && started && !stopRequested) {
        if (wakeRetry()) return;
        if (client && pendingRun && !pendingRefresh) {
            pendingRefresh = client.asyncRequestRefresh()
                .catch(error => postMessage({ type: 'diagnostic', message: String(error) }))
                .finally(() => { pendingRefresh = undefined; });
        }
    } else if (data.type === 'start' && !started) {
        started = true;
        synchronize(data);
    }
};

async function synchronize(options) {
    let observation;
    let announcedHeight = -1;
    let retries = 0;
    const snapshot = () => {
        if (!client) return;
        const height = Number(client.lastHeight());
        const status = client.status();
        postMessage({ type: 'progress', height, status, bytes: client.indexMemoryBytes(), tipHash: client.headerHash(BigInt(height)) });
        if (status === 'caught_up') retries = 0;
        if (status === 'caught_up' && height !== announcedHeight) {
            announcedHeight = height;
            postMessage({ type: 'caughtUp', height });
        }
    };
    try {
        postMessage({ type: 'loading' });
        importScripts('browser_headers_api_control.js');
        const core = await createHeaders({ printErr: message => postMessage({ type: 'diagnostic', message }) });
        if (!stopRequested) {
            client = new core.HeaderSync(options.bridge, options.network ?? 'mainnet', options.capacity ?? 1500000);
            snapshot();
            observation = setInterval(snapshot, 200);
            while (!stopRequested) {
                let failure;
                try {
                    pendingRun = client.asyncFollow();
                    await pendingRun;
                } catch (error) {
                    failure = error;
                } finally {
                    pendingRun = undefined;
                }
                if (stopRequested) break;
                const detail = client.status();
                // Invalid chain data/configuration is terminal. Connectivity does not clear start intent.
                if (/invalid|capacity|proof|timestamp|difficulty|no header progress/i.test(detail)) {
                    throw failure ?? new Error(detail);
                }
                const delay = Math.min(60000, 5000 * 2 ** Math.min(retries++, 4)) + Math.floor(Math.random() * 1000);
                postMessage({ type: 'retry', delay, detail });
                await new Promise(resolve => { wakeNext = resolve; nextTimer = setTimeout(resolve, delay); });
                wakeNext = undefined;
                if (pendingRefresh) await pendingRefresh;
                if (!stopRequested) client.reset();
            }
        }
        snapshot();
        postMessage({ type: 'stopped' });
    } catch (error) {
        postMessage({ type: 'failure', message: String(error), detail: client?.status(), fatal: true });
    } finally {
        clearInterval(observation);
        clearTimeout(nextTimer);
        if (client) {
            try {
                if (pendingRefresh) await pendingRefresh;
                if (pendingStop) await pendingStop;
                client.dispose();
            } catch (error) {
                postMessage({ type: 'failure', message: 'Cleanup: ' + String(error), fatal: true });
            }
            client = undefined;
        }
        postMessage({ type: 'closed' });
    }
}
