# Browser Worker transport controls

This directory owns the C++ controls, Python runners, loopback fixtures, manual
transport harness (`index.html` / `worker.js`), and scenario-specific application
Workers. Reusable application demos live in
[`examples/browser-worker`](../../examples/browser-worker/README.md).
All commands below are run from the repository root unless stated otherwise.

These controls exercise the opt-in C++ browser transport, header actor and
application boundary. They do not establish full block/UTXO validation or a
background execution guarantee. BCH protocol parsing and header validation run
in C++; JavaScript provides the browser harness and storage integration.

## Ownership and limits

One Worker owns the runtime, sockets, timers and nursery state. Browser events
schedule a one-shot drain of at most 64 runnable Asio handlers. Exhausting that
budget schedules a continuation; idle time causes no executor polling. Browser
one-shot deadlines replace native reactor timers. The pinned Asio fork exposes
its internal selection timer through ASIO_CHANNEL_SELECT_TIMER_HEADER and
ASIO_CHANNEL_SELECT_TIMER_TYPE. Browser builds select the Worker timer through
those compile definitions; ordinary builds retain Asio's default steady timer.
No dependency headers or package-cache files are rewritten.

The WebSocket adapter implements partial stream reads and writes of at most
65536 bytes per binary message. Incoming admission is bounded by 1 MiB and 64
segments; outgoing browser buffered payload is bounded by 1 MiB. Saturation
terminates the session and retains its error. Accepted final bytes drain before
clean EOF. Reconnection starts a new BCH session. Browser WebSocket has no drain
notification: outgoing admission either succeeds or closes, without polling
bufferedAmount. These bounds exclude browser/OS internal buffering and the
browser's allocation for a received WebSocket message. The bridge must enforce
its 65536-byte message contract too.

Each browser control has a 64 MiB initial and 512 MiB maximum WASM memory. The
observed modules use unshared memory. The page terminates each Worker after its
control joins, or after a 15-second harness deadline.

## Build and run

Use the repository's Emscripten 6 profile and a fresh build directory. Set
`SOURCE`, `DEPENDENCIES` and `BUILD` to your source/dependency/build directories:

```sh
conan install "$SOURCE" --output-folder="$DEPENDENCIES" \
  -pr:h "$SOURCE/ci_utils/conan/profiles/ems6" --lockfile-partial \
  -o browser_node=True -o tests=False -o rpc=False -o consensus=False --build=missing
. "$DEPENDENCIES/build/Release/generators/conanbuild.sh"
cmake -S "$SOURCE" -B "$BUILD" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$DEPENDENCIES/build/Release/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_NODE_EXE=OFF -DBUILD_C_API=ON \
  -DKTH_BROWSER_CONTROLS=ON
cmake --build "$BUILD" -j4
node "$BUILD/tests/browser-worker/browser_configuration_control.js"
```

The configuration control runs the browser-compiled parser in Node, checks that
automatic inbound/outbound connections default to zero and DNS seeds are empty
on both mainnet and regtest, and verifies that the
pinned Asio channel wakes a receive when closed both before and after admission.
No cancellation call is required to wake a pending receive in that version.
The embedding application must still supply one manual peer and its bridge URL;
these operator-selected endpoints intentionally have no implicit default.

Copy the generated `browser_*_control.js` and `.wasm` files from
`$BUILD/tests/browser-worker` beside this directory's `index.html` and
`worker.js` in a fresh serving directory. The header demo pages and application
Workers live separately in [`examples/browser-worker`](../../examples/browser-worker/README.md). With Python 3 and websockets 15.0.1:

```sh
python tests/browser-worker/fixtures.py all
python3 -m http.server 18764 --directory "$SERVE"
```

Open `http://127.0.0.1:18764/` in a browser. The echo, stalled-handshake,
saturation and EOF fixtures are byte-level controls, **not BCH peers**.

