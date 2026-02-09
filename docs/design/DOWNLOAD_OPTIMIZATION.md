# Download Optimization Plan

**Date:** 2026-02-08
**Status:** In Progress
**Branch:** feat/csp-sync-utxoz

## Current Performance

- Full sync: 932,257 blocks in 54 minutes
- Average throughput: 72.6 MB/s
- Peak throughput: ~140 MB/s
- Problem: Significant throughput variations (drops from 100+ MB/s to <5 MB/s)

## Analysis Results

### Batch Request Timing (58,290 batches analyzed)

| Metric | Network Wait | Deserialize |
|--------|-------------|-------------|
| Min | 35 ms | 0.0 ms |
| Median | 237 ms | 0.1 ms |
| p95 | 3,750 ms | 1.1 ms |
| p99 | 9,838 ms | 1.5 ms |
| Max | 58,786 ms | 18.2 ms |

**Key finding:** Deserialize is NOT the bottleneck. Network wait is.

### Slow Batches

- 1,943 batches (3.3%) took >5 seconds
- Some peers consistently slow (e.g., 73.224.164.37, 167.179.172.173)

### Stall Events

- 29 stalls with >20 peers connected but <5 MB/s throughput
- 42 gaps >5 seconds between stats prints
- Stalls distributed across all block heights (not block-size related)

## Root Causes Identified

1. **Slow peers** - 3.3% of batches take >5 seconds due to slow peers
2. **No peer reputation** - Slow peers keep getting assigned chunks
3. **High stall timeout** - 15s timeout means slow peer blocks a slot for too long
4. **Possible io_context bottleneck** - Single-threaded, all coroutines compete

## Proposed Solutions

### Phase 1: Quick Wins (Low complexity)

#### 1.1 Reduce stall_timeout
- **Current:** 15 seconds
- **Proposed:** 5 seconds
- **File:** `src/node/include/kth/node/sync/chunk_coordinator.hpp`
- **Risk:** May cause more chunk retries on legitimately slow (but working) peers
- **Expected impact:** High - faster recovery from slow peers

#### 1.2 Smaller chunks
- **Current:** 16 blocks per chunk
- **Proposed:** 8 blocks per chunk
- **File:** `src/node/include/kth/node/sync/chunk_coordinator.hpp`
- **Risk:** More overhead per chunk, more round transitions
- **Expected impact:** Medium - faster granularity, less wasted time per slow chunk

### Phase 2: Peer Reputation (Medium complexity)

#### 2.1 Track peer speed
- Track average download time per peer
- Store in `peer_database` or `chunk_coordinator`
- Metrics: blocks/second, MB/second, failure rate

