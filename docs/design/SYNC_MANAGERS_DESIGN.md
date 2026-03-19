# Sync Managers Design (CSP Architecture)

> **IMPORTANT**: This is the authoritative design document for the sync layer.
> Read this at the start of EVERY session. Location: `docs/design/SYNC_MANAGERS_DESIGN.md`

## Core Principles (NON-NEGOTIABLE)

1. **ZERO mutexes, ZERO locks** - all coordination via channels
2. **Independent tasks** - each task owns its state, no shared mutable state
3. **Channel communication** - Go-style CSP (Communicating Sequential Processes)
4. **Separation of concerns**:
   - Download tasks are SEPARATE from validation tasks
   - Tasks communicate via channels, not shared memory
5. **Structured concurrency** - all tasks awaited via task_group

## Task Decomposition

```
DOWNLOAD TASKS              CHANNELS              VALIDATION TASKS
──────────────              ────────              ────────────────

┌─────────────────┐                              ┌─────────────────┐
│ HeaderDownload  │ ───▶ header_channel ───▶    │ HeaderValidation│
│ Task            │      (raw headers)          │ Task            │
└─────────────────┘                              └─────────────────┘

┌─────────────────┐                              ┌─────────────────┐
│ BlockDownload   │                              │ BlockValidation │
│ Task 1          │ ───┐                    ┌──▶│ Task            │
├─────────────────┤    │                    │   └─────────────────┘
│ BlockDownload   │ ───┼─▶ block_channel ───┤
│ Task 2          │    │   (height, block)  │
├─────────────────┤    │                    │
│ BlockDownload   │ ───┘                    │
│ Task N          │                         │
└─────────────────┘                         │
                                            │
                       chunk_complete_ch ◀──┘
                       (feedback loop)
```

## Full Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              full_node::run()                                │
│                                                                              │
│  co_await (network_.run() && sync_orchestrator());                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                          sync_orchestrator()                                 │
│                                                                              │
│  task_group all_tasks;                                                       │
│                                                                              │
│  // CHANNELS (the ONLY way tasks communicate)                               │
│  concurrent_channel<peer_ptr>           peer_channel;                       │
│  concurrent_channel<raw_headers>        header_download_ch;                 │
│  concurrent_channel<validated_header>   header_validated_ch;                │
│  concurrent_channel<height_range>       block_request_ch;                   │
│  concurrent_channel<downloaded_block>   block_download_ch;                  │
│  concurrent_channel<validated_block>    block_validated_ch;                 │
│                                                                              │
│  // INDEPENDENT TASKS (no shared state between them)                        │
│  all_tasks.spawn(peer_provider_task());           // Watches network        │
│  all_tasks.spawn(header_download_task());         // Downloads headers      │
│  all_tasks.spawn(header_validation_task());       // Validates headers      │
│  all_tasks.spawn(block_download_supervisor());    // Spawns downloaders     │
│  all_tasks.spawn(block_validation_task());        // Validates blocks       │
│  all_tasks.spawn(sync_coordinator_task());        // Orchestrates flow      │
│                                                                              │
│  co_await all_tasks.join();                                                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Tasks in Detail

### 1. PeerProviderTask
```
INPUT:  network (watches for new connections)
OUTPUT: peer_channel (sends new peers)

Loop:
  - Watch network for peer connect/disconnect
  - Send new peers to peer_channel
  - Tasks that need peers receive from this channel
```

### 2. HeaderDownloadTask
```
INPUT:  peer_channel (gets peers)
        header_request_ch (what to download)
OUTPUT: header_download_ch (raw headers)

Loop:
  - Receive peer from channel
  - Request headers from peer
  - Send raw headers to header_download_ch
```

### 3. HeaderValidationTask
```
INPUT:  header_download_ch (raw headers)
OUTPUT: header_validated_ch (validation results)
        header_organizer (writes validated headers)

Loop:
  - Receive headers from channel
  - Validate each header
  - Write to header_organizer (single writer, no lock needed)
  - Send validation result to header_validated_ch
```

### 4. BlockDownloadSupervisor
```
INPUT:  peer_channel (gets peers)
        block_request_ch (range to download)
OUTPUT: block_download_ch (downloaded blocks)

- Maintains internal task_group for download workers
- Spawns one download task per available peer
- Each download task claims ranges atomically and downloads
```

