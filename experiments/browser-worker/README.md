# Browser Worker transport controls

This is an experimental transport/executor milestone, **not a completed browser
full node**. The existing C++ full-node and sync modules compile with the opt-in
build. The tests below do not run full sync orchestration, connect blocks to a
UTXO set, establish contextual consensus acceptance, or establish persistence.
No JavaScript implementation of BCH protocol or validation is used.

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
  -DCMAKE_BUILD_TYPE=Release -DBUILD_NODE_EXE=OFF -DBUILD_C_API=OFF \
  -DKTH_BROWSER_CONTROLS=ON
cmake --build "$BUILD" -j4
node "$BUILD/experiments/browser-worker/browser_configuration_control.js"
```

The configuration control runs the browser-compiled parser in Node, checks that
automatic inbound/outbound connections default to zero and DNS seeds are empty
on both mainnet and regtest, and verifies that the
pinned Asio channel wakes a receive when closed both before and after admission.
No cancellation call is required to wake a pending receive in that version.
The embedding application must still supply one manual peer and its bridge URL;
these operator-selected endpoints intentionally have no implicit default.

Copy the generated `browser_*_control.js` and `.wasm` files from
`$BUILD/experiments/browser-worker` beside this directory's index.html and
worker.js in a fresh serving directory. With Python 3 and websockets 15.0.1:

```sh
python fixtures.py all
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
and the stream, cap, EOF, select, terminal and lifecycle controls in Chromium.
It requires both successful completion and zero retained timers/streams, and
fails on Worker errors or a deadline. Logs and JSON observations are uploaded.

To run the same Worker checks locally after building the controls:

```sh
python -m pip install playwright==1.55.0 websockets==15.0.1
python -m playwright install chromium
python experiments/browser-worker/ci.py "$BUILD/experiments/browser-worker" \
    --report browser-results.json
```

The runner owns and closes its loopback fixtures. These checks do not require
an external peer or bridge and do not certify real-node interoperability or
full chain synchronization. The separate BCHN interop control remains manual.

For ordinary WASM compatibility, configure a separate build with browser_node
False, BUILD_C_API ON and KTH_LEGACY_API_CONTROL ON, build
`ordinary_wasm_api_control`, and run the resulting JS module with the toolchain's
Node executable. This tests the existing C API boundary; it does not replace the
complete generated JavaScript wrapper suite.

## Evidence and regression controls

This branch is a draft transport milestone. Earlier controls ran against a
working tree based on a8e656047a4142c6782a2b1e38317f3dfe00a010 with
Emscripten 6.0.13. Those observations do not certify this rebased commit.
The complete control set, including the latest terminal instrumentation, must
be rerun on the final review commit before readiness.

The terminal-state controls reproduced two defects in the initial adapter:
closure lost the saturation reason, and clean EOF discarded accepted buffered
bytes. `terminal-state.negative.patch` reconstructs that exact adapter
(SHA256 `1380046c16aaa2dfe1a8c68bbbaf5056af676296b284578f4e452c0c188b85e7`).
Apply it only to a disposable copy of browser_stream.hpp and compile the same
cap/EOF controls with that copy first in the include path. They fail for the
intended observations; the fixed header passes. This is regression evidence for
the new transport, not a claim that the unmodified base provided browser P2P.

The original proof.cpp also has a discriminating timeout cancellation mutation:
remove only emscripten_clear_timeout(timeout_id); the browser then emits
`UNEXPECTED deadline fired`. Its source SHA256 is
`47ff6900352a182ee7c8d5216205d60337548cf6a94d203fb3542a24aab99eb2`.

## Remaining integration blocker

The pinned UTXO-Z production geometry initially maps 2924 MiB across its full
mode containers. Its reference mode's 4 GiB size truncates to zero on wasm32.
The public API offers production open or test-only open_for_testing, without a
small deployment capacity profile or generation/mapped-byte budget. No test
geometry is substituted for production here. Bounded production browser storage
requires a reviewed dependency capacity policy/API and independent LMDB bounds.
The existing sync path also needs complete runtime/cancellation/CPU-affinity
validation before the opt-in build can be considered a working browser node.


## Headers-only integration control

`headers-sync-proof.cpp` exercises the `node::header_sync` library actor, which downloads consecutive `getheaders` batches through the WebSocket transport and passes them to the existing `header_organizer` and `header_index`. It opens no UTXO or block database. Checkpoint lists are cleared in this control so historical proof-of-work is checked too. A heavier accepted header branch is adopted through the organizer. The peer is stopped on success or failure, and the Worker reports timer/stream quiescence.

