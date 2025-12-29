# Structured Concurrency Design for Knuth Network Layer

> **IMPORTANT**: This document is the source of truth for the network layer refactoring.
> Always read this document at the start of each session to understand the design goals.
> Location: `docs/design/STRUCTURED_CONCURRENCY_DESIGN.md`

## Overview

This document describes the refactoring of the Knuth network layer from "fire-and-forget"
coroutines (`::asio::detached`) to a structured concurrency model inspired by Go channels
and Kotlin coroutines.

## Design Principles

1. **No `::asio::detached`** except at the top-level entry point
2. **All coroutines must be awaited** - parent waits for all children
3. **Channel-based coordination** - Go-style message passing
4. **Separate IO and CPU executors** - maximize parallelism
5. **Graceful shutdown by design** - no race conditions

## Current Problems

```cpp
// PROBLEM: Fire-and-forget coroutines
::asio::co_spawn(executor, peer->run(), ::asio::detached);  // Lost track!
::asio::co_spawn(executor, run_peer_protocols(peer), ::asio::detached);  // Lost track!

// PROBLEM: Race condition on shutdown
if (stopped()) { co_return; }  // Check here...
co_await some_async_op();      // ...but stop() called here = crash
```

## Target Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           executor::run_async()                          │
│                         (single detached entry point)                    │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                         full_node::run()                           │  │
│  │  co_await (                                                        │  │
│  │      run_blockchain_subscriber() &&                                │  │
│  │      network_.run() &&                                             │  │
│  │      run_sync()                                                    │  │
│  │  );                                                                │  │
│  │                              │                                     │  │
│  │                              ▼                                     │  │
│  │  ┌─────────────────────────────────────────────────────────────┐  │  │
│  │  │                      p2p_node::run()                         │  │  │
│  │  │  co_await (                                                  │  │  │
│  │  │      run_inbound() &&                                        │  │  │
│  │  │      run_outbound() &&                                       │  │  │
│  │  │      peer_supervisor()   ← manages all peer lifecycles       │  │  │
│  │  │  );                                                          │  │  │
│  │  └─────────────────────────────────────────────────────────────┘  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

## Key Components

### 1. task_group (Nursery Pattern)

New class to manage dynamic tasks without detached:

```cpp
// File: src/infrastructure/include/kth/infrastructure/utility/task_group.hpp

class task_group {
public:
    explicit task_group(::asio::any_io_executor executor);

    // Spawn a task - tracked automatically
    template<typename Coro>
    void spawn(Coro&& coro);

    // Spawn and get a handle to cancel later
    template<typename Coro>
    task_handle spawn_with_handle(Coro&& coro);

    // Wait for ALL tasks to complete
    ::asio::awaitable<void> join();

    // Cancel all tasks and wait
    ::asio::awaitable<void> cancel_and_join();

    // Number of active tasks
    size_t active_count() const;

private:
    ::asio::any_io_executor executor_;
    std::atomic<size_t> active_count_{0};
    ::asio::experimental::channel<void()> done_signal_;
};
```

### 2. peer_supervisor

Centralized peer lifecycle management:

```cpp
// In p2p_node

::asio::awaitable<void> peer_supervisor() {
    task_group peer_tasks(pool_.get_executor());

    while (!stopped_) {
        // Wait for new peer OR shutdown
        auto event = co_await (
            new_peer_channel_.async_receive(::asio::use_awaitable) ||
            stop_signal_.async_receive(::asio::use_awaitable)
        );

        if (event.index() == 1) break;  // shutdown

        auto [peer, direction] = std::get<0>(event);

        peer_tasks.spawn([this, peer]() -> ::asio::awaitable<void> {
            co_await peer->run_full(dispatcher_);
            co_await manager_.remove(peer);
            spdlog::debug("[p2p_node] Peer {} task completed", peer->authority());
        });
    }

    // CRITICAL: Wait for all peer tasks to finish
    co_await peer_tasks.join();
}
```

### 3. Unified peer_session::run_full()

Single coroutine for entire peer lifecycle:

```cpp
// In peer_session

::asio::awaitable<void> run_full(message_dispatcher& dispatcher) {
    // All loops run in parallel, all must complete
    co_await (
        read_loop() &&
        write_loop() &&
        run_protocols(dispatcher) &&
        inactivity_monitor() &&
        expiration_monitor()
    );
}
```

