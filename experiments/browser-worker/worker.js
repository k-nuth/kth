const moduleName = new URL(self.location).searchParams.get('module') || '';
if (!/^browser_(stream|cap|eof|select|terminal|headers|persist|peers|content)_control\.js$/.test(moduleName)) throw new Error('Unknown control module');
var Module = {print: text => postMessage(text), printErr: text => postMessage('ERROR: '+text)};
importScripts(moduleName);
setTimeout(() => postMessage('Worker event loop heartbeat'),100);
