# Sync Stages Architecture

## Stage 1: Header Sync
- Download and validate all block headers from peers
- Headers are validated (PoW, chain rules, etc.)
- This establishes the "roadmap" for block download

## Stage 2: Fast Block Sync (below checkpoint)
- Download blocks in parallel from multiple peers
- No re-orgs possible because headers are already validated
- Network pool downloads and does lightweight parsing only (`light_block.hpp` - enough to compute merkle root, but without computing it yet)
- Pass serialized (light) blocks to the blockchain thread pool (more threads) for lightweight validation
- **Lightweight validation**: compute Merkle Root and compare against the header's merkle root
- These blocks are "under checkpoint" so full validation is not needed

## Stage 3: Block Storage
- Persist validated blocks to disk
- Could use the same thread pool as Stage 2, or a dedicated group of threads
- Must happen before UTXO indexing can process these blocks

## Stage 4: UTXO Set Indexing
- Independent process that can run in parallel with block download
- Reads stored blocks from disk and builds the UTXO set
- Can lag behind the download/storage stages - doesn't block them

## Stage 5: Slow Block Sync (above checkpoint)
- Blocks with height above the last checkpoint
- Requires **full validation** (script execution, signature verification, etc.)
- Full validation needs the UTXO set with the "snapshot" of the previous block
- Download can still be parallel, but validation is a bottleneck: it must be serial because each block depends on the UTXO state left by the previous block
- Needs: UTXO set complete up to checkpoint + all blocks above checkpoint downloaded

## Stage 6: Post-IBD (Initial Block Download complete)
- Node is synced, now receiving new blocks (header + block) as they are mined
- No urgency - blocks arrive every ~10 minutes
- Needs: UTXO set, headers, mempool
- Transaction validation scheme needed (not yet designed)
- Full validation of incoming blocks and transactions

---

## Pipeline Visualization

```
Stage 1        Stage 2           Stage 3        Stage 4          Stage 5         Stage 6
Headers  --->  Fast Download --> Storage  --->  UTXO Build  -->  Slow Sync  -->  Post-IBD
(serial)       (parallel)        (parallel?)    (parallel,       (serial          (event-
               light parse       disk I/O       independent)     validation,      driven)
               + merkle                                          parallel
               validation                                        download)
```

## Thread Pool Usage (planned)

| Stage | Pool | Notes |
|-------|------|-------|
| 1 - Header Sync | Network pool | Download + validate headers |
| 2 - Fast Download | Network pool | Download only, light parse |
| 2 - Light Validation | Priority/blockchain pool | Merkle root computation |
| 3 - Block Storage | TBD | Disk I/O |
| 4 - UTXO Indexing | Priority/blockchain pool | CPU-bound, can run in parallel with download |
| 5 - Slow Sync | Network (download) + Priority (validation) | Validation is serial bottleneck |
| 6 - Post-IBD | Both pools | Full validation + mempool management |