### 4. Channel-based Coordination

```cpp
// New channels in p2p_node
struct {
    // New peers to be supervised
    concurrent_channel<void(std::error_code, peer_event)> new_peer_channel_;

    // Shutdown signal
    concurrent_channel<void()> stop_signal_;

} channels_;

// peer_event structure
struct peer_event {
    peer_session::ptr peer;
    enum class direction { inbound, outbound } dir;
};
```

### 5. Separate CPU Executor

```cpp
// For CPU-bound work (validation, etc.)
class cpu_executor {
public:
    explicit cpu_executor(size_t threads = std::thread::hardware_concurrency());

    template<typename F>
    auto post(F&& work) -> ::asio::awaitable<std::invoke_result_t<F>>;

    void stop();
    void join();

private:
    ::asio::thread_pool pool_;
};
```

## Implementation Phases

### Phase 1: Infrastructure ✅ COMPLETE
- [x] Make `run_inbound()` and `run_outbound()` awaited in `p2p_node::run()`
- [x] Make `full_node::run()` use `&&` operator for parallel tasks
- [x] Create `task_group` class (`src/infrastructure/.../utility/task_group.hpp`)
- [x] Create `cpu_executor` class (`src/infrastructure/.../utility/cpu_executor.hpp`)

### Phase 2: Peer Lifecycle Refactoring ✅ COMPLETE
- [x] Add `new_peer_channel_` and `stop_signal_` to p2p_node
- [x] Implement `peer_supervisor()` with task_group
- [x] Modify `connect()` to send peer_event to channel with response channel
- [x] Modify `run_inbound()` to send peer_event to channel
- [x] Eliminate `peer->run()` detached spawns using `&&` operator pattern
  - supervisor does: `peer->run() && (handshake && add && protocols)`
  - connect() waits for handshake result via response channel

### Phase 3: Connection Management ✅ COMPLETE
- [x] Refactor `run_inbound()` to send new peers to channel (done in Phase 2)
- [x] Refactor `maintain_outbound_connections()` to use task_group for parallel connects
- [x] Refactor seeding to use task_group

