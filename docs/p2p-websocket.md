# Native P2P WebSocket transport

KTH can accept ordinary BCH TCP peers and binary WebSocket peers through the
same peer supervisor, BCH version handshake, framing and dispatcher. WebSocket
is a transport capability; it does not describe how a client is compiled.

## Build and configuration

The server transport is opt-in at build time:

```sh
conan install . --version=0.0.0 -s compiler.cppstd=23 \
  -o with_websocket=True --build=missing --lockfile-partial -of build-ws
cmake -S . -B build-ws/build/Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build-ws/build/Release/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ws/build/Release --parallel
```

Use the generated Conan toolchain when configuring CMake. The corresponding
CMake option is `KTH_WITH_WEBSOCKET=ON`. The additional codec dependency is
`wslay/1.1.1`; HTTP upgrade parsing uses Boost.Beast's parser without introducing
Boost.Asio sockets or a second I/O loop. `KTH_WITH_WEBSOCKET=OFF` omits the server
source and codec dependency. Endpoint discovery remains available to TCP peers.
The Emscripten recipe does not build this native server transport.

Example configuration (flat keys, matching existing KTH config conventions):

```ini
net.ws_enabled=true
net.ws_bind=127.0.0.1
net.ws_port=8080
net.ws_path=/p2p
net.ws_origins=https://wallet.example.org
net.ws_max_connections=32
net.ws_handshake_seconds=10
net.ws_discovery=true
net.ws_public_endpoints=wss://peer.example.org/p2p
net.ws_allow_private=false
```

`net.ws_enabled` defaults to false. Attempting to enable it in a build without
server support produces an explicit startup error. TCP settings remain separate.

Allowed Origins are exact strings. Missing Origin is rejected, including for
native WebSocket clients. Origin policy is a browser access control, not client
authentication. Public URLs are explicit operator configuration, never inferred
from a TCP address or the bind. A relay-only TCP node can set
`net.ws_discovery=true` while leaving `net.ws_enabled=false` and declaring no
local endpoint. Set `net.ws_allow_private=true` only for explicit LAN experiments;
that setting permits private records both in local configuration and received
gossip, and such experiments must not be treated as public reachability tests.

## WSS deployment

The native listener is plaintext WS. Terminate TLS at an ordinary reverse proxy
and forward the WebSocket upgrade and original Origin header. An HTTPS browser
application uses a `wss://` advertised URL. For example, with nginx:

```nginx
location = /p2p {
    proxy_pass http://127.0.0.1:8080;
    proxy_http_version 1.1;
    proxy_set_header Upgrade $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Origin $http_origin;
    proxy_set_header X-Forwarded-For $remote_addr;
    proxy_read_timeout 5m;
    proxy_send_timeout 30s;
}
```

Behind the proxy every client connects from its address. List it in
`net.ws_trusted_proxies` (here `127.0.0.1`) and have it set `X-Forwarded-For`
to the client's address, as above: the node then scores and bans the client,
the last entry of that header, not the proxy. A connection from a listed proxy
without the header is refused; from any other address the header is ignored.

The encompassing HTTPS server needs a valid certificate and port 443 listener.
Limit connections, request rate and HTTP header sizes at the public proxy as
well. KTH does not trust X-Forwarded-For or change peer ban identity based on an
untrusted request header. Peers behind one proxy consequently share its socket
IP for IP-based policy; normal transport closure is not itself protocol misconduct.

## Stream contract and ownership

Binary WebSocket payloads form the BCH byte stream. One BCH message may span
many WebSocket messages, and one WebSocket message may contain several BCH
messages. Client code must split outgoing data into messages no larger than
64 KiB and concatenate received payloads before BCH parsing. WebSocket
fragmentation and control frames are handled by the transport codec.

The listener has a dedicated execution strand, four owned HTTP upgrade workers,
a four-socket upgrade queue, and an eight-event admission queue. Every upgrade
has a bounded deadline; a silent client does not monopolize all admission.
The configured connection cap includes pending upgrades, sessions handed to
the supervisor and sessions queued for handoff. Pre-admission accepted sockets
are separately bounded by the event queue and one accept operation. Refused admission closes the
socket; it is never an untracked connection. The supervisor owns each established
session, including negotiation and shutdown. Transport queues are bounded;
saturation terminates the session explicitly rather than dropping stream bytes.
A terminated stream cannot resume: a new connection starts a new BCH handshake.

The application should normally keep sessions open. On network failure, use
bounded exponential backoff with jitter rather than repeatedly rebuilding
healthy sessions. Serving `getheaders` is independent of connection direction.
A client that opened an outbound connection can respond to requests over that
same connection; it does not thereby gain a public listener or an endpoint to
advertise. KTH's chain-backed header handler is shared across transports.

## Discovery

The negotiated experimental extension is specified in
[websocket-discovery.md](websocket-discovery.md). It advertises connection
parameters, not an ability to execute WebAssembly. Existing BCH `addr`/`addrv2`
formats, network IDs and service bits remain unchanged.

The store belongs to a single task. Decoding and per-session rate checks occur
before bounded submission to it. A separate owned resolver classifies DNS
candidates; slow DNS cannot block the store's replies. Verification work has a
bounded queue and bounded owner-held debt, reactivated by verification completion.
Records with unresolved or nonpublic DNS answers remain unadvertised. Public
classification is not a promise that a listener is live, honest, or permanently
resolves to the same addresses. Received endpoints remain untrusted hints.

## Reproducible controls

With tests enabled, build `kth_network_test`, `kth_ws_config_test`, and
`kth_ws_fixture`. Run the discovery cases and runtime settings controls:

```sh
./build-ws/build/Release/src/network/kth_network_test '[ws_discovery]'
./build-ws/build/Release/src/node/kth_ws_config_test
```

Build and run the config control again with WebSocket server support excluded.
That proves activation is refused while ordinary TCP and relay-only discovery
remain compilable. The browser control requires Python 3.11+, Playwright and
its Chromium installation, used only as a test driver:

```sh
python -m pip install playwright
python -m playwright install chromium
python test/websocket/browser.py \
  --fixture ./build-ws/build/Release/src/node/kth_ws_fixture \
  --output /tmp/kth-native-ws-browser.json
```

The test starts two native KTH P2P fixtures, a TCP link between them, an HTTP
page server, an HTTPS page server, and a TLS test proxy. It exercises WS, WSS
from an HTTPS page, BCH handshake, headers, endpoint discovery across TCP,
transport filtering, legacy negotiation, persistent sessions and normal quick
reconnections. Certificates are ephemeral and trusted only by that test browser.
The fixture stores three mined regtest headers in a disposable chain and serves
them through the production header handler. It does not run block synchronization,
UTXO processing or a wallet, and it is not a WASM coordinator test.

The production executable has a separate smoke control that initializes a fresh
regtest data directory and checks its actual registered handler from Chromium:

```sh
python test/websocket/full_node.py \
  --node ./build-ws/build/Release/src/node-exe/kth \
  --log /tmp/kth-full-node-ws.log
```

It requests the regtest genesis header with an empty locator, then closes the
browser and requires a successful node shutdown. Neither browser control
implements the WASM header coordinator; that integration is separate.

Linux development CI enables the server option and runs transport/discovery
controls. Package builds retain the default disabled option; native builds
without the option run the configuration rejection and TCP tests as well.
