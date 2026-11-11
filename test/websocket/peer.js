// Test-only BCH peer using the browser's real WebSocket API. No WASM coordinator.
const MAGIC = 0xfabfb5da;
const join = (...arrays) => {
  const out = new Uint8Array(arrays.reduce((n, a) => n + a.length, 0));
  let offset = 0;
  for (const a of arrays) { out.set(a, offset); offset += a.length; }
  return out;
};
const u32 = n => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, n, true); return b; };
const u64 = n => { const b = new Uint8Array(8); new DataView(b.buffer).setBigUint64(0, BigInt(n), true); return b; };
const text = s => new TextEncoder().encode(s);
const checksum = async b => new Uint8Array(await crypto.subtle.digest('SHA-256', await crypto.subtle.digest('SHA-256', b)));
const equal = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

class Peer {
  constructor(url, discovery = true) {
    this.url = url;
    this.discovery = discovery;
    this.bytes = new Uint8Array();
    this.messages = new Map();
    this.waiters = new Map();
    this.commands = [];
    this.receiving = Promise.resolve();
    this.sending = Promise.resolve();
  }
  async open() {
    this.ws = new WebSocket(this.url);
    this.ws.binaryType = 'arraybuffer';
    this.ws.onmessage = ({data}) => {
      this.receiving = this.receiving.then(() => this.receive(new Uint8Array(data))).catch(error => this.fail(error));
    };
    this.ws.onclose = () => this.fail(new Error('WebSocket closed'));
    await new Promise((resolve, reject) => { this.ws.onopen = resolve; this.ws.onerror = () => reject(new Error('WebSocket failed')); });
    const agent = text('/BrowserTransportTest:1/');
    const version = join(u32(70016), u64(0), u64(Math.floor(Date.now() / 1000)), new Uint8Array(52),
      u64(BigInt(Math.floor(Math.random() * Number.MAX_SAFE_INTEGER))), new Uint8Array([agent.length]), agent, u32(0), new Uint8Array([0]));
    await this.send('version', version, true);
    const remote = await this.wait('version');
    const agentLength = remote[80];
    this.remoteAgent = new TextDecoder().decode(remote.slice(81, 81 + agentLength));
    if (this.discovery) await this.send('sendwsaddr', new Uint8Array([1]));
    await this.send('verack');
    await this.wait('verack');
    return this;
  }
  fail(error) {
    for (const list of this.waiters.values()) for (const waiter of list) waiter.reject(error);
    this.waiters.clear();
    this.error = error;
  }
  async receive(bytes) {
    this.bytes = join(this.bytes, bytes);
    while (this.bytes.length >= 24) {
      const header = new DataView(this.bytes.buffer, this.bytes.byteOffset, 24);
      if (header.getUint32(0, true) !== MAGIC) throw new Error('Wrong BCH network magic');
      const size = header.getUint32(16, true);
      if (size > 4 * 1024 * 1024) throw new Error('Test peer payload limit');
      if (this.bytes.length < 24 + size) return;
      const command = new TextDecoder().decode(this.bytes.slice(4, 16)).replace(/\0.*$/, '');
      const payload = this.bytes.slice(24, 24 + size);
      if (!equal((await checksum(payload)).slice(0, 4), this.bytes.slice(20, 24))) throw new Error('Bad BCH checksum');
      this.bytes = this.bytes.slice(24 + size);
      this.commands.push(command);
      if (command === 'sendwsaddr') {
        if (this.remoteVerack || this.remoteDiscoverySeen || payload.length !== 1) throw new Error('Invalid discovery announcement');
        this.remoteDiscoverySeen = true;
        this.remoteDiscovery = payload[0] === 1;
      }
      if (command === 'verack') this.remoteVerack = true;
      if (command === 'getheaders' && this.knownHeaders) {
        await this.send('headers', this.knownHeaders);
      }
      if (command === 'getwsaddr' && this.discovery && this.remoteDiscovery) {
        if (payload.length !== 10 || payload[0] !== 1) throw new Error('Malformed discovery request');
        await this.send('wsaddr', join(new Uint8Array([1]), payload.slice(2, 10), new Uint8Array([0])));
        continue;
      }
      if (command === 'ping') { await this.send('pong', payload); continue; }
      const waiting = this.waiters.get(command);
      if (waiting?.length) waiting.shift().resolve(payload);
      else {
        if (!this.messages.has(command)) this.messages.set(command, []);
        this.messages.get(command).push(payload);
      }
    }
  }
  async send(command, payload = new Uint8Array(), split = false) {
    this.sending = this.sending.then(async () => {
      const name = new Uint8Array(12); name.set(text(command));
      const packet = join(u32(MAGIC), name, u32(payload.length), (await checksum(payload)).slice(0, 4), payload);
      if (split) { this.ws.send(packet.slice(0, 13)); this.ws.send(packet.slice(13)); }
      else this.ws.send(packet);
    });
    return this.sending;
  }
  wait(command) {
    if (this.messages.get(command)?.length) return Promise.resolve(this.messages.get(command).shift());
    if (this.error) return Promise.reject(this.error);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('Timeout waiting for ' + command)), 10000);
      const waiter = {resolve: value => { clearTimeout(timer); resolve(value); }, reject: error => { clearTimeout(timer); reject(error); }};
      if (!this.waiters.has(command)) this.waiters.set(command, []);
      this.waiters.get(command).push(waiter);
    });
  }
  async ping() {
    const nonce = u64(0x1234567811223344n);
    await this.send('ping', nonce);
    if (!equal(await this.wait('pong'), nonce)) throw new Error('Pong nonce mismatch');
  }
  async endpoints(mask = 3) {
    if (!this.discovery || !this.remoteDiscovery) throw new Error('Discovery not negotiated');
    const id = u64(0x9988776655443322n);
    await this.send('getwsaddr', join(new Uint8Array([1, mask]), id));
    const payload = await this.wait('wsaddr');
    if (payload.length < 10 || payload[0] !== 1 || !equal(payload.slice(1, 9), id) || payload[9] > 16) throw new Error('Bad discovery response');
    const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    const endpoints = [];
    let offset = 10;
    for (let i = 0; i < payload[9]; i++) {
      const transport = payload[offset++];
      const expires = view.getUint32(offset, true); offset += 4;
      const port = view.getUint16(offset, true); offset += 2;
      const hostLength = payload[offset++];
      const host = new TextDecoder().decode(payload.slice(offset, offset + hostLength)); offset += hostLength;
      const pathLength = view.getUint16(offset, true); offset += 2;
      const path = new TextDecoder().decode(payload.slice(offset, offset + pathLength)); offset += pathLength;
      if (!(transport & mask) || expires <= Date.now() / 1000) throw new Error('Ineligible endpoint');
      const scheme = transport === 1 ? 'ws' : 'wss';
      endpoints.push(`${scheme}://${host.includes(':') ? '[' + host + ']' : host}:${port}${path}`);
    }
    if (offset !== payload.length) throw new Error('Trailing discovery bytes');
    return endpoints;
  }
  async headers() {
    const genesis = Uint8Array.from('0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206'.match(/../g), b => parseInt(b, 16)).reverse();
    await this.send('getheaders', join(u32(70016), new Uint8Array([1]), genesis, new Uint8Array(32)));
    const payload = await this.wait('headers');
    if (payload[0] !== 3 || payload.length !== 244) throw new Error('Expected three fixture headers');
    let previous = genesis;
    for (let i = 0; i < 3; i++) {
      const header = payload.slice(1 + i * 81, 81 + i * 81);
      if (!equal(header.slice(4, 36), previous)) throw new Error('Unlinked header');
      if (payload[81 + i * 81] !== 0) throw new Error('Nonzero headers transaction count');
      previous = await checksum(header);
    }
    this.knownHeaders = payload;
    return payload[0];
  }
  async serveHeaders() {
    await this.send('ping', u64(0x4845414445525301n));
    if (!equal(await this.wait('pong'), u64(0x4845414445525301n))) throw new Error('Header request trigger failed');
    if (!equal(await this.wait('pong'), u64(0x4845414445525302n))) throw new Error('Native peer did not receive browser-served headers');
  }
  async close() {
    if (this.ws.readyState === WebSocket.CLOSED) return;
    const closed = new Promise(resolve => this.ws.addEventListener('close', resolve, {once:true}));
    this.ws.close(1000, 'test complete');
    await closed;
  }
}
window.Peer = Peer;
window.testUtils = {join, u32, u64, text, equal};
