# gRPC + Protobuf interface

A node built with `rpc=grpc` serves a typed, **read-only** query API over gRPC.
The schema is [`src/node/proto/kth/rpc/v1/node.proto`](../src/node/proto/kth/rpc/v1/node.proto)
(package `kth.rpc.v1`, service `NodeService`). It is a different frontend from
the [JSON-RPC server](json-rpc.md): the backend is chosen when the node is built,
and exactly one is compiled in.

## API (v1)

| Method | Request | Answer |
| --- | --- | --- |
| `GetChainInfo` | none | network, `block_height`, `best_block_hash`, `header_height` |
| `GetBlock` | `hash` (32 bytes) | `raw_block` (wire serialization), `height` |
| `GetTransaction` | `hash` (32 bytes) | `raw_transaction` (wire serialization), `confirmation` (absent for a mempool transaction) |

Hashes are **raw 32 bytes in internal (wire) order**, which is the *reverse* of
the hex that explorers and bitcoind-style RPC display. Blocks and transactions
come back exactly as the consensus wire format, not as hex or JSON. A hash that
is not 32 bytes is `INVALID_ARGUMENT`; the server never reverses, pads or
truncates.

| Status | Meaning |
| --- | --- |
| `OK` | success |
| `INVALID_ARGUMENT` | malformed request |
| `NOT_FOUND` | the node does not have it |
| `RESOURCE_EXHAUSTED` | at the in-flight limit (retry), or the answer exceeds the response limit |
| `UNAUTHENTICATED` | missing or wrong credentials |
| `UNAVAILABLE` | node shutting down or its chain is not serving |
| `ABORTED` | the chain tip moved while the answer was assembled (retry) |
| `CANCELLED` | the client cancelled, or the server shut down |
| `INTERNAL` | unexpected chain failure; the message names the operation |

Not in v1: mining, submitting transactions or blocks, subscriptions, streaming,
queries by height, address or script.

### How blocks are found

`block_chain::fetch_block(hash)` is a stub on this chain (it always reports not
found: block storage moved to flat files, issue #491), so `GetBlock` does not use
it. It resolves the block the way the chain serves bodies: hash → height →
`fetch_blocks_raw(height, height)`, the **active chain's** flat-file body, read on
the chain's priority pool. The bytes must hash to the requested hash (a block on a
side branch or without a stored body is `NOT_FOUND`), and the active chain must
still hold that hash after the read (otherwise `ABORTED`, retry). The stored wire
bytes are returned as they are. The size of a body is only known once read, so
the response limit is enforced after the read, not before the allocation.

`GetTransaction` still calls `fetch_transaction`, whose confirmed branch is a stub
too, so only **mempool** transactions are found until a by-hash transaction index
exists. `GetChainInfo` is fully live.

## Building

```sh
conan install . -o '&:rpc=grpc' -o '&:consensus=False' -s compiler.cppstd=23 ...
```

| Dependency | Pin | Notes |
| --- | --- | --- |
| `grpc` | `1.84.0` (recipe revision pinned) | host, and `tool_requires` for `grpc_cpp_plugin` |
| `protobuf` | `6.33.5` (revision pinned) | host, and `tool_requires` for `protoc` |
| `asio-grpc` | `3.7.0` (revision pinned) | header-only; `backend=asio`, i.e. the node's standalone asio |