The interop control additionally requires a real BCHN regtest peer at TCP port
19444 with three freshly mined blocks, and a separately built bridge listening
on 18766, forwarding to that peer and allowing the exact origin
`http://127.0.0.1:18764`. Use an isolated regtest data directory. No bridge source
changes are required. The interop driver calls the real C++ peer_session,
perform_handshake_direct, domain parsers and context-free checks. Its request
sequence is a diagnostic coroutine, **not node::sync orchestration**.

## Continuous integration

The WebAssembly workflow has a separate browser-node job. It explicitly installs
dependencies with `browser_node=True`, compiles the opt-in controls and executes
them in Chromium Workers. The ordinary WASM job keeps its default configuration.
The browser job runs the parser/subscription configuration control under Node
and the stream, cap, EOF, select, terminal, lifecycle, timer, reset, headers-session
and headers-busy controls in Chromium. The last two run the real header actor
against deterministic loopback regtest peers: duplicate batches, a heavier
sibling branch, and 130 ping/pong exchanges before a valid headers reply.
The timer control checks long/infinite deadlines, multiple waiters and cancellation;
the reset control calls the raw C ABI during follow and after its terminal callback.
It requires both successful completion and zero retained timers/streams, and
fails on Worker errors or a deadline. Logs and JSON observations are uploaded.

To run the same Worker checks locally after building the controls:

```sh
python -m pip install playwright==1.55.0 websockets==15.0.1
python -m playwright install chromium
python tests/browser-worker/ci.py "$BUILD/tests/browser-worker" \
    --report browser-results.json
```

The runner owns and closes its loopback fixtures. These checks do not require
an external peer or bridge and do not certify real-node interoperability or
full chain synchronization. The separate BCHN interop and generated application-binding controls remain
manual. `browser_headers_control` is compiled in CI; the finite regtest actor
sessions are executed by `browser_headers_session_control`. Native coverage
reports do not measure code excluded by `KTH_BROWSER_NODE`.

For ordinary WASM compatibility, configure a separate build with browser_node
False, BUILD_C_API ON and KTH_LEGACY_API_CONTROL ON, build
`ordinary_wasm_api_control`, and run the resulting JS module with the toolchain's
Node executable. This tests the existing C API boundary; it does not replace the
complete generated JavaScript wrapper suite.

## Evidence and regression controls

The workflow executes the retained controls on its selected commit and uploads
logs and JSON observations. Historical manual observations below are dated and
must not be treated as execution evidence for a later tree.

The terminal-state controls reproduced two defects in the initial adapter:
closure lost the saturation reason, and clean EOF discarded accepted buffered
bytes. `terminal-state.negative.patch` reconstructs that exact adapter
(SHA256 `1380046c16aaa2dfe1a8c68bbbaf5056af676296b284578f4e452c0c188b85e7`).
Apply it only to a disposable copy of `kth/network/browser/stream.hpp` and compile the same
cap/EOF controls with that copy first in the include path. They fail for the
intended observations; the fixed header passes. This is regression evidence for
the new transport, not a claim that the unmodified base provided browser P2P.

The original proof.cpp also has a discriminating timeout cancellation mutation:
remove only emscripten_clear_timeout(timeout_id); the browser then emits
`UNEXPECTED deadline fired`. Its source SHA256 is
`47ff6900352a182ee7c8d5216205d60337548cf6a94d203fb3542a24aab99eb2`.

## Scope of storage validation

Header sessions do not open the production block/UTXO databases. Their success
does not certify production UTXO-Z geometry or LMDB capacity on wasm32. A full
browser node with those stores needs a separate storage capacity policy and
runtime validation; it is not a prerequisite for these header controls.

## Headers-only integration control

`headers-sync-proof.cpp` exercises the `node::header_sync` library actor, which downloads consecutive `getheaders` batches through the WebSocket transport and passes them to the existing `header_organizer` and `header_index`. It opens no UTXO or block database. Checkpoint lists are cleared in this control so historical proof-of-work is checked too. A heavier accepted header branch is adopted through the organizer. The peer is stopped on success or failure, and the Worker reports timer/stream quiescence.

