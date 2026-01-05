# Parallel Block Download - Design Document

## Problem Statement

Current block download is sequential: request block N, wait for response, request block N+1.
With ~100ms round-trip latency, this limits throughput to ~10 blocks/second regardless of bandwidth.

**Goal**: Download blocks in parallel from multiple peers to maximize throughput.

## BCHN Reference Implementation

BCHN uses a **centralized coordinator** with these parameters:

```cpp
MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16   // per-peer pipeline depth
BLOCK_DOWNLOAD_WINDOW = 1024           // global look-ahead window
```

Key data structures:
- `mapBlocksInFlight`: global multimap (hash → peer_id) tracking ALL in-flight blocks
- `vBlocksInFlight`: per-peer list of blocks being downloaded

Coordination logic:
1. For each peer, periodically check if `vBlocksInFlight.size() < 16`
2. Call `FindNextBlocksToDownload()` which skips blocks already in `mapBlocksInFlight`
3. Send single `getdata` with multiple block inventories
4. On block received: remove from tracking, process, loop automatically refills

## Our Design: Block Download Coordinator

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     BlockDownloadCoordinator                         │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                    Shared State                              │    │
│  │  - next_height_to_assign: atomic<uint32_t>                  │    │
│  │  - blocks_in_flight: map<height, PeerAssignment>            │    │
│  │  - pending_blocks: map<height, Block> (out-of-order buffer) │    │
│  │  - next_height_to_validate: uint32_t                        │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐   │
│  │   Peer 1    │ │   Peer 2    │ │   Peer 3    │ │   Peer N    │   │
│  │ in_flight:  │ │ in_flight:  │ │ in_flight:  │ │ in_flight:  │   │
│  │ [h1,h2,h3]  │ │ [h4,h5,h6]  │ │ [h7,h8,h9]  │ │ [...]       │   │
│  │ max: 16     │ │ max: 16     │ │ max: 16     │ │ max: 16     │   │
│  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │                 Validation Pipeline                          │    │
│  │  Processes blocks in order as they become available          │    │
│  │  next_height_to_validate → chain_.organize()                 │    │
│  └─────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────┘
```

### Configuration

```cpp
struct download_config {
    size_t max_blocks_per_peer = 16;        // pipeline depth per peer
    size_t global_window = 1024;            // max look-ahead
    size_t target_peers = 8;                // desired concurrent peers
    std::chrono::seconds block_timeout = 60s;
    std::chrono::seconds stall_timeout = 10s;
};
```

### Core Components

#### 1. BlockDownloadCoordinator

Central coordinator that manages block assignments across peers.

```cpp
class BlockDownloadCoordinator {
public:
    // Called by sync_session to get next blocks to download
    std::vector<hash_digest> claim_blocks(
        peer_session& peer,
        size_t max_count  // typically 16
    );

    // Called when a block is received
    void block_received(
        hash_digest const& hash,
        uint32_t height,
        domain::message::block block
    );

    // Called when peer disconnects - reassign its blocks
    void peer_disconnected(peer_session const& peer);

    // Called periodically to check for stalled downloads
    void check_timeouts();

    // Get next validated block for chain (blocks until available or timeout)
    awaitable<std::optional<block_ptr>> next_validated_block();

private:
    std::mutex mutex_;

    // Assignment tracking
    uint32_t start_height_;
    uint32_t target_height_;
    std::atomic<uint32_t> next_height_to_assign_;

    // In-flight tracking: height -> {peer, request_time, hash}
    struct Assignment {
        peer_session::ptr peer;
        std::chrono::steady_clock::time_point requested_at;
        hash_digest hash;
    };
    std::map<uint32_t, Assignment> in_flight_;

    // Out-of-order buffer: height -> block
    std::map<uint32_t, block_ptr> pending_blocks_;

    // Validation state
    uint32_t next_height_to_validate_;

