// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

let client;
let run;
let progress;
self.onmessage = async ({ data }) => {
    try {
        if (data.type === 'stop') {
            if (client && run) await client.asyncRequestStop();
            return;
        }
        if (data.type !== 'start' || client) return;
        importScripts('browser_headers_api_control.js');
        const core = await createHeaders({ printErr: text => postMessage({ type: 'diagnostic', text }) });
        client = new core.HeaderSync(data.bridge, data.network, data.capacity ?? 1500000);
        const snapshot = () => postMessage({
            type: 'progress', height: Number(client.lastHeight()),
            status: client.status(), bytes: client.indexMemoryBytes(),
        });
        run = client.asyncRun();
        // Rendering observes the actor; it does not drive protocol progress.
        progress = setInterval(snapshot, 100);
        snapshot();
        await run;
        snapshot();
        clearInterval(progress);
        // Terminal callback releases the native operation's lifetime pin.
        const finalStatus = client.status();
        client.dispose();
        client = undefined;
        postMessage({ type: 'done', status: finalStatus });
    } catch (error) {
        clearInterval(progress);
        const status = client?.status();
        if (client) {
            try { client.dispose(); client = undefined; }
            catch (cleanupError) { postMessage({ type: 'cleanupFailure', message: String(cleanupError) }); }
        }
        postMessage({ type: 'failure', message: String(error), status });
    }
};