Build with `KTH_BROWSER_NODE` enabled:

```sh
cmake --build <build-directory> --target browser_headers_control
```

Serve `headers.html` and `headers-worker.js` from `examples/browser-worker`, together with `browser_headers_control.js`, and `browser_headers_control.wasm` from one HTTP origin. Configure the Rust bridge to allow that exact origin and connect to a BCH peer. Select the matching network on the page. HTTPS deployments need WSS. The bridge selects the upstream TCP destination; the browser does not select arbitrary TCP hosts.

This is an integration control, not the application binding. The page reads progress reports from a C++ executable in a Worker. The final example still needs the generated lifecycle/status interface, application-level stop/reconnection controls and persistence decisions. The library actor owns the index and pending session; `request_stop()` is a request, and `run()` must settle before reset or disposal.

Observed locally on 2026-11-01 with real Chromium, Emscripten 6.0.13 and the Rust bridge:

- Isolated BCHN 29.0.0 regtest peer: validated height 3005, in batches of 2000 and 1005, then an empty response and zero pending timers/streams.
- Mainnet peer discovered through a configured BCH DNS seed: validated height 975537 from genesis, with checkpoint shortcuts disabled, then an empty response and zero pending timers/streams. This is one peer's observed tip, not a proof of global peer agreement or transaction validity.

The index reported 331679052 bytes at that mainnet height. This exceeds raw header storage because it includes the in-memory index and reserved capacity. The control caps the index at 1500000 entries and allows WASM memory growth; it is not yet a tuned persistent browser store. Reopening the page starts again from genesis.

A retained browser runner (`check-headers.py`, requiring Playwright and Chromium) verifies completion and quiescence and saves JSON plus a screenshot:

```sh
python tests/browser-worker/check-headers.py --bridge ws://127.0.0.1:18768 \
    --network regtest --expected-height 3005 --output /tmp/headers-valid
```

The negative control `invalid-headers-proxy.py` sits between the Rust bridge and an isolated BCHN regtest peer. It changes the final header's target in the first nonempty batch to zero and recomputes the BCH message checksum. Thus framing and transport checks pass, while consensus validation must reject the header. Keep this fixture bound to loopback and separate from production connections:

```sh
python tests/browser-worker/invalid-headers-proxy.py --upstream 19454 --listen 19455
# Configure a separate Rust bridge endpoint to forward to 127.0.0.1:19455.
python tests/browser-worker/check-headers.py --bridge ws://127.0.0.1:18770 \
    --network regtest --expect-rejection --output /tmp/headers-invalid
```

The retained negative control reported `proof of work invalid`, did not announce a synchronized tip, and drained all timers and streams. This does not replace the remaining application lifecycle/cancellation controls.

The library actor also passed a real Chromium stop control against a peer that accepts TCP but never answers the BCH handshake: a stop request at 100 ms ended the pending run and drained all timers/streams within the five-second control deadline. The runner supports `--expect-stopped` for this fixture.

### Application binding control

The `headers-api.html` page uses the asynchronous `HeaderSync` application class,
including synchronous height/status observations and an asynchronous stop request.
It does not parse console output to determine chain progress.

Prepare the application glue and pre-JS from the matching `js-wasm-private`
checkout using its kth-tools generator and installed dependencies. These generated
files are external build inputs; this repository does not contain their generator.
The configuration below assumes the prepared output is in `/tmp/headers-app`.

Configure an existing KTH Emscripten browser build with `BUILD_C_API=ON`,
`KTH_HEADERS_GLUE_CPP=/tmp/headers-app/javascript/wasm/glue.cpp` and
`KTH_HEADERS_PRE_JS=/tmp/headers-app/pre.js`. Build the
`browser_headers_api_control` target. The files must be accessible to the build
host; when using different hosts, transfer the complete prepared output first.
Serve its `.js` and `.wasm` outputs alongside `headers-api.html` and
`headers-api-worker.js` from `examples/browser-worker`. For the construction,
lifecycle and reconnect scenarios, also copy this test directory's
`headers-api-{construction,contract,reconnect}-worker.js` into the same serving
directory. The follow runner needs `headers-api-follow-worker.js` there too. Supply the bridge URL and matching network in the page.