### 5. BlockDownloadTask (spawned by supervisor, one per peer)
```
INPUT:  peer (the peer to download from)
        next_chunk atomic (claims chunks)
OUTPUT: block_download_ch (downloaded blocks)

Loop:
  - Claim next chunk atomically: chunk = next_chunk.fetch_add(1)
  - Calculate range: [start + chunk*16, start + chunk*16 + 15]
  - Download blocks from peer
  - Send to block_download_ch
```

### 6. BlockValidationTask
```
INPUT:  block_download_ch (blocks to validate)
OUTPUT: block_validated_ch (validation results)
        block_chain (writes validated blocks)

State (OWNED, not shared):
  - next_height_to_validate: uint32_t
  - pending_blocks: map<height, block>  (out-of-order buffer)

Loop:
  - Receive block from channel
  - If out of order: buffer in pending_blocks
  - If in order: validate, write to chain, flush buffered
  - Send result to block_validated_ch
```

### 7. SyncCoordinatorTask
```
INPUT:  header_validated_ch (header sync progress)
        block_validated_ch (block sync progress)
OUTPUT: header_request_ch (trigger header sync)
        block_request_ch (trigger block sync)

Logic:
  - On startup: trigger header sync
  - On headers validated: trigger block sync for that range
  - On blocks validated: trigger more header sync
  - Handle errors, retries, etc.
```

## Message Types

```cpp
namespace kth::node::sync {

// ============================================================================
// Channel Message Types (CSP messages)
// ============================================================================

// Peer availability
struct peer_available {
    network::peer_session::ptr peer;
};

// Header sync messages
struct header_request {
    uint32_t from_height;
    hash_digest from_hash;
};

struct downloaded_headers {
    std::vector<domain::chain::header> headers;
    uint32_t start_height;
    network::peer_session::ptr source_peer;
};

struct headers_validated {
    uint32_t height;
    size_t count;
    code result;
};

// Block sync messages
struct block_range_request {
    uint32_t start_height;
    uint32_t end_height;
};

struct downloaded_block {
    uint32_t height;
    domain::message::block::const_ptr block;
    network::peer_session::ptr source_peer;
};

struct block_validated {
    uint32_t height;
    code result;
};

// Control messages
struct stop_signal {};

} // namespace kth::node::sync
```

## Channel Definitions

```cpp
namespace kth::node::sync {

// Peer distribution
using peer_channel = concurrent_channel<peer_available>;

// Header sync pipeline
using header_request_channel = concurrent_channel<header_request>;
using header_download_channel = concurrent_channel<downloaded_headers>;
using header_validated_channel = concurrent_channel<headers_validated>;

// Block sync pipeline
using block_request_channel = concurrent_channel<block_range_request>;
using block_download_channel = concurrent_channel<downloaded_block>;
using block_validated_channel = concurrent_channel<block_validated>;

// Control
using stop_channel = concurrent_event_channel;

} // namespace kth::node::sync
```

## Task Implementations

### header_download_task

Downloads headers from peers. Separate from validation.

```cpp
::asio::awaitable<void> header_download_task(
    peer_channel& peers,
    header_request_channel& requests,
    header_download_channel& output,
    stop_channel& stop
) {
    std::vector<network::peer_session::ptr> available_peers;

    while (true) {
        // Wait for: peer, request, or stop
        auto event = co_await (
            peers.async_receive(use_awaitable) ||
            requests.async_receive(use_awaitable) ||
            stop.async_receive(use_awaitable)
        );

        if (event.index() == 2) break;  // stop

        if (event.index() == 0) {
            // New peer
            available_peers.push_back(std::get<0>(event).peer);
            continue;
        }

        // Request received
        auto request = std::get<1>(event);

        if (available_peers.empty()) {
            continue;  // No peers, drop request (coordinator will retry)
        }

        // Use first available peer
        auto peer = available_peers.back();
        if (peer->stopped()) {
            available_peers.pop_back();
            continue;
        }

        // Download headers
        auto result = co_await network::get_headers(*peer, request.from_hash);

        if (result) {
            output.try_send({}, downloaded_headers{
                .headers = std::move(*result),
                .start_height = request.from_height,
                .source_peer = peer
            });
        }
        // On failure, peer will be removed naturally when stopped
    }
}
```

### header_validation_task

Validates headers. Single writer to header_organizer - NO LOCK NEEDED.

