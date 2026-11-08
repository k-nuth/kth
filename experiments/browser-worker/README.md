# Browser Worker runtime and transport

An experimental profile (`browser_node`, Emscripten only) that runs Knuth's
standalone-Asio coroutines in a browser Worker and gives `peer_session` a
WebSocket byte stream in place of a TCP socket. A WebSocket-to-TCP bridge (not
part of this repository) relays the BCH byte stream to a peer.

* `kth::browser::runtime` (`browser_runtime.hpp`) drains an `io_context`
  cooperatively from the Worker's event loop. One per Worker: a second is
  refused. `kth::browser::timer` is the timer it wakes for; `kth::steady_timer`
  (`platform_timer.hpp`) is that timer in a browser node and `asio::steady_timer`
  elsewhere, and channel selections use it too (asio 1.38.3-kth.3). Nothing
  wakes the runtime for a raw Asio timer: `scripts/check_browser_timers.py`
  refuses them in code a browser node compiles.
* `kth::network::browser_stream` is the byte stream. It admits at most 1 MiB
  unread, in messages of at most 65536 bytes; past that the session ends with
  `no_buffer_space` (continuing after dropping a byte would corrupt framing).
  Many small messages are coalesced: the bound is on bytes, not messages.

## Header synchronization

`kth::node::header_sync` (`header_sync.hpp`) is a headers-only client for one
peer behind a bridge: it validates headers with the header index and the
contextual header organizer, opens no block or UTXO database, and runs on the
Worker's runtime (the application's, when it runs one). `run()` ends at the
peer's tip; `follow()` stays on the same connection, answers pings, fetches
announced blocks' headers and refreshes every 120 s; `request_stop()` and
`request_refresh()` only ask. It accepts messages of at most 256 KiB (a full
`headers` and room to spare). The C API (`kth/capi/node/header_sync.h`) never
lets an exception cross it: destruct and reset are refused while a run is
pending.

## Controls

`ci.py` runs the controls below as real WASM in Chromium Workers against the
loopback fixtures in `fixtures.py`:

| Control | What it checks |
| --- | --- |
| `stream` | echo through the stream, partial reads, writes split at the frame cap |
| `cap` | 1000 single-byte messages are kept; more than 1 MiB unread ends the session with `no_buffer_space` |
| `eof` | a clean close delivers every accepted byte, then EOF |
| `select` | channel selection deadlines fire through the runtime's timer |
| `terminal` | a terminal error is kept for every later operation |
| `headers` | against a scripted BCH regtest peer (`fixtures.py`): 2500 headers in two batches; a header with invalid proof of work ends the session; a message declaring more than 256 KiB ends it at its heading; `follow()` answers a ping, fetches an announced block on the same connection and stops; the C API refuses destruct and reset while it runs |

Build (Emscripten 6, `ci_utils/conan/profiles/ems6`):

```sh
conan install . --output-folder=build/browser-deps --lockfile=conan-wasm.lock --lockfile-partial \
  -pr:h ci_utils/conan/profiles/ems6 --build=missing \
  -o browser_node=True -o tests=False -o rpc=none -o consensus=False
source build/browser-deps/build/Release/generators/conanbuild.sh
cmake -S . -B build/browser -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/browser-deps/build/Release/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_NODE_EXE=OFF -DBUILD_C_API=ON \
  -DKTH_BROWSER_NODE=ON -DKTH_BROWSER_CONTROLS=ON
cmake --build build/browser --target browser_stream_control browser_cap_control \
  browser_eof_control browser_select_control browser_terminal_control browser_headers_control
python experiments/browser-worker/ci.py build/browser/experiments/browser-worker
```
