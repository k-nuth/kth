# Sync Performance Benchmarks

## Test Environment
- Bandwidth: 1 Gbps (contracted)
- Node: Knuth (kth-mono)
- Mode: Download-only (no validation, no storage)

## Configuration Locations
- `src/node/src/parser.cpp:100` → outbound_connections
- `src/node/src/sync/orchestrator.cpp:254` → max_peers
- `src/network/src/p2p_node.cpp:764` → max_parallel_attempts

---

## Results

### Test 1: 8 peers (full block parsing)
- **Date:** 2026-02-02
- **Peers:** 8
- **Parsing mode:** Full block (before light_block optimization)
- **Blocks:** ~928,000
- **Duration:** ~1h 41m (101 minutes)
- **Throughput:** ~35-45 MB/s
- **Notes:** Baseline before light_block fix

### Test 2: 16 peers (light_block)
- **Date:** 2026-02-02
- **Peers:** 16
- **Parsing mode:** light_block
- **Blocks:** -
- **Duration:** -
- **Throughput:** -
- **Notes:** Running...

### Test 3: 32 peers (light_block)
- **Date:** Pending
- **Peers:** 32
- **Parsing mode:** light_block
- **Blocks:** -
- **Duration:** -
- **Throughput:** -****
- **Notes:** Configured, waiting for Test 2

---

## Reference Benchmark (BCHN)

- **Full IBD with validation + storage:** 5h 23m
- **Start:** 2026-01-30T11:11:07Z
- **End:** 2026-01-30T16:34:XX
- **Notes:** Complete IBD including validation and storage to disk

---

## Analysis

| Test | Peers | Mode | Duration | Throughput | vs BCHN |
|------|-------|------|----------|------------|---------|
| 1 | 8 | full block | ~1h 41m | ~35-45 MB/s | - |
| 2 | 16 | light_block | - | - | - |
| 3 | 32 | light_block | - | - | - |
| BCHN | - | full IBD | 5h 23m | - | baseline |

**Goal:** Establish download-only baselines to compare against when adding validation and storage.
