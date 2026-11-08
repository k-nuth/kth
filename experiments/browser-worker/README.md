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

## Controls

`ci.py` runs the controls below as real WASM in Chromium Workers against the
loopback fixtures in `fixtures.py` (byte-stream fixtures, not BCH peers):

| Control | What it checks |
| --- | --- |
| `stream` | echo through the stream, partial reads, writes split at the frame cap |
| `cap` | 1000 single-byte messages are kept; more than 1 MiB unread ends the session with `no_buffer_space` |
| `eof` | a clean close delivers every accepted byte, then EOF |
| `select` | channel selection deadlines fire through the runtime's timer |
| `terminal` | a terminal error is kept for every later operation |

Build (Emscripten 6, `ci_utils/conan/profiles/ems6`):

```sh
conan install . --output-folder=build/browser-deps --lockfile=conan-wasm.lock --lockfile-partial \
  -pr:h ci_utils/conan/profiles/ems6 --build=missing \
  -o browser_node=True -o tests=False -o rpc=none -o consensus=False
source build/browser-deps/build/Release/generators/conanbuild.sh
cmake -S . -B build/browser -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build/browser-deps/build/Release/generators/conan_toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_NODE_EXE=OFF -DBUILD_C_API=OFF \
  -DKTH_BROWSER_NODE=ON -DKTH_BROWSER_CONTROLS=ON
cmake --build build/browser --target browser_stream_control browser_cap_control \
  browser_eof_control browser_select_control browser_terminal_control
python experiments/browser-worker/ci.py build/browser/experiments/browser-worker
```
