# UTXO-Z Performance Improvements

## Log format reference

```
{height}/{target} ({pct}%) | {total}ms (f:{} s:{} p:{} a:{} d:{}) | {} ins, ({},{}) del
```

| Letter | Variable | Meaning |
|--------|----------|---------|
| **f** | `fetch_ms` | Fetch raw blocks from disk — block store (`fetch_blocks_raw`) |
| **s** | `parse_ms` | Parse blocks — minimal zero-copy parser (`parse_utxo_block`) |
| **p** | `process_ms` | Process batch — build delta + serialize values + MTP calculation (`process_compact_block_utxos` + `merge`) |
| **a** | `apply_ms` | Apply delta to UTXO-Z — inserts and erases (`apply_utxo_delta_raw`) |
| **d** | `deferred_ms` | Process deferred deletions (`process_deferred_deletions`) |

Inserts/deletes: `{net_inserts} ins, ({direct_del},{deferred_del}) del`

Source: `src/blockchain/src/utxo_builder.cpp` lines ~693-700

---

## Measurement History

### Baseline (utxoz 0.0.1) — heights 440-455k
- Total: ~4750ms per 1000-block batch
- a (apply to UTXO-Z): ~2700ms (57% of total) — **bottleneck**

### After hash fix (utxoz 0.0.2) — heights 440-455k
- Total: ~2200ms per 1000-block batch (**-54%**)
- a (apply): ~420ms → ~310ms (14-19% of total) (**-84%**)
- Typical breakdown at 445k: `2292ms (f:294 s:541 p:796 a:324 d:337)`
  - f: 13%, s: 24%, p: 35%, a: 14%, d: 15%
- **s (parse) and p (process) are now the heaviest phases**

### After hash fix + OP_RETURN filter (utxoz 0.0.2) — heights 440-455k
- Total: ~2150ms per 1000-block batch
- a (apply): ~340ms (16% of total)
- Typical breakdown at 445k: `2068ms (f:189 s:536 p:722 a:269 d:352)`
  - f: 9%, s: 26%, p: 35%, a: 13%, d: 17%
- OP_RETURN filter reduces insert count → smaller UTXO set → faster apply

### Interesting patterns at 480-550k (hash fix run)
- 480-500k: tiny blocks (10-60k ins), 80-400ms per batch — `d` (deferred) dominates
- 510-550k: blocks growing, mostly 300-600ms with `a` growing as UTXO set gets larger
- **547k outlier**: `7384ms (f:333 s:1201 p:2053 a:3355 d:442)` — 6M insertions
  - a = 45%, p = 28% — at extreme volumes, UTXO-Z apply is still the bottleneck

---

## 1. Hash function: `boost::hash<raw_outpoint>` — DONE

Replaced `boost::hash<raw_outpoint>` (36 byte-by-byte `hash_combine` calls) with
`hash_outpoint()`: memcpy first 8 bytes of txid + XOR with output index.

Centralized in `hash_outpoint()` free function in `types.hpp`, used by:
- `outpoint_hash` in `utxo_value.hpp` (internal flat_map hasher)
- `std::hash<raw_outpoint>` specialization
- `hash_value()` for deferred_deletion_entry / deferred_lookup_entry

**Result:** a (apply) time dropped 84% at heights 440-455k.

**Status:** DONE (utxoz 0.0.2)

---

## 2. `can_insert_safely()` calls `get_free_memory()` on every insert

**File:** `src/database_impl.cpp` (`insert_in_index`)

Every single `insert()` call triggers `can_insert_safely()` which calls
`segments_[Index]->get_free_memory()`. This walks the free-list of the memory-mapped segment
allocator — an O(n) operation where n is the number of free chunks.

During IBD we insert millions of UTXOs. This is a massive overhead.

**Fix:** Check only periodically (e.g., every N inserts) or use a cached free-memory value
that's updated every N operations. Alternative: track insertions since last check and only
recheck when approaching the limit (e.g., when load factor > 0.8).

**Impact:** HIGH — eliminates per-insert O(n) walk of the allocator free list.
Would reduce `a` (apply) time.

**Status:** PENDING — next target

---

## 3. `std::hash<raw_outpoint>` also byte-by-byte — DONE

Fixed alongside #1. Now uses `hash_outpoint()`.

**Status:** DONE (utxoz 0.0.2)

---

## 4. Erase searches ALL 4 containers linearly

**File:** `src/database_impl.cpp` (`erase_in_latest_version`)

For every `erase()`, the code searches container 0 (44B), then 1 (128B), then 2 (512B),
then 3 (10KB). Since the caller knows the value size (it was the entity that inserted it),
we could pass a size hint to skip irrelevant containers.

**Fix:** Add `erase(key, height, size_hint)` overload that goes directly to the right container.
During IBD we know the output size from the raw bytes.

**Impact:** MEDIUM — saves 1-3 unnecessary hash lookups per erase. At scale (millions of erases)
this adds up. Would reduce `a` (apply) time.

**Status:** PENDING

---

## 5. Memory-mapped `flat_map` allocator overhead

**File:** `src/detail/utxo_value.hpp`

The `boost::unordered_flat_map` uses `bip::allocator` backed by `managed_mapped_file`.
This allocator is more expensive than heap allocation due to:
- Segment manager bookkeeping
- Free-list management in shared memory
- Potential fragmentation in the mapped region

During IBD, the pattern is insert-heavy with relatively few erases. The allocator
overhead compounds with each insertion.

**Fix options:**
a) Pre-reserve the hash map with expected capacity to minimize rehashing
b) Use a bulk-insert API that batches allocations
c) Consider a simpler memory layout for the IBD phase (e.g., a sorted array that gets
   converted to a hash map after IBD)

**Impact:** MEDIUM — structural, harder to change. Would reduce `a` (apply) time.

**Status:** PENDING

---

## 6. Eliminate exceptions/throw from UTXO-Z

UTXO-Z currently uses `throw std::out_of_range` in `insert()` when a value is too large
for any container. Replace with a non-throwing error path (e.g., return `false` or
`std::expected`/error code). Exceptions are expensive and should not be used in the hot path.

**Status:** PENDING

---

## 7. Handle BIP30 duplicate txids correctly

Before BIP30, two pairs of coinbase transactions were mined with identical txids:

- `d5d27987d2a3dfc724e359870c6644b40e497bdc0589a033220fe15429d88599` — heights 91722 and 91812
- `e3bf3d07d4b0375638d5f1db5255fe07ba2c4cb067cd81b84ee974b6585fb468` — heights 91842 and 91880

The second occurrence overwrites the first in the UTXO set (the original outputs become
unspendable). This is part of the consensus rules — BIP30 later prohibited duplicate txids
entirely, but these two historical cases must be handled correctly.

Currently UTXO-Z returns `false` on duplicate insert (and we treat it as an error).
We need to decide the correct behavior: overwrite the existing entry (matching the
historical consensus behavior) or skip with a warning only for these two known cases.

**Status:** PENDING

---

## Priority Order

1. ~~Hash function (#1)~~ — DONE
2. **can_insert_safely (#2)** — next target (reduces `a`)
3. ~~std::hash specialization (#3)~~ — DONE
4. **Erase size hint (#4)** — medium impact (reduces `a`)
5. **Allocator overhead (#5)** — lowest priority, structural change (reduces `a`)