```cpp
::asio::awaitable<void> header_validation_task(
    blockchain::header_organizer& organizer,
    header_download_channel& input,
    header_validated_channel& output,
    stop_channel& stop
) {
    while (true) {
        auto event = co_await (
            input.async_receive(use_awaitable) ||
            stop.async_receive(use_awaitable)
        );

        if (event.index() == 1) break;

        auto downloaded = std::get<0>(event);

        size_t validated = 0;
        code result = error::success;

        for (auto const& header : downloaded.headers) {
            result = organizer.organize(header);  // Single writer!
            if (result != error::success) break;
            ++validated;
        }

        output.try_send({}, headers_validated{
            .height = downloaded.start_height + validated,
            .count = validated,
            .result = result
        });
    }
}
```

### block_download_supervisor

Manages download tasks. Spawns one download task per peer.

```cpp
::asio::awaitable<void> block_download_supervisor(
    peer_channel& peers,
    block_request_channel& requests,
    block_download_channel& output,
    stop_channel& stop,
    blockchain::header_organizer& organizer  // read-only for hashes
) {
    auto executor = co_await ::asio::this_coro::executor;
    task_group download_tasks(executor);

    // Shared atomic for chunk assignment (the ONLY shared state)
    // This is acceptable: it's a single atomic, no mutex
    std::atomic<uint32_t> next_chunk{0};
    uint32_t start_height = 0;
    uint32_t end_height = 0;
    size_t chunk_size = 16;

    while (true) {
        auto event = co_await (
            peers.async_receive(use_awaitable) ||
            requests.async_receive(use_awaitable) ||
            stop.async_receive(use_awaitable)
        );

        if (event.index() == 2) break;  // stop

        if (event.index() == 1) {
            // New block range request
            auto request = std::get<1>(event);
            start_height = request.start_height;
            end_height = request.end_height;
            next_chunk.store(0);
            continue;
        }

        // New peer - spawn download task
        auto peer = std::get<0>(event).peer;

        download_tasks.spawn(block_download_task(
            peer,
            next_chunk,
            start_height,
            end_height,
            chunk_size,
            output,
            organizer
        ));
    }

    co_await download_tasks.join();
}
```

### block_download_task

Downloads blocks from a single peer. Claims chunks atomically.

```cpp
::asio::awaitable<void> block_download_task(
    network::peer_session::ptr peer,
    std::atomic<uint32_t>& next_chunk,  // Atomic - no lock
    uint32_t start_height,
    uint32_t end_height,
    size_t chunk_size,
    block_download_channel& output,
    blockchain::header_organizer const& organizer  // read-only
) {
    uint32_t total_chunks = (end_height - start_height + chunk_size) / chunk_size;

    while (!peer->stopped()) {
        // Claim chunk atomically - NO LOCK, just atomic increment
        uint32_t chunk_id = next_chunk.fetch_add(1);

        if (chunk_id >= total_chunks) {
            break;  // No more chunks
        }

        uint32_t chunk_start = start_height + chunk_id * chunk_size;
        uint32_t chunk_end = std::min(chunk_start + chunk_size - 1, end_height);

        // Build request
        std::vector<std::pair<uint32_t, hash_digest>> blocks;
        for (uint32_t h = chunk_start; h <= chunk_end; ++h) {
            blocks.emplace_back(h, organizer.index().get_hash(h));
        }

        // Download from peer
        auto result = co_await network::request_blocks_batch(*peer, blocks, 30s);

        if (!result) {
            // Peer failed - exit, supervisor will spawn new tasks for new peers
            break;
        }

        // Send each block to output channel
        for (auto& block : *result) {
            output.try_send({}, downloaded_block{
                .height = block.height,
                .block = std::make_shared<domain::message::block const>(std::move(block.block)),
                .source_peer = peer
            });
        }
    }
}
```

### block_validation_task

Validates blocks in order. Single writer to chain - NO LOCK NEEDED.