The stop request acknowledges cancellation admission. The worker awaits the run's
terminal completion before disposing the client. Reset and disposal are refused
while a run is pending; queries and stop remain available. The accompanying
contract and reconnect workers exercise these lifetime rules against controlled
peers. Mainnet observations establish header interoperability, not SPV wallet
completeness, block validation, peer agreement, or durable storage.

The native and raw C `reset()` return false while run/follow is active and
leave the session unchanged; after all callbacks have joined, a successful reset
returns true. The generated application wrapper exposes the same boolean result.

Construction returns a C status and writes the owned handle only on success.
A failure clears the output slot. Allocation exceptions map to
`kth_ec_out_of_memory`; other construction exceptions map to
`kth_ec_unhandled_exception`. The bridge URL must be nonempty and the header
capacity must be between 1 and 2000000. The application constructor translates
these statuses into a recoverable error. The `construction` scenario of
`check-headers-api.py` verifies invalid configurations and subsequent successful
construction in the same Worker.

### Persistent headers session

`HeaderSync.asyncRun()` retains its finite behavior: it returns when the selected
peer has no more headers. `HeaderSync.asyncFollow()` instead remains pending at
`caught_up`, keeps the P2P session open, handles ping/pong, and fetches new headers
on block inventory or header announcements. A 120-second deadline also refreshes
headers on the same session for peers that do not announce updates. This deadline
waits on the inbound queue, so it never cancels an in-flight socket read.

The actor owns and joins the existing `peer_session` I/O loops. Call
`asyncRequestStop()`, then await the pending follow operation before disposal or
reset. As with run, a connection/protocol error terminates follow; a caller may
reset and retry with backoff while retaining the validated index. The browser actor supports a bounded set of peers and optional WebSocket
endpoint discovery. It shares one validated header index; several peers do not
constitute independent peer agreement. These manual controls select one peer.

Transport settings retain the normal node's 60-minute connection expiration.
A follow session rotates that connection and reconnects with backoff, preserving
its validated index. "Persistent" describes following the chain across those
connections, not one WebSocket that can never close.

After preparing and serving the generated application binding, check an idle
session across its refresh deadline:

```sh
python3 tests/browser-worker/check-headers-follow.py --url http://127.0.0.1:18765/ \
  --bridge ws://127.0.0.1:18768 --network regtest --hold-seconds 125 \
  --output /tmp/headers-follow-idle.json
```

Use `--new-header` with a fixture that advances after synchronization to require
height advancement without another WebSocket. `--method asyncRun` reproduces the
baseline early termination. The runner checks that follow remains pending at the
tip, that only one WebSocket is opened, and that Stop settles before disposal.


`asyncRequestRefresh()` coalesces a request to check the tip on the existing
session. It is admitted while follow is pending, like the stop request, and
returns an acknowledgement/height rather than joining follow. The actor owns the
one-slot refresh doorbell; repeated requests cannot grow an unbounded queue.
Use `--refresh` in `check-headers-follow.py` to require another `getheaders` on
the same WebSocket before Stop.

`headers-live-worker.js` is the reconnecting application example. Send
`{type: 'start', bridge, network, capacity}` only after the user starts the node.
It follows indefinitely, retries connectivity failures with exponential backoff
capped at 60 seconds plus jitter, and retains the validated index between sessions.
Validation/configuration errors remain terminal. Send `{type: 'resume'}` on
visible/resume/pageshow/online events: it wakes a backoff wait or requests a tip
refresh without replacing a healthy connection. `{type: 'stop'}` cancels retries,
requests native shutdown and awaits pending run/refresh/stop callbacks before
releasing the client.