Other backends do not pull any of this in. `protoc` and `grpc_cpp_plugin` come from
the *build* context, so cross-builds run native generators; CMake finds them on
`PATH` (set by Conan's build environment) or via `-DKTH_PROTOC=` /
`-DKTH_GRPC_CPP_PLUGIN=`. The schema is installed to `share/kth/proto/` and the
generated `kth/rpc/v1/node.pb.h` and `node.grpc.pb.h` to `include/`.

CI: `.github/workflows/grpc-backend.yml` is a dedicated variant that builds
`rpc=grpc` (with `consensus=False`), runs the gRPC suites, and `conan create`s the
package so `test_package` compiles a consumer against the installed generated
headers.

Known build constraints, with their reasons:

* **`rpc=grpc` with `consensus=True` is refused.** `grpc/1.84.0` requires
  `openssl <4` and does not compile against the `openssl/4.1.0` consensus pins
  (`grpc_tls_crl_provider.cc`: `X509_CRL_get_issuer` now returns a const pointer).
  The recipe fails with that explanation rather than repin OpenSSL.
* `asio-grpc` asks for `asio/[>=1.17 <2]`, which never matches the
  `1.38.3-kth.1` fork pre-release, so the recipe requires the fork with
  `force=True` for this backend only.

## Running

The existing settings apply; the backend is the compiled-in one:

```ini
rpc.enabled  = true          # off by default
rpc.bind     = 127.0.0.1     # loopback IP literal only
rpc.port     = 8332         # JSON-RPC's default; one backend per build, so no clash
rpc.user     = alice
rpc.password = secret
```

Authentication is HTTP-Basic-shaped call metadata:
`authorization: Basic base64(user:password)`. With no user/password configured the
node generates a token and writes `__cookie__:<token>` to `./.cookie`. The file
is created exclusively as a private temporary (owner read/write only, set and
verified before the secret is written), written and synced with every step
checked, then atomically renamed over `.cookie`; a symlink already there is
replaced, never written through. If any step fails the node does not start gRPC
(`start()` fails) rather than serve with a credential it could not store
privately. On Windows the mode bits do not exist; the file inherits its
directory's ACL.

The transport is **plaintext**, so it refuses to start on a non-loopback
`rpc.bind` (and on a missing credential, a taken port, or `rpc.enabled=true`
with an unusable address): an explicit start error, never a silent fallback or an
unauthenticated listener. Exposing it beyond the host needs TLS, which this
backend does not have yet; put it behind an authenticated tunnel meanwhile.

Try it with the smoke client built alongside the node:

```sh
kth-grpc-smoke-client --target 127.0.0.1:8332 --cookie .cookie
kth-grpc-smoke-client --user alice --password secret --block <displayed hash hex>
```

## Limits

| Limit | Default | Behaviour past it |
| --- | --- | --- |
| calls in flight | 16 | next call is refused at once with `RESOURCE_EXHAUSTED` (no queue) |
| request message | 4 KiB | `RESOURCE_EXHAUSTED` |
| response message | 64 MiB | `RESOURCE_EXHAUSTED` naming the limit (a larger block is not truncated) |
| shutdown grace | 1 s | running calls are then cancelled |

These come from `transport_options`; the node uses the defaults. There is no
connection cap or per-call deadline: a client that reads slowly holds one of the
in-flight slots, never a P2P or executor thread. Worst-case response memory is
in-flight × response limit. No performance claims are made; nothing has been
benchmarked.

## Architecture

* **`transport`** (`grpc_rpc/transport.hpp`) is the only code that touches gRPC
  and asio-grpc, behind a pimpl. It takes typed `std::function` handlers that
  return `asio::awaitable<expected<Response, status>>`; it knows nothing of the
  chain. That makes it testable with a real generated client on loopback.
* **Threads.** gRPC's own threads are internal. The transport adds two it owns
  and joins: one running the `agrpc::GrpcContext` (where handlers resume) and one
  that performs the blocking `grpc::Server::Shutdown`. `stop()` only signals;
  nothing blocks a node executor thread.
* **Chain access.** A handler validates its request on the transport thread, then
  `co_spawn`s the chain call onto the chain's executor (`block_chain::executor()`,
  which is the chain's priority pool) and awaits it. Chain state is never touched from a gRPC thread; the chain
  executor never waits on gRPC. Large blocks are serialized on the transport
  thread, not the chain executor.
* **Bounded admission.** Calls are counted lock-free before any work starts;
  past `max_in_flight` they are refused. That bound also caps the work queued
  on the chain executor, so no extra queue exists to overflow.
* **Cancellation and ownership.** Each handler races the call's own end
  (`wait_for_done`), so a client cancel or a shutdown past the grace period
  cancels the handler and the chain hop it awaits. Every hop is owned by the
  handler awaiting it; the registration coroutines complete only after all
  handlers do; nothing is detached.
* **Shutdown order** (`full_node`): `stop()` signals (also when only the network
  was stopped: `run()` stops the gRPC server when its core tasks end); `join()`
  runs on the owner's wind-down thread, never an executor thread, after `run()`
  completes, and joins the gRPC server **first** (its calls still need the network executor and the chain),
  then the network pool, then stops and closes the chain. Stopping before starting
  makes `start()` fail, and a transport that never started joins immediately.