```cpp
::asio::awaitable<void> block_validation_task(
    blockchain::block_chain& chain,
    block_download_channel& input,
    block_validated_channel& output,
    stop_channel& stop,
    uint32_t start_height
) {
    // OWNED state - not shared with anyone
    uint32_t next_height = start_height;
    boost::unordered_flat_map<uint32_t, domain::message::block::const_ptr> pending;

    while (true) {
        auto event = co_await (
            input.async_receive(use_awaitable) ||
            stop.async_receive(use_awaitable)
        );

        if (event.index() == 1) break;

        auto downloaded = std::get<0>(event);

        if (downloaded.height != next_height) {
            // Out of order - buffer it
            pending[downloaded.height] = downloaded.block;
            continue;
        }

        // Validate in order
        auto result = co_await chain.organize(downloaded.block, true);

        output.try_send({}, block_validated{
            .height = downloaded.height,
            .result = result
        });

        if (result != error::success) {
            break;  // Validation failed
        }

        ++next_height;

        // Flush buffered blocks
        while (auto it = pending.find(next_height); it != pending.end()) {
            result = co_await chain.organize(it->second, true);
            output.try_send({}, block_validated{next_height, result});
            pending.erase(it);
            ++next_height;

            if (result != error::success) break;
        }
    }
}
```

## sync_orchestrator

The main entry point. Creates all channels and spawns all tasks.

```cpp
::asio::awaitable<void> sync_orchestrator(
    blockchain::block_chain& chain,
    blockchain::header_organizer& organizer,
    network::p2p_node& network
) {
    auto executor = co_await ::asio::this_coro::executor;

    // =========================================================================
    // CHANNELS - The ONLY way tasks communicate
    // =========================================================================

    // Peer distribution
    peer_channel peers_for_headers(executor, 100);
    peer_channel peers_for_blocks(executor, 100);

    // Header pipeline
    header_request_channel header_requests(executor, 10);
    header_download_channel downloaded_headers(executor, 100);
    header_validated_channel validated_headers(executor, 100);

    // Block pipeline
    block_request_channel block_requests(executor, 10);
    block_download_channel downloaded_blocks(executor, 10000);  // Large buffer
    block_validated_channel validated_blocks(executor, 100);

    // Control
    stop_channel stop_signal(executor, 1);

    // =========================================================================
    // TASKS - All independent, communicate only via channels
    // =========================================================================

    task_group all_tasks(executor);

    // 1. Peer provider - watches network for new peers
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        boost::unordered_flat_set<std::string> known_peers;

        while (!network.stopped()) {
            auto peers = co_await network.peers().all();

            for (auto& peer : peers) {
                if (peer->stopped()) continue;

                auto addr = peer->authority().to_string();
                if (known_peers.insert(addr).second) {
                    // New peer - send to both pipelines
                    peers_for_headers.try_send({}, peer_available{peer});
                    peers_for_blocks.try_send({}, peer_available{peer});
                }
            }

            ::asio::steady_timer timer(executor);
            timer.expires_after(std::chrono::seconds(2));
            co_await timer.async_wait(::asio::use_awaitable);
        }
    }());

    // 2. Header download task
    all_tasks.spawn(header_download_task(
        peers_for_headers,
        header_requests,
        downloaded_headers,
        stop_signal
    ));

    // 3. Header validation task
    all_tasks.spawn(header_validation_task(
        organizer,
        downloaded_headers,
        validated_headers,
        stop_signal
    ));

    // 4. Block download supervisor (spawns per-peer download tasks)
    all_tasks.spawn(block_download_supervisor(
        peers_for_blocks,
        block_requests,
        downloaded_blocks,
        stop_signal,
        organizer
    ));

    // 5. Block validation task
    all_tasks.spawn(block_validation_task(
        chain,
        downloaded_blocks,
        validated_blocks,
        stop_signal,
        chain.top_height() + 1
    ));

    // 6. Sync coordinator - orchestrates the flow
    all_tasks.spawn([&]() -> ::asio::awaitable<void> {
        uint32_t blocks_synced_to = chain.top_height();
        uint32_t headers_synced_to = organizer.top_height();

        // Trigger initial header sync
        header_requests.try_send({}, header_request{
            headers_synced_to,
            organizer.index().get_hash(headers_synced_to)
        });

        while (!network.stopped()) {
            auto event = co_await (
                validated_headers.async_receive(use_awaitable) ||
                validated_blocks.async_receive(use_awaitable)
            );

            if (event.index() == 0) {
                // Headers validated
                auto result = std::get<0>(event);
                if (result.result == error::success) {
                    headers_synced_to = result.height;

                    // Trigger block download for new range
                    if (headers_synced_to > blocks_synced_to) {
                        block_requests.try_send({}, block_range_request{
                            blocks_synced_to + 1,
                            headers_synced_to
                        });
                    }

                    // Continue header sync
                    header_requests.try_send({}, header_request{
                        headers_synced_to,
                        organizer.index().get_hash(headers_synced_to)
                    });
                }
            } else {
                // Block validated
                auto result = std::get<1>(event);
                if (result.result == error::success) {
                    blocks_synced_to = result.height;

                    // Log progress periodically
                    if (blocks_synced_to % 1000 == 0) {
                        spdlog::info("[sync] Block {} validated", blocks_synced_to);
                    }
                }
            }
        }

        // Signal all tasks to stop
        stop_signal.try_send({});
    }());

    co_await all_tasks.join();
}
```