#### 2.2 Prefer fast peers
- Sort available peers by speed when assigning chunks
- Deprioritize (but don't ban) slow peers
- Consider: weighted random selection based on speed

#### 2.3 Avoid repeat offenders
- If peer times out on a chunk, reduce priority for next chunks
- Exponential backoff for consistently slow peers

### Phase 3: Speculative Execution (Medium complexity)

#### 3.1 Speculative retry
- If chunk takes >2s, start speculative request to another peer
- First response wins, cancel the other
- **Risk:** Increased bandwidth usage
- **Expected impact:** Very high - eliminates tail latency

#### 3.2 Implementation approach
```cpp
// Pseudocode
auto primary = request_chunk(peer1, chunk_id);
timer.expires_after(2s);

auto result = co_await (primary || timer);
if (result.index() == 1) {  // Timer won
    auto backup = request_chunk(peer2, chunk_id);
    auto final = co_await (primary || backup);
    // Use whichever completes first
}
```

### Phase 4: Multi-threading (Medium complexity)

#### 4.1 Multi-threaded io_context
- **Current:** Single thread runs `io_context_.run()`
- **Proposed:** Multiple threads (e.g., 4) run `io_context_.run()`
- **File:** `src/node/src/executor/executor.cpp`
- **Risk:** Need to ensure thread-safety of shared state
- **Investigation needed:** Is io_context actually the bottleneck?

#### 4.2 Test approach
```cpp
// Instead of:
io_thread_ = std::thread([this]() { io_context_.run(); });

// Try:
for (int i = 0; i < 4; ++i) {
    io_threads_.emplace_back([this]() { io_context_.run(); });
}
```

#### 4.3 Thread-safety considerations
- `chunk_coordinator` uses atomics - should be safe
- Channels are thread-safe
- `peer_session` - need to verify
- Logging with spdlog - thread-safe

### Phase 5: Advanced Optimizations (Higher complexity)

#### 5.1 Predictive chunk assignment
- Predict which chunks will be slow based on block height/size
- Assign to faster peers proactively

#### 5.2 Connection pooling
- Maintain more connections than active downloads
- Hot-swap slow connections

#### 5.3 Geographic peer selection
- Prefer peers with lower latency
- Track RTT per peer

## Implementation Order

1. **Multi-threaded io_context** - Test if it helps (Phase 4.1)
2. **Reduce stall_timeout to 5s** - Quick win (Phase 1.1)
3. **Track peer speed** - Foundation for other improvements (Phase 2.1)
4. **Speculative retry** - Biggest impact on tail latency (Phase 3.1)

## Metrics to Track

- Blocks per second (overall)
- MB per second (overall)
- p50, p95, p99 chunk download times
- Number of chunk timeouts/failures
- Peer speed distribution
- io_context thread utilization (if multi-threaded)

## Files to Modify

| File | Changes |
|------|---------|
| `chunk_coordinator.hpp` | stall_timeout, chunk_size config |
| `chunk_coordinator.cpp` | Peer speed tracking, speculative retry |
| `executor.cpp` | Multi-threaded io_context |
| `block_tasks.cpp` | Peer selection logic |
| `peer_database.hpp/cpp` | Speed metrics storage |

## Testing Plan

1. Run full sync with each change
2. Compare throughput metrics
3. Analyze logs with `scripts/analysis/` tools
4. A/B test different configurations

## Peer Performance Data (from 2026-02-08 sync)

### Most Problematic Peers

| Peer | Degradation | Avg(ms) | Slow% | Failures |
|------|-------------|---------|-------|----------|
| 73.224.164.37 | 79x (391→30,862ms) | 2,400 | 8.1% | 20 |
| 167.179.172.173 | 20.7x | 4,298 | 23.2% | 10 |
| 18.139.1.192 | - | 11,282 | 47.9% | 6 |
| 146.190.164.232 | - | 3,570 | 20.7% | 13 |
| 15.235.229.86 | - | 3,373 | 23.9% | 13 |

### Best Performing Peers

| Peer | Chunks | Avg(ms) | Max(ms) |
|------|--------|---------|---------|
| 95.179.139.47 | 5,555 | 191 | 2,357 |
| 18.198.69.223 | 529 | 217 | 1,566 |
| 54.168.27.235 | 72 | 290 | 1,196 |

### Key Observations

1. **Degradation is common**: Many peers start fast and get 3-80x slower
2. **Slow peers keep getting work**: No memory of past performance
3. **Wide variance**: 191ms vs 11,282ms average (59x difference)
4. **Reconnection problem**: Slow peers reconnect and get work again

## Future Improvements

### Relative vs Absolute Slow Peer Detection

Currently `is_slow_peer()` uses an absolute threshold (500ms/block). This has limitations:

1. **All peers slow**: If network conditions are bad and ALL peers are slow, we'd exclude everyone
2. **Relative outliers**: A peer at 400ms is "fast" by absolute measure but 4x slower than peers at 100ms

**Proposed approach**: Use both relative and absolute thresholds:

```cpp
// Absolute: never use peers slower than 2000ms/block (hard limit)
bool is_absolutely_slow = avg_ms > 2000.0;

// Relative: exclude peers >3x slower than median
double median = peer_db.get_median_performance();
bool is_relatively_slow = median > 0 && avg_ms > median * 3.0;

// Peer is slow if either condition is true
return is_absolutely_slow || is_relatively_slow;
```

**Benefits**:
- Adapts to network conditions (if all peers are slow, still uses them)
- Identifies outliers even when absolute threshold not reached
- Hard limit prevents using extremely slow peers

**Alternative approach**: Top-N fastest peers

Instead of filtering slow peers, select the N fastest peers (sorted by speed):

```cpp
// Get peers sorted by speed (fastest first)
auto sorted_peers = peers;
std::sort(sorted_peers.begin(), sorted_peers.end(), [&](auto& a, auto& b) {
    return network.get_peer_speed(a->authority()) < network.get_peer_speed(b->authority());
});

// Use top N peers (or all if fewer than N)
size_t max_download_peers = std::min(sorted_peers.size(), size_t{8});
sorted_peers.resize(max_download_peers);
```

This approach:
- Always uses the best available peers
- Naturally adapts to pool quality
- Limits parallelism to avoid diminishing returns
- Simple to implement and reason about

**Implementation notes**:
- `get_median_performance()` already exists in `peer_database`
- Need to call it during filtering in `broadcast_peers()`
- Consider caching median (expensive to compute every broadcast)

## Progress Log

| Date | Change | Result |
|------|--------|--------|
| 2026-02-08 | Analysis complete | Identified slow peers as root cause |
| 2026-02-08 | Peer performance script | Created scripts/analysis/peer_performance.py |
| 2026-02-08 | Slow peer filtering | Filter slow peers in peer_provider broadcast |
| | | |
