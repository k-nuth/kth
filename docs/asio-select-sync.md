# Channel selection in the block sync

Knuth's standalone Asio is the Knuth fork `asio/1.38.3-kth.1`
(recipe revision `350bc1a8f6021cd9973a02957d9972c4`, source commit
`4b04892f2d02d5c9e04b79b428d8fe905a927a48`, PR <https://github.com/k-nuth/asio/pull/2>).
It is upstream `asio-1-38-2` plus one header, `asio/experimental/channel_select.hpp`,
and one opt-in trait in `asio/impl/awaitable.hpp`. `ASIO_VERSION` is still 103802,
the CMake target is still `asio::asio`, and every existing `experimental::channel`
and `concurrent_channel` in Knuth is unchanged. Only the pieces listed below use
the new primitive.

The reviewed recipe and header-only package are published on the `kth` Conan
remote (`https://packages.kth.cash/api/`). Both native and WebAssembly lockfiles
pin the same recipe revision.

## What the primitive gives that `||` did not

A `select_channel<T>` is a bounded, thread-safe channel with no executor and no
`async_send`/`async_receive`. It is used through `try_send`/`try_receive`,
one-shot `make_select(cases...).async_wait(ex, options, stop_token, token)` and
task-owned `make_selector(ex, options, stop_token, cases...)` loops. A selection
waits on any number of `receive(ch)`, `send(ch, value)` and `after(time_point)`
cases and commits exactly one of them:

* a losing receive consumes nothing — the message stays in its channel;
* a losing or failed send keeps its payload — it is still the caller's;
* `close()` is the shutdown signal: pending sends complete with `broken_pipe`,
  empty receives with `eof`, and what is buffered can still be drained;
* a `std::stop_token` cancels a wait with `operation_aborted`, from any thread,
  even before the wait starts.

With `receive || timer`, the losing receive could complete with a message that
nobody handles. The chunk hand-over also used repeated `try_send` calls to wait
for capacity, although the old Asio channel itself supports suspended sends.
Selection replaces those application patterns and explicitly returns unsent payloads.

## Where it is used, and who owns what

### The fast path: download worker → `fast_validation_task`

`fast_validation_input_channel` is a `select_channel<fast_validation_input>`
(capacity 256). Each `block_download_task` owns one selector for its whole life
with a `send` case on it and an `after` case, armed with the chunk coordinator's
stop token. Per chunk it emplaces the chunk as the send payload and waits
(`hand_over_chunk` in `block_tasks.cpp`). Exactly one of these happens:

| outcome | who owns the chunk afterwards | what the worker does |
|---|---|---|
| the send wins | the channel (or the validation task's pending receive) | `chunk_completed`, next chunk |
| the `after` case wins (every 10 s) | still the worker | re-reads the coordinator: stopped → drop and `chunk_failed`; chain switched → drop and `chunk_failed`; otherwise logs and waits again |
| `operation_aborted` (coordinator stop token) | still the worker | drop, `chunk_failed`, leave the loop |
| `broken_pipe` (channel closed at shutdown) | still the worker | drop, `chunk_failed`, leave the loop — nothing will ever take a chunk again |

The in-flight bound is explicit: the channel's capacity plus one chunk per
worker. No copy per attempt, no poll, and no give-up that throws away blocks
already downloaded (the previous code retried `try_send` 600 times at 100 ms,
copying the chunk each time, then failed the chunk; after the channel was closed
it would re-claim and re-download the same chunk every 60 s).

A peer that stops while its chunk is held does **not** forfeit the chunk: the
blocks are valid, the worker delivers them and only then leaves. The peer's
reader is not behind the suspended worker: the download worker is a coroutine
of its own, and the session's read loop keeps delivering (a control sends a
ping through a real session while the worker is suspended).

`fast_validation_task` is the one consumer: a task-owned selector with a
single receive case. Every receive is what resumes a suspended worker. After
`close()` it drains what is buffered and ends on `eof`.

### The download supervisor

`block_download_input_channel` and `block_download_task_output_channel` are
`select_channel`s. The supervisor is one loop over one selector with three
cases, **biased** in this order: `receive(input)` (stop, range, peers), the
10 s timeout tick (`after`), `receive(task_output)` (blocks on the old path,
`task_ended`, performance). Control before data: a stop or a new range is
attended within one wait however full the workers' output is; this gives control traffic priority. Sustained control traffic can delay worker
reports; this priority policy does not provide a general starvation bound.

This replaced a timer task and two bridge tasks feeding a unified 512-entry
queue. What the old shape preserved is preserved directly: per-source FIFO
(each channel is FIFO, one message per wait), `task_ended` and stop never
dropped (there is no intermediate queue to be full), the timeout cadence (an
`after` case re-armed per tick, instead of a timer task whose `try_send` could
be refused by a full queue — which also ended that task for good), and one
coroutine touching the coordinator and the ownership bookkeeping with nothing
between a start and its record (#652). What is not preserved is the
cross-source interleaving of the old queue, which was an artefact of bridge
scheduling and retry delays, not a contract.

A worker's `task_ended` is a suspended send with no deadline and no stop token:
a full channel is waited out. The only way it is not delivered is the
supervisor having closed `task_output`, which it does after its loop, before
joining its workers — so a worker suspended on its report completes with
`broken_pipe` and ends, and the join completes.

Shutdown order: loop exits (stop request or input `eof`) → `coordinator->stop()`
(flag, then its stop token) → `task_output.close()` → `tasks.join()` →
coordinator destroyed → selector destroyed with the frame, after its last wait.

### The coordinator's range request

`send_block_range` is a suspended send armed with the network's stop token:
it commits in the caller's frame when the channel has room, suspends with the
request still its own when it is full, and ends on the supervisor's receive, on
the channel's `close()` (`broken_pipe`) or on the network's stop
(`operation_aborted`). Nothing polls and nothing is rebuilt.

### Per-request response channels of a peer

`peer_session::response_channel` (`block_responses()`, `addr_responses()`) is a
`select_channel<raw_message>`. `request_blocks_batch` owns one selector for the
whole batch — a receive case and an `after` case at the batch deadline — so a
block already buffered is taken in the requester's frame without suspending,
and a deadline that fires with a block ready does not consume it.
`request_addresses` is a one-shot selection of the same shape. The dispatcher
in `p2p_node::run_peer_protocols` hands responses in with a suspended send; a
send that fails (the session closed the channel) keeps its payload, which is
taken back so the message still reaches the generic dispatcher as before.

`peer_session::stop()` closes both channels and drains them; `~peer_session`
closes them again (idempotent). A selection keeps the channel state alive
without borrowing the session object.

### The network's stop as an event

`p2p_node::stop_token()` is stopped by `p2p_node::stop()`, once, before any
channel is closed, whatever lifecycle state the stop found. The sync layer's
`peer_provider` waits on its (now `select_channel`) input with that token, and
the coordinator's stop bridge waits on the token alone instead of polling
`stopped()` every 100 ms. The peer-event bridge still races the node's
`concurrent_channel` against a 500 ms timer: that channel belongs to the node
and was not converted.

## Limits that stay explicit

* `select_channel` bounds buffered messages (capacity) and wait registrations
  per channel (`pending_limit`, 1024 by default, counting senders and
  receivers together). It does not bound how many tasks may be suspended, nor
  the bytes they hold. In the fast path those are: capacity 256 chunks, plus
  one chunk per download worker (one per connected download peer), plus
  whatever `block_storage_input` (still a `concurrent_channel`, 128) holds.
  A registration refused by `pending_limit` completes with `no_buffer_space`
  and the payload returned; it is logged like any other failed selection.
* Payload types must be nothrow-move-constructible and must not touch a channel
  from their move or destructor. `downloaded_chunk`, the variants carrying it
  and `raw_message` all are.
* A selector outlives its waits and is used by one task at a time; every
  selector here is a local of the coroutine that uses it, destroyed with the
  frame after the last wait and after everything it started has been joined.
* GCC 15 and 16.1 never destroy a structured binding taken directly from a
  `co_await` of a tuple that owns a payload and lives across a later
  suspension (`csp/README.md`, "named tuple storage"). GCC 16.2 fixes it and
  is the minimum kth builds with; the configure step also runs the pattern and
  refuses a compiler that still retains it (`cmake/KthCompilerChecks.cmake`).
  The one-shot selections here store their result in a named `completion` and
  bind it by reference, which is correct with any compiler.

### The inbound message channel of a peer

`peer_session::messages()` (`inbound_`) is a `select_channel<raw_message>`
(capacity 100). The read loop hands each message in with a suspended send
(broken_pipe once `stop()` has closed the channel), and every consumer takes
messages through `peer_session::receive(deadline)`: one selection with a
`receive` case and an `after` case, so the handshake, the ping/pong loop,
`wait_for_message`, `run_peer_protocols` and the seeding loop no longer race
`async_receive` against a timer with `||`. This closed a crash: a parallel
group emits the cancellation of its losing branch from the winning branch's
completion handler, with no synchronisation against the other branch
completing on another thread, and the cancellation handler object (asio's
per-thread recycling allocator) was freed while being read (SIGBUS in
`cancellation_slot::clear()`, seen on arm64 during chipnet sync, the same
race as the earlier ASAN report in `peer_session.cpp`). A selection commits
one case under the channel's lock and has nothing to cancel across threads.

The node's peer-event channel (`p2p_node::peer_events()`) is a
`select_channel<peer_notification>` for the same reason; the orchestrator's
bridge waits on it with the network's stop token instead of a 500 ms timer.
The two remaining timeouts on sockets use asio's own per-operation
cancellation (`cancel_after` on the connect, `cancel_at` on the direct
handshake reads), and the per-peer lifecycle coroutine runs on the session's
strand, so `run()`'s own `||` of its four loops completes on one strand.

## Not converted, and why

* `outbound_`: a `concurrent_channel` bound to the session's strand, used by
  the write loop and `send()` only (no `||`).
* The sync coordinator's unified event channel and the header pipeline: many
  producers, one consumer, no `||`; nothing to gain from a selection there.
* `block_storage_input`, `chunk_validated`, `validated_headers`: still
  `concurrent_channel`s with `try_send_with_retry`. The storage task sits
  behind the reorg barrier; its 50 ms pause polls and the barrier itself were
  left alone deliberately.
* `coordinator_utxo_bridge`: a 1 s poll of the store by design (see its
  comment), not a channel wait.
* `task_group::join()`: a 1 ms poll of the active count; unrelated to channels.
