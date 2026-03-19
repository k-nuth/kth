# Fast IBD Benchmarks

## Environment

- **Chain**: BCH mainnet (~930K blocks at time of test)
- **Date**: 2026-02-11
- **OS**: Linux 6.18.4-zen1
- **Block files**: `blk*.dat` (128 MiB max per file, `posix_fallocate` pre-allocation)
- **Chunk size**: 16 blocks per chunk
- **Peers**: 20-25 concurrent download peers

## Pipeline Stages

```
Download → Fast Validation (merkle) → Block Storage (flat files) → [UTXO Build]
```

## Results

| Stage                                    | Time       | Overhead | Notes |
|------------------------------------------|------------|----------|-------|
| Download only                            | 37 min     | —        | Baseline: download + deserialize, discard blocks |
| Download + Merkle Validation             | 37 min     | +0 min   | Parallel merkle via thread pool, zero measurable cost |
| Download + Validation + Storage (run 1)  | 44.5 min   | +7.5 min | Old code (serial writes), files created from scratch |
| **Download + Validation + Storage (run 2)** | **38.4 min** | **+1.4 min** | New code (parallel writes + contiguous tracking), files created from scratch |

## Run 1 — Serial Writes (old code)

- **Date**: 2026-02-11 18:46 → 19:30
- **Duration**: 44 min 26 sec
- **Total blocks**: 933,712 (58,357 chunks × 16 blocks)
- **Height range**: 1 → 930,000
- **Total disk**: ~203 GB across 1,631 block files
- **Peak RSS**: 868 MB (+668 MB over startup)
- **Storage throughput**: ~351 blk/s sustained (tail end)
- **Per-chunk store time**: ~150-650 µs (early), ~1-3 ms (late, height >900K)

## Run 2 — Parallel Writes (new code, parallel writes + contiguous tracking)

- **Date**: 2026-02-11 22:49 → 23:28
- **Duration**: 38 min 22 sec
- **Total blocks**: 930,832 (58,177 chunks × 16 blocks)
- **Height range**: 1 → 930,000
- **Contiguous**: [1..930,000] — no gaps, fully verified via `header_index::have_data`
- **LMDB last_block_height**: 930,000 (set from contiguous height, correct)
- **Peak RSS**: 1,218 MB (+1,019 MB over startup)
- **Storage throughput**: ~410 blk/s sustained (tail end)
- **Download speed**: ~80-110 MB/s with 25 peers
- **Peers**: 25
- **Fragmentation**: 55.7% in-order on disk, 44.3% inversions (25,792 backward seeks for sequential read)
- **header_index**: 937,903 entries total, 930,000 have_data, first gap at 930,001
- **Note**: Cold run (files created from scratch), yet faster than run 1 thanks to parallel writes

## Storage Architecture (Option 2)

Parallel writes with pre-allocated file positions:

1. **Phase 1 — Allocate** (serial, single pool thread):
   Reserve N positions in `blk*.dat` files via `allocate_block_space()`.
   Fast O(1) per block — just bumps file offset counters.

2. **Phase 2 — Write** (parallel, N pool threads):
   Write N blocks simultaneously via `write_block_at()`.
   Each opens its own `FILE*` at the pre-allocated offset.

3. **Phase 3 — Index** (serial, no I/O):
   Update `header_index` with `(file, pos)` for each block.

Chunks are stored in arrival order (not height order). The `header_index` provides
the mapping `height → (file, pos)` for later reads. Sequential read performance
is preserved by `read_blocks_raw()` which sorts positions before reading.

Contiguous stored height is tracked via `header_index::has_status(idx, have_data)`
— no additional data structure needed. LMDB `last_block_height` is only set to the
highest contiguous height (no gaps).

## Fragmentation Analysis

With 25 peers downloading chunks in parallel, blocks arrive out of order and are
written to `blk*.dat` files in arrival order rather than height order.

Run 2 measured **55.7% in-order** on disk (44.3% inversions). This means a sequential
height scan would need to seek backwards ~44% of the time. However, `read_blocks_raw()`
already sorts positions by (file, offset) before reading, so actual read performance
is not significantly impacted.

## Key Observations

- Merkle validation is essentially free (parallel, CPU-bound, dwarfed by network I/O).
- Run 2 (new code, cold) is faster than Run 1 (old code, cold): 38.4 vs 44.5 min.
- Parallel writes (Option 2) reduced cold-start overhead from +7.5 min to +1.4 min.
- RSS stays around 1.2 GB — no memory leak or unbounded buffering.
- Download throughput ~80-110 MB/s sustained with 25 peers.
- Contiguous height tracking via `have_data` flags works perfectly — zero overhead, no extra structures.

## TODO

- [x] Measure fragmentation (% of blocks out-of-order on disk)
- [ ] UTXO Build benchmark (Download + Validation + Storage + UTXO)
- [ ] Compare Knuth v1 vs BCHN IBD times on same hardware
- [ ] Test on low-end hardware (Raspberry Pi) and compare Knuth v1 vs BCHN