    // Channel for validation pipeline
    concurrent_channel<block_ptr> validated_blocks_;
};
```

#### 2. Peer Download Coroutine

Each peer runs a download coroutine that:
1. Claims blocks from coordinator
2. Sends batch `getdata`
3. Receives blocks and reports to coordinator
4. Repeats

```cpp
::asio::awaitable<void> peer_download_loop(
    peer_session::ptr peer,
    BlockDownloadCoordinator& coordinator
) {
    while (!stopped_ && peer->connected()) {
        // Claim up to 16 blocks
        auto blocks = coordinator.claim_blocks(*peer, 16);
        if (blocks.empty()) {
            // No more blocks or window full
            co_await async_sleep(100ms);
            continue;
        }

        // Send batch getdata
        co_await send_getdata(peer, blocks);

        // Receive blocks (with timeout)
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto result = co_await receive_block_with_timeout(peer, 30s);
            if (!result) {
                // Timeout or error - coordinator will reassign
                break;
            }
            coordinator.block_received(result->hash, result->height, std::move(*result));
        }
    }
}
```

#### 3. Validation Pipeline

Separate coroutine that processes blocks in order:

```cpp
::asio::awaitable<void> validation_pipeline(
    BlockDownloadCoordinator& coordinator,
    blockchain::block_chain& chain
) {
    while (!stopped_) {
        auto block = co_await coordinator.next_validated_block();
        if (!block) break;

        auto ec = co_await chain.organize(*block);
        if (ec) {
            // Handle validation failure
            spdlog::error("Block validation failed at height {}", block->height());
        }
    }
}
```

### Coordination Strategies

#### Option A: Centralized (BCHN-style)

```
Coordinator assigns blocks sequentially:
  Peer 1 gets: [1, 2, 3, ..., 16]
  Peer 2 gets: [17, 18, 19, ..., 32]
  Peer 3 gets: [33, 34, 35, ..., 48]

As blocks complete, refill to maintain 16 per peer.
```

**Pros**: Simple, predictable, good locality
**Cons**: Single point of coordination

#### Option B: Work Stealing (Decentralized)

```
Each peer claims ranges atomically:
  next_to_claim.fetch_add(16) → gets exclusive range

