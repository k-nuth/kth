// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

self.onmessage = async ({ data }) => {
    try {
        importScripts('browser_headers_api_control.js');
        const core = await createHeaders();
        const failures = [];
        for (const [url, capacity] of [['', 16], [data.bridge, 0], [data.bridge, 2000001]]) {
            let client;
            try {
                client = new core.HeaderSync(url, 'regtest', capacity);
            } catch (error) {
                if (error.status !== 10001) throw error;
                failures.push(error.status);
            }
            if (client) {
                client.dispose();
                throw new Error('Invalid configuration was accepted');
            }
        }
        const valid = new core.HeaderSync(data.bridge, 'regtest', 16);
        if (valid.lastHeight() !== 0n) throw new Error('Valid construction failed after errors');
        valid.dispose();
        postMessage({ type: 'done', failures, recovered: true });
    } catch (error) {
        postMessage({ type: 'failure', message: String(error) });
    }
};
