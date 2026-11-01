# Cap'n Proto RPC

A typed, read-only node API over [Cap'n Proto RPC](https://capnproto.org/rpc.html):
real capnp RPC (promises, capabilities, cancellation) over capnp serialization,
not capnp bytes inside a custom protocol. It is one of the build-time RPC
backends (see [json-rpc.md](json-rpc.md)); exactly one is compiled in.

## Building

```
conan install . -o '&:rpc=capnproto' ...        # Conan
cmake ... -DKTH_RPC_BACKEND=capnproto ...       # CMake
```

* `capnproto/1.5.0` (recipe revision pinned in `conanfile.py` and `conan.lock`)
  is a host requirement for the runtime and a `tool_requires` for the schema
  compiler (`capnp`, `capnpc-c++`). The generator therefore runs on the build
  machine when cross compiling; `CapnProtoMacros.cmake` takes it from `PATH`
  when `CMAKE_CROSSCOMPILING` is set. TLS and gzip are disabled (`with_openssl`,
  `with_zlib`), which also keeps the package's `openssl <4` range out of a graph
  that pins openssl 4.x.
* `KTH_WITH_RPC` still means "JSON-RPC compiled in"; this backend defines
  `KTH_WITH_CAPNPROTO_RPC`. `jsonrpc`, `none` and the reserved `grpc` are unchanged.
* The schema is `src/node/schema/kth/rpc/v1/chain.capnp`, namespace
  `kth::rpc::v1`. It is installed to `share/kth/schema/kth/rpc/v1/`, and the
  generated `kth/rpc/v1/chain.capnp.h` to `include/`. The Conan `node`
  component requires `capnproto::capnp-rpc`.
* Not built for WebAssembly.

## Running

```
rpc.enabled  = true
rpc.bind     = 127.0.0.1     # loopback only, see below
rpc.port     = 8332
rpc.user     = alice
rpc.password = secret
```

The same settings as JSON-RPC, same defaults (off at runtime).

**Authentication.** The bootstrap capability is a `Gateway` with a single
method, `login(user, password)`, which returns the `Chain` capability. Nothing
can be read before it. Credentials are `rpc.user`/`rpc.password`; if both are
empty a `__cookie__` token is generated and used as the password for user
`__cookie__`. It is written when `run()` starts, to `./.cookie` (`server_options::cookie_path`):
a private temporary file is created exclusively next to it (mode 0600 at
creation, no symlink following), written, fsynced and closed with every step
checked, then renamed over `.cookie`, which replaces a pre-existing file or
symlink instead of writing through it; the temporary file is removed on failure.
If any step fails the server logs the error and does **not** listen. On Windows
the file inherits the directory ACL (the mode bits are not enforceable there). Configured credentials
are never ignored. Wrong passwords are compared in constant time; three failures
or no login within 10 s close the connection.

**Loopback only.** There is no TLS in this backend and passwords cross the socket
in the clear, so the server refuses (logs an error and does not listen) any
non-loopback `rpc.bind`. Remote clients should tunnel (SSH, stunnel...).

## API (v1)

| Call | Returns |
| --- | --- |
| `getChainInfo()` | `blockHeight` (connected tip), `headerHeight`, `bestBlockHash` |
| `getBlock(hash)` | `hash`, `height`, `wire` (the block as relayed on the P2P network) |
| `getTransaction(hash)` | `hash`, `wire`, `location` = `mempool` or `confirmed{blockHeight,index}` |

* **Hash byte order.** A hash is exactly 32 raw bytes in the node's internal
  order, i.e. the order inside serialized headers and transactions. That is the
  *reverse* of the hex that explorers and `getblockhash` print. A displayed hash
  must be hex-decoded and reversed (the example does this). No hex anywhere.
* **Errors are data.** Each result is `union { ok; error }` with a typed
  `ErrorCode`: `invalidArgument` (e.g. a hash that is not 32 bytes),
  `notFound`, `resourceExhausted` (overload or oversized result),
  `unavailable` (stopping, cancelled), `unauthenticated`, `internal`. Cap'n Proto
  exceptions (`DISCONNECTED`, ...) only report transport problems: a closed
  connection, a message above the size limit.
* `getBlock` serves only validated blocks of the active chain at or below the
  connected tip, from the flat-file block store (`fetch_blocks_raw`). The read is
  blocking disk I/O, so it runs on the chain's own worker pool while the owner
  strand is suspended awaiting it (the await owns the frame, so cancellation
  cannot free state under the running read). After the read the bytes' header
  hash must equal the requested hash and that height must still be on the active
  chain, otherwise `notFound`: a reorganization during the read cannot return a
  different block. A stopping chain gives `unavailable`. The chain's
  by-hash block/transaction fetches are currently stubs returning `not_found`
  (the LMDB stores were removed), so `getBlock` resolves hash -> height via the
  header index instead.
  The genesis block's body is not in the flat-file store of a fresh node, so
  `getBlock(genesis)` reports `notFound` ("block data not stored") until it is.
* `getTransaction` answers from the mempool today. It already calls
  `fetch_transaction` first, which reports `notFound` until a transaction index
  exists (issue #491), so confirmed lookups start working without an API change.
  `fetch_transaction` yields `(transaction, index, height)` (as the C API uses
  it); `to_transaction_data` maps that, with a unit test on a synthetic tuple.
  The real confirmed path itself is unexercised because the chain stub never
  returns a confirmed transaction.

## Limits

All bounded, none queue without limit (`capnp_rpc::limits`, defaults in parentheses):

| Limit | Behavior when exceeded |
| --- | --- |
| open connections (16) | the new connection is closed |
| calls in flight, total (16) and per connection (4) | the call is answered `resourceExhausted` at once, nothing is queued |
| incoming message (16 KiB) | the connection is closed (Cap'n Proto's own traversal limit) |
| block/transaction payload (48 MiB) | `resourceExhausted`; clients must accept at least this size (`ReaderOptions`) |
| login (10 s, 3 failures) | the connection is closed |

**There is no enforced total memory bound.** The in-flight and message limits
bound the number of operations and the size of what is *sent*, not what the
reader allocates: `fetch_blocks_raw` reads a whole block into memory before the
payload limit is checked, so a block larger than the limit is allocated
transiently and then refused with `resourceExhausted`, and up to `max in flight`
such reads can run at once. Accepted payloads are copied once more into the
outgoing message. Size the limits for the machine. No performance claims are made;
none were measured.

## Architecture and why it is safe

Cap'n Proto's RPC is built on KJ, whose promises, capabilities and streams belong
to the thread that runs their event loop. The node's state belongs to asio
strands. The two runtimes are not mixed:

```
 client ─TCP─▶ KJ thread (owned std::thread)        asio owner strand (node executor)
               accept, capnp RPC, login,            table of running operations,
               admission, message limits            the only caller of `reader`
                  │  request_msg / cancel_msg           ▲
                  └──────── bounded concurrent_channel ─┘   (try_send, never waits)
                  ▲  reply (plain value)
                  └── kj CrossThreadPromiseFulfiller, one per call (fulfill() is thread-safe)
```

* **KJ thread** (`kj_service.cpp`): never touches chain state or any asio object
  except `try_send` on the mailbox. It validates hashes, enforces connection and
  in-flight limits, and turns a call into a `request_msg` carrying a
  cross-thread fulfiller. The call's server-side promise waits on that, so the
  KJ loop never blocks.
* **Owner strand** (`server.cpp`): the only place that calls the typed `reader`
  (which in production calls `block_chain`). Each operation is a coroutine with
  its own cancellation signal, tracked in a table. Requests are admitted by the
  KJ thread before they are sent, so the table and the mailbox are bounded.
* **Cancellation** is routed to each operation's owner: when a call's promise is
  dropped (client cancel, connection close, transport shutdown) a guard posts a
  `cancel_msg`; the owner emits that operation's cancellation signal. An
  operation cancelled before it starts never calls the reader. What a handler
  cannot interrupt finishes and its result is discarded; its admission slot is
  held until then, so overload accounting stays honest.
* **Shutdown**: `stop()` (valid before `run()`; then `run()` starts nothing)
  makes the owner cancel every operation and ask the KJ loop to stop through a
  cross-thread fulfiller. The KJ thread destroys all connections (clients see
  `DISCONNECTED`), posts a final wake and returns. The owner keeps draining the
  mailbox until every operation has finished and the KJ thread has exited, then
  joins it; that join waits only for thread teardown. Nothing is detached. The
  destructor also stops and joins, so abandoning `run()` cannot leave a thread.
* P2P executor threads are never blocked: the owner strand only awaits; the KJ
  thread has its own loop and does not poll or wait on node executors.
* Results cross the boundary as plain values (`data_chunk`, hashes), moved, never
  shared. Cap'n Proto messages are built on the KJ thread only.

Cost of the design: one extra thread, one extra copy of each payload into the
capnp message, and a documented foreign runtime (KJ) in the process. A KJ/asio
adapter would avoid the thread but make KJ objects share the node executor's
threads, which KJ does not support.

## Example

`src/node/examples/capnp_chain_client.cpp` (target `kth_capnp_chain_client`,
built with the node tests):

```
kth_capnp_chain_client 8332 alice secret chain-info
kth_capnp_chain_client 8332 alice secret block <displayed-hex-hash>
```

## Tests

`src/node/test/capnp_rpc.cpp` runs generated clients against the real server over
loopback: typed success, malformed hashes, handler failures and missing objects,
payload limit, login/lockout/timeouts, request-size limit, bounded admission
(total, per connection, connections), cancellation by dropped call and by closed
connection, stop-before-run, shutdown with running work, non-loopback refusal,
and the real `chain_reader` over a genesis chain. Handlers are injected through
the `capnp_rpc::reader` interface.