## Key Differences from V2

| Aspect | V2 (Current) | CSP (New) |
|--------|-------------|-----------|
| State sharing | Mutex-protected shared state | Each task owns state |
| Coordination | Atomic CAS, mutex | Channels only |
| Download/Validation | Same "manager" | Separate tasks |
| Chunk assignment | Lock-free slots with round | Single atomic counter |
| Validation buffer | Shared map with mutex | Task-owned, no lock |
| Peer management | Passed by reference | Sent via channel |
| Lifecycle | Manual stop flags | Channel close signals |

## Benefits

1. **ZERO data races by design** - no shared mutable state (except one atomic)
2. **Clear separation** - download is separate from validation
3. **Single writer principle** - each resource has ONE writer
4. **Testable** - tasks can be tested in isolation with mock channels
5. **Debuggable** - channel messages can be logged/traced
6. **Composable** - easy to add new tasks or modify coordination

## Concurrency Analysis

```
Task                    | Writes to              | Reads from
------------------------|------------------------|---------------------------
header_download_task    | header_download_ch     | peers_ch, header_request_ch
header_validation_task  | header_organizer       | header_download_ch
                        | header_validated_ch    |
block_download_task     | block_download_ch      | next_chunk atomic (shared)
block_validation_task   | block_chain            | block_download_ch
                        | block_validated_ch     |
sync_coordinator        | header_request_ch      | validated_headers_ch
                        | block_request_ch       | validated_blocks_ch
```

**Shared state:**
- `next_chunk` atomic - used by block download tasks (acceptable, single atomic)
- All other state is task-local

## Implementation Plan

### Phase 1: Message Types & Channels
- [ ] Create `src/node/include/kth/node/sync/messages.hpp`
- [ ] Define all message structs
- [ ] Define channel type aliases

### Phase 2: Header Tasks
- [ ] Create `src/node/include/kth/node/sync/header_tasks.hpp`
- [ ] Implement `header_download_task()`
- [ ] Implement `header_validation_task()`

### Phase 3: Block Tasks
- [ ] Create `src/node/include/kth/node/sync/block_tasks.hpp`
- [ ] Implement `block_download_supervisor()`
- [ ] Implement `block_download_task()`
- [ ] Implement `block_validation_task()`

### Phase 4: Orchestrator
- [ ] Create `src/node/include/kth/node/sync/orchestrator.hpp`
- [ ] Implement `sync_orchestrator()`
- [ ] Integrate with `full_node::run()`

### Phase 5: Cleanup
- [ ] Remove `block_download_coordinator.hpp/cpp`
- [ ] Remove `block_download_coordinator_v2.hpp/cpp`
- [ ] Remove `parallel_sync.hpp/cpp`
- [ ] Remove `parallel_sync_v2.hpp/cpp`
- [ ] Remove `sync_session.hpp/cpp` (replaced by orchestrator)
- [ ] Update CMakeLists.txt

## File Structure

```
src/node/
├── include/kth/node/
│   ├── sync/
│   │   ├── messages.hpp           # Message types & channel aliases
│   │   ├── header_tasks.hpp       # Header download & validation tasks
│   │   ├── block_tasks.hpp        # Block download & validation tasks
│   │   └── orchestrator.hpp       # Main sync_orchestrator()
│   └── ... (existing files)
├── src/
│   ├── sync/
│   │   ├── header_tasks.cpp
│   │   ├── block_tasks.cpp
│   │   └── orchestrator.cpp
│   └── ... (existing files)
```

## Integration with full_node

```cpp
// In full_node::run()
::asio::awaitable<void> full_node::run() {
    co_await (
        network_.run() &&
        sync::sync_orchestrator(chain_, organizer_, network_)
    );
}
```

---

**Last Updated**: 2025-12-30
**Branch**: `feat/structured-concurrency`