Build with `KTH_BROWSER_NODE` enabled:

```sh
cmake --build <build-directory> --target browser_headers_control
```

Serve `headers.html`, `headers-worker.js`, `browser_headers_control.js`, and `browser_headers_control.wasm` from one HTTP origin. Configure the Rust bridge to allow that exact origin and connect to a BCH peer. Select the matching network on the page. HTTPS deployments need WSS. The bridge selects the upstream TCP destination; the browser does not select arbitrary TCP hosts.

This is an integration control, not the application binding. The page reads progress reports from a C++ executable in a Worker. The final example still needs the generated lifecycle/status interface, application-level stop/reconnection controls and persistence decisions. The library actor owns the index and pending session; `request_stop()` is a request, and `run()` must settle before reset or disposal.

Observed locally on 2026-11-01 with real Chromium, Emscripten 6.0.13 and the Rust bridge:

- Isolated BCHN 29.0.0 regtest peer: validated height 3005, in batches of 2000 and 1005, then an empty response and zero pending timers/streams.
- Mainnet peer discovered through a configured BCH DNS seed: validated height 975537 from genesis, with checkpoint shortcuts disabled, then an empty response and zero pending timers/streams. This is one peer's observed tip, not a proof of global peer agreement or transaction validity.

The index reported 331679052 bytes at that mainnet height. This exceeds raw header storage because it includes the in-memory index and reserved capacity. The control caps the index at 1500000 entries and allows WASM memory growth; it is not yet a tuned persistent browser store. Reopening the page starts again from genesis.

A retained browser runner (`check-headers.py`, requiring Playwright and Chromium) verifies completion and quiescence and saves JSON plus a screenshot:

```sh
python check-headers.py --bridge ws://127.0.0.1:18768 \
    --network regtest --expected-height 3005 --output /tmp/headers-valid
```

The negative control `invalid-headers-proxy.py` sits between the Rust bridge and an isolated BCHN regtest peer. It changes the final header's target in the first nonempty batch to zero and recomputes the BCH message checksum. Thus framing and transport checks pass, while consensus validation must reject the header. Keep this fixture bound to loopback and separate from production connections:

```sh
python invalid-headers-proxy.py --upstream 19454 --listen 19455
# Configure a separate Rust bridge endpoint to forward to 127.0.0.1:19455.
python check-headers.py --bridge ws://127.0.0.1:18770 \
    --network regtest --expect-rejection --output /tmp/headers-invalid
```

The retained negative control reported `proof of work invalid`, did not announce a synchronized tip, and drained all timers and streams. This does not replace the remaining application lifecycle/cancellation controls.

The library actor also passed a real Chromium stop control against a peer that accepts TCP but never answers the BCH handshake: a stop request at 100 ms ended the pending run and drained all timers/streams within the five-second control deadline. The runner supports `--expect-stopped` for this fixture.

### Application binding control

The `headers-api.html` page uses the asynchronous `HeaderSync` application class,
including synchronous height/status observations and an asynchronous stop request.
It does not parse console output to determine chain progress.

Prepare the matching JS checkout (with its root and internal tool dependencies
installed):

```sh
python3 helpers/prepare-browser-headers.py --output /tmp/headers-app
```

Configure an existing KTH Emscripten browser build with `BUILD_C_API=ON`,
`KTH_HEADERS_GLUE_CPP=/tmp/headers-app/javascript/wasm/glue.cpp` and
`KTH_HEADERS_PRE_JS=/tmp/headers-app/pre.js`. Build the
`browser_headers_api_control` target. The files must be accessible to the build
host; when using different hosts, transfer the complete prepared output first.
Serve its `.js` and `.wasm` outputs alongside `headers-api.html` and
`headers-api-worker.js`. Supply the bridge URL and matching network in the page.

The stop request acknowledges cancellation admission. The worker awaits the run's
terminal completion before disposing the client. Reset and disposal are refused
while a run is pending; queries and stop remain available. The accompanying
contract and reconnect workers exercise these lifetime rules against controlled
peers. Mainnet observations establish header interoperability, not SPV wallet
completeness, block validation, peer agreement, or durable storage.

Construction returns a C status and writes the owned handle only on success.
A failure clears the output slot. Allocation exceptions map to
`kth_ec_out_of_memory`; other construction exceptions map to
`kth_ec_unhandled_exception`. The bridge URL must be nonempty and the header
capacity must be between 1 and 2000000. The application constructor translates
these statuses into a recoverable error. The `construction` scenario of
`check-headers-api.py` verifies invalid configurations and subsequent successful
construction in the same Worker.