The page owns user intent: persist Start/Stop per tab (for example in
`sessionStorage`) and gate all resume/automatic-start paths on that intent.
A discarded page or lost Worker has no surviving in-memory header index; restarting
then validates again from genesis. Never advertise a background-running guarantee:
the browser/OS may freeze or discard a document and its Worker.

The read-only `headerHex(height)` and `headerHash(height)` queries expose one
header from the validated active chain while run/follow is pending. Hex contains
exactly the 80-byte wire header; hash uses conventional displayed byte order.
Both return an empty string outside the validated height range. They do not fetch
blocks, use RPC or change the index. A Worker can collect a bounded window for an
application table; `headers-live-worker.js` accepts `{type: 'getHeaders', requestId,
height, count}` and returns at most 50 entries. Omit height for the current tip.
`check-headers-follow.py --headers` checks double-SHA256 hashes against the wire
bytes, parent continuity and out-of-range behavior through the generated binding.


## Optional persistent header cache

The headers API application control now exports Emscripten `FS`/`IDBFS` and links
`idbfs.js`. The ordinary transport controls remain memory-only. There are no C
API, IDL or generated-wrapper changes.

`headers-persistent-worker.js` and `header-storage.js`, in
`examples/browser-worker`, are an opt-in embedding example. Copy them beside the built `browser_headers_api_control.js`/`.wasm`,
renaming that matching module pair to `browser_headers_persistent_v1.js` and
`browser_headers_persistent_v1.wasm`. Start the Worker with the existing
`{type:'start', bridge, network, capacity}` message. This retains the existing
stop, refresh and header-window messages. The endpoint must permit the page's
exact Origin. HTTPS pages must select WSS.

The Worker mounts `/kth-header-cache/<Network>` on IDBFS before constructing
`HeaderSync`. Each network has its own IndexedDB database. When this directory
exists, the native actor maintains `chain-v1.bin`: contiguous 80-byte active-chain
headers, including genesis. It appends on extension and rewrites the active
chain when its saved anchor no longer matches after a reorganization. It never
serializes pointers, orphan branches, cached work or trusted consensus state.
Without that opt-in directory, native actor behavior is unchanged.

On restore, the native actor checks size/capacity and genesis and replays every
header through the same contextual consensus validator used by P2P sync. A
malformed or invalid cache is discarded by the embedding Worker, which starts
fresh. The active-chain locator then resumes from the restored tip. This saves
network download; local revalidation still takes time on large chains.

The Worker commits the single cache file through an IndexedDB transaction at
most once every two seconds while progress advances, and flushes on joined
Stop. An abrupt reload can lose the most recent uncommitted progress, which is
fetched again from the peer. Refresh, tab closure and browser restart preserve
the last committed chain. A quota/storage failure emits `storageUnavailable`
and networking continues in memory. Clearing or evicting site data removes the
cache; storage is scoped to the browser profile and page origin. Storage events
also include `storageLoading`, `storageRestored`, `storageSaved` and `storageReset`.

A reproducible Chromium control checks partial reload, full reload without
redownloading the chain, a complete browser restart, corrupted-cache rejection
and quota-failure fallback. It serves its own virtual fixture under an
operator-selected allowed Origin, without changing a production site's files.
The selected peer must have at least one non-genesis header:

```sh
python tests/browser-worker/check-headers-persistence.py \
  --module-dir "$BUILD/tests/browser-worker" \
  --peer ws://127.0.0.1:18774/p2p --network tempnet \
  --origin http://127.0.0.1:8766
```

Use a Python environment with Playwright and Chromium installed. With recent
Chrome, a virtual origin may require the test-only `--allow-local-network`
permission for a loopback/LAN fixture. `--browser-executable` selects an installed
Chromium/Chrome binary. The runner reports Worker errors and accepts an unchanged
restored tip as already durable; it does not require another redundant save event. This is a
headers-only browser cache; it does not introduce block, UTXO or wallet storage.
