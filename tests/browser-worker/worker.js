const moduleName = new URL(self.location).searchParams.get('module') || 'proof.js';
if (!/^(proof|browser_(stream|cap|eof|select|terminal|interop|lifecycle|timer|headers_session|reset)_control)\.js$/.test(moduleName)) throw new Error('Unknown control module');
var Module = {arguments: new URL(self.location).searchParams.has('peer') ? [new URL(self.location).searchParams.get('peer')] : [], print: text => postMessage(text), printErr: text => postMessage('ERROR: '+text)};
importScripts(moduleName);
setTimeout(() => postMessage('Worker event loop heartbeat'),100);