### Phase 4: Cleanup ✅ COMPLETE
- [x] Fix `stop_all()` to NOT clear peers_ map (let remove() handle cleanup)
- [x] Remove `stopped()` check from `remove()` and `remove_by_nonce()`
- [x] Remove all `::asio::detached` from p2p_node.cpp (using `&&` operator pattern)
- [ ] Remove remaining `stopped()` checks from query methods (optional - they're defensive)
- [ ] Update tests

### Phase 5: CPU Parallelism
- [ ] Integrate `cpu_executor` for block validation
- [ ] Add parallel header validation
- [ ] Add parallel transaction validation

## Files to Modify

| File | Changes |
|------|---------|
| `src/infrastructure/.../utility/task_group.hpp` | **DONE** - task_group class |
| `src/infrastructure/.../utility/cpu_executor.hpp` | **DONE** - CPU executor |
| `src/network/include/kth/network/p2p_node.hpp` | Add channels, peer_supervisor |
| `src/network/src/p2p_node.cpp` | Refactor connect/inbound/outbound |
| `src/network/include/kth/network/peer_session.hpp` | Add run_full() |
| `src/network/src/peer_session.cpp` | Implement run_full() |
| `src/network/src/peer_manager.cpp` | Remove stopped() checks |
| `src/node/src/full_node.cpp` | Already done - uses && |

## Message Flow (Go-style)

```
                    ┌─────────────────┐
                    │  run_inbound()  │
                    │  (accept loop)  │
                    └────────┬────────┘
                             │ new peer
                             ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│  run_outbound() │───▶│ new_peer_channel│◀───│   connect()     │
│ (connect loop)  │    └────────┬────────┘    │  (manual peer)  │
└─────────────────┘             │             └─────────────────┘
                                ▼
                    ┌─────────────────────┐
                    │  peer_supervisor()  │
                    │                     │
                    │  task_group {       │
                    │    peer1.run_full() │
                    │    peer2.run_full() │
                    │    peer3.run_full() │
                    │    ...              │
                    │  }                  │
                    └─────────────────────┘
                                │
                                ▼ on stop()
                    ┌─────────────────────┐
                    │ task_group.join()   │
                    │ (wait for all)      │
                    └─────────────────────┘
```

## Shutdown Sequence

```
1. stop() called
   │
   ├─▶ stop_signal_.send()           // Signal supervisor to stop accepting
   ├─▶ acceptor_.close()             // Stop accepting new connections
   └─▶ manager_.stop_all()           // Signal all peers to stop

2. peer_supervisor() receives stop_signal_
   │
   └─▶ Exits while loop, calls peer_tasks.join()
       │
       └─▶ Waits for ALL peer->run_full() to complete
           │
           └─▶ Each peer: socket closes → loops exit → run_full() returns

3. run_inbound() exits (acceptor closed)

4. run_outbound() exits (stopped_ = true)

5. p2p_node::run() returns (all && branches completed)

6. full_node::run() returns

7. executor cleans up io_context

NO RACE CONDITIONS - everything is awaited!
```

## Testing Strategy

1. **Unit tests for task_group**
   - spawn/join behavior
   - cancel_and_join behavior
   - exception propagation

2. **Integration tests**
   - Clean shutdown with active peers
   - Shutdown during connection attempts
   - Shutdown during handshake

3. **Stress tests**
   - Many concurrent connections
   - Rapid connect/disconnect cycles
   - Shutdown under load

## References

- [Trio Structured Concurrency](https://trio.readthedocs.io/en/stable/reference-core.html#nurseries-and-spawning)
- [Kotlin Structured Concurrency](https://kotlinlang.org/docs/coroutines-basics.html#structured-concurrency)
- [ASIO Awaitable Operators](https://think-async.com/Asio/asio-1.28.0/doc/asio/overview/composition/cpp20_coroutines.html)

## Notes

- The `&&` operator from `asio::experimental::awaitable_operators` runs coroutines
  in parallel and waits for ALL to complete
- The `||` operator runs coroutines in parallel and completes when ANY finishes
- Channels use `asio::experimental::concurrent_channel` for thread-safety
- All peers share the same thread pool (`p2p_node::pool_`)

## Remaining `::asio::detached` Uses

These are the remaining detached uses in the codebase (excluding tests):

### Legitimate / By Design

| File | Location | Justification |
|------|----------|---------------|
| `task_group.hpp:97` | `spawn()` | Core mechanism - tasks are TRACKED via `active_count_` |
| `task_group.hpp:191` | `scoped_task_group` destructor | RAII blocking wait - expected behavior |
| `executor.cpp:232` | `stop_async()` | Top-level entry point from sync code (signal handlers) |

### DEFERRED (Requires Restructuring) ✅ COMPLETE

All `peer->run()` detached spawns have been eliminated using the `&&` operator pattern:

| File | Location | Solution |
|------|----------|----------|
| `p2p_node.cpp` | `connect()` | Sends to supervisor, waits for handshake via response channel |
| `p2p_node.cpp` | `connect_to_seed()` | Uses `peer->run() && seeding_protocol()` directly |
| `p2p_node.cpp` | `run_inbound()` | Sends to supervisor (no wait needed for inbound) |
| `p2p_node.cpp` | `peer_supervisor()` | Uses `peer->run() && (handshake && add && protocols)` |

**Pattern used**: The `&&` operator runs both coroutines in parallel. When peer disconnects,
both `run()` and the protocol branch exit, and `&&` completes. No detached needed!

```cpp
// Structured concurrency pattern for peer lifecycle
peer_tasks.spawn([...]() -> awaitable<void> {
    co_await (
        peer->run() &&                           // Message pump runs in parallel
        [&]() -> awaitable<void> {
            co_await perform_handshake(*peer);   // Uses channels (run() is running!)
            co_await run_protocols(peer);
        }()
    );  // Both complete when peer disconnects
});
```

## Future Work

### High Priority
- [x] ~~Eliminate `peer->run()` detached spawns~~ ✅ COMPLETE

### Medium Priority
- [ ] Review C API (`src/c-api/`) for structured concurrency patterns
- [ ] Update tests to use structured concurrency where applicable

### Low Priority
- [ ] Consider making `executor::stop_async()` awaitable
- [ ] Add exception propagation to `task_group`

---

**Last Updated**: 2025-12-22
**Branch**: `feat/structured-concurrency`
