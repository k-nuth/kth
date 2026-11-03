// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see COPYING.
self.onmessage = ({ data }) => {
    if (data.type !== 'start') return;
    self.onmessage = null;
    self.Module = {
        arguments: [data.bridge, data.network, ...(data.stop ? ["stop"] : [])],
        print: text => postMessage({ type: 'log', text }),
        printErr: text => postMessage({ type: 'error', text }),
    };
    importScripts('browser_headers_control.js');
};