If peer stalls, others can "steal" unclaimed work.
```

**Pros**: Less contention, self-balancing
**Cons**: More complex, potential for gaps

#### Option C: Hybrid (Recommended)

```
1. Coordinator assigns contiguous ranges to peers
2. Each peer manages its own in-flight window
3. On timeout/disconnect, coordinator reassigns to fastest peer
4. Adaptive: assign more blocks to faster peers
```

### Handling Edge Cases

#### 1. Peer Disconnection

```cpp
void BlockDownloadCoordinator::peer_disconnected(peer_session const& peer) {
    std::lock_guard lock(mutex_);

    // Find all blocks assigned to this peer
    std::vector<uint32_t> to_reassign;
    for (auto& [height, assignment] : in_flight_) {
        if (assignment.peer.get() == &peer) {
            to_reassign.push_back(height);
        }
    }

    // Move back to unassigned pool
    for (auto height : to_reassign) {
        in_flight_.erase(height);
        // Will be claimed by next peer that asks
    }
}
```

#### 2. Stalled Downloads

```cpp
void BlockDownloadCoordinator::check_timeouts() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(mutex_);

    for (auto it = in_flight_.begin(); it != in_flight_.end(); ) {
        if (now - it->second.requested_at > stall_timeout_) {
            spdlog::warn("Block {} stalled from peer {}",
                it->first, it->second.peer->authority());

            // Ban slow peer or just reassign?
            // For now, just reassign
            it = in_flight_.erase(it);
        } else {
            ++it;
        }
    }
}
```

#### 3. Out-of-Order Blocks

```cpp
void BlockDownloadCoordinator::block_received(
    hash_digest const& hash,
    uint32_t height,
    block_ptr block
) {
    std::lock_guard lock(mutex_);

    // Remove from in-flight
    in_flight_.erase(height);

    if (height == next_height_to_validate_) {
        // Perfect - validate immediately
        validated_blocks_.try_send({}, std::move(block));
        ++next_height_to_validate_;

        // Check if we can validate more from pending
        while (auto it = pending_blocks_.find(next_height_to_validate_);
               it != pending_blocks_.end()) {
            validated_blocks_.try_send({}, std::move(it->second));
            pending_blocks_.erase(it);
            ++next_height_to_validate_;
        }
    } else {
        // Out of order - buffer it
        pending_blocks_[height] = std::move(block);
    }
}
```

### Multi-Peer Orchestration

```cpp
::asio::awaitable<sync_result> parallel_block_sync(
    blockchain::block_chain& chain,
    p2p_node& network,
    uint32_t start_height,
    uint32_t target_height
) {
    BlockDownloadCoordinator coordinator(start_height, target_height);

    // Get all connected peers
    auto peers = co_await network.peers().all();
    if (peers.empty()) {
        co_return sync_result{error::no_peers};
    }

    // Create task group for all download coroutines
    task_group download_tasks(network.thread_pool().get_executor());

    // Spawn download loop for each peer
    for (auto& peer : peers) {
        download_tasks.spawn([&peer, &coordinator]() -> awaitable<void> {
            co_await peer_download_loop(peer, coordinator);
        });
    }

    // Spawn validation pipeline
    download_tasks.spawn([&coordinator, &chain]() -> awaitable<void> {
        co_await validation_pipeline(coordinator, chain);
    });

    // Spawn timeout checker
    download_tasks.spawn([&coordinator]() -> awaitable<void> {
        while (!coordinator.complete()) {
            co_await async_sleep(1s);
            coordinator.check_timeouts();
        }
    });

    // Wait for all to complete
    co_await download_tasks.join();

    co_return sync_result{error::success, target_height - start_height};
}
```

### Performance Expectations

| Peers | Blocks/peer | Total In-Flight | Theoretical Speedup |
|-------|-------------|-----------------|---------------------|
| 1     | 16          | 16              | 16x                 |
| 4     | 16          | 64              | 64x                 |
| 8     | 16          | 128             | 128x                |

With 100ms latency and 16 blocks in-flight per peer:
- Single peer: 16 blocks / 100ms = 160 blocks/sec
- 8 peers: 128 blocks / 100ms = 1280 blocks/sec

Actual throughput limited by:
- Bandwidth (block size × blocks/sec)
- Validation speed (CPU-bound)
- Disk I/O

### Implementation Plan

1. **Phase 1: BlockDownloadCoordinator class**
   - Block assignment logic
   - In-flight tracking
   - Out-of-order buffering

2. **Phase 2: Peer download coroutines**
   - Batch getdata sending
   - Block receiving with timeout
   - Integration with coordinator

3. **Phase 3: Validation pipeline**
   - Ordered block processing
   - Chain integration

4. **Phase 4: Adaptive optimizations**
   - Peer speed tracking
   - Dynamic block assignment
   - Slow peer detection

### File Structure

```
src/node/
├── block_download_coordinator.hpp
├── block_download_coordinator.cpp
├── parallel_sync.hpp
├── parallel_sync.cpp
└── sync_session.cpp  (modified to use coordinator)
```

### Open Questions

1. **Should we download from peers we're not fully connected to?**
   - BCHN requires handshake complete before requesting blocks

2. **How to handle forks/reorgs during download?**
   - Need to validate headers first, then download blocks for winning chain

3. **Memory limits for out-of-order buffer?**
   - With 1024 window and avg 1MB blocks, could need 1GB buffer
   - Consider disk-based buffering for large syncs

4. **Prioritize recent blocks?**
   - During IBD, oldest blocks first
   - After IBD, newest blocks (for tip) should have priority

---

## Implementation Status

### Completed
- [x] `block_download_coordinator` class with parallel download management
- [x] `parallel_sync` orchestration with task_group (nursery pattern)
- [x] Integration with `sync_session` (automatic when p2p_node available)
- [x] Peer download loops with claim/receive pattern
- [x] Validation pipeline (in-order block validation)
- [x] Timeout checker for stalled downloads
- [x] Out-of-order buffering with flush to validation
- [x] Uses `boost::unordered_flat_map` for performance

### Pending Optimizations

1. **Batch getdata** (High Priority)
   - Current: Each peer sends getdata one block at a time, waits for response
   - Optimal: Send batch getdata (16 blocks), receive blocks via message handler
   - Requires: New `receive_block_with_timeout()` function in `protocols_coro.hpp`
   - Impact: Would reduce round-trip latency by ~16x per peer

2. **Expose configuration in settings** (Medium Priority)
   - Currently: `parallel_download_config` has hardcoded values
   - TODO: Add to `node::settings` for user configuration
   - Parameters: `max_blocks_per_peer`, `global_window`, `stall_timeout`

3. **Dynamic memory limits** (Medium Priority)
   - Issue: With window=1024 and ~1MB avg blocks, could use ~1GB in worst case
   - TODO: Calculate `global_window` based on available system memory
   - Consider: Disk-based buffering for very large syncs

4. **Refactor coordinator to eliminate mutex** (Low Priority)
   - Current: Uses `std::mutex` for thread-safe state access
   - Alternatives to explore:
     - Strand-based serialization (make `claim_blocks` awaitable)
     - Actor model with command channel
   - Note: Current mutex is acceptable due to low contention (~128 ops/sec)

5. **Adaptive peer assignment** (Future Enhancement)
   - Track peer download speeds
   - Assign more blocks to faster peers
   - Automatically deprioritize slow peers

6. **Penalize slow peers** (Future Enhancement)
   - If a peer consistently times out, reduce their block allocation
   - Consider temporary banning for very slow peers

---

## TODO: Parallel Validation Discussion

### Current State: Sequential Validation

Currently, validation happens sequentially: block N must be fully validated before block N+1.

```
Download:    [====1====] [====2====] [====3====] [====4====]  (parallel)
Validation:  [====1====][====2====][====3====][====4====]     (sequential)
```

### What CAN be parallelized?

1. **Syntax/Format Validation**
   - Block header format
   - Transaction format
   - Merkle root calculation
   - Size limits
   - These are independent per block

2. **Script Validation (Signature Checks)**
   - ECDSA/Schnorr signature verification is CPU-intensive
   - Scripts within a block can be validated in parallel
   - Scripts across blocks can potentially be validated in parallel (with caveats)

3. **PoW Validation**
   - Header hash < target
   - Independent per block

### What MUST be sequential?

1. **UTXO Set Updates**
   - Block N creates outputs that block N+1 might spend
   - Must apply in order to maintain consistency

2. **Contextual Validation**
   - Coinbase maturity (100 blocks)
   - Median time past
   - Difficulty adjustment
   - BIP activation heights

### Proposed Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     Download Phase                          │
│  Peer1: [blk 100-115]  Peer2: [blk 116-131]  Peer3: [...]  │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Pre-Validation (Parallel)                      │
│  - Syntax checks                                            │
│  - Merkle root                                              │
│  - PoW validation                                           │
│  - Script validation (signatures)                           │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              Contextual Validation (Sequential)             │
│  - UTXO lookups and updates                                 │
│  - Coinbase maturity                                        │
│  - Median time past                                         │
│  - Connect to chain                                         │
└─────────────────────────────────────────────────────────────┘
```

### Questions to Discuss

1. **Script validation across blocks**:
   - Can we validate scripts for block N+1 while block N is being connected?
   - Need to handle the case where N+1 spends an output created in N

2. **Batch signature verification**:
   - Schnorr signatures can be batch-verified (faster than individual)
   - How to accumulate signatures across blocks?

3. **UTXO cache strategy**:
   - Pre-fetch UTXOs for upcoming blocks?
   - Speculative execution with rollback?

4. **Thread pool sizing**:
   - How many threads for script validation?
   - Separate pools for download vs validation?

5. **Assumevalid optimization**:
   - Skip script validation for blocks before a known-good hash
   - Bitcoin Core uses this for faster IBD

### References

- BCHN parallel validation: `src/validation.cpp`
- Bitcoin Core `CheckInputScripts` parallelization
- libsecp256k1 batch verification

---

## Discussion Notes

(Add notes here as we discuss)
