# Coroutine Migration Status

## Overview

This document tracks the current state of the coroutine migration for the kth-node P2P networking layer.

## Completed Work

### Phase 0-6: Coroutine Infrastructure
- Coroutine configuration and smoke tests
- `peer_session` with connection helpers
- `peer_manager` for unified peer collection management
- Coroutine-based protocol functions (`protocols_coro.hpp/cpp`)
- `p2p_node` unified networking class

### Phase 7: Headers-First Sync Implementation
- **`sync_session`**: Coordinates headers-first synchronization with peers
  - Builds block locators for `getheaders` requests
  - Receives headers in batches (up to 2000 per request)
  - Delegates validation to `header_organizer`
  - Requests blocks via `getdata` for validated headers

- **`header_organizer`**: Manages header caching and validation during IBD
  - In-memory cache with configurable memory limits (default 256 MB)
  - Memory-aware caching with auto-flush when threshold reached
  - Thread-safe (mutex-protected public methods)
  - Batch persistence to database via `chain_.organize_headers_batch()`

- **`validate_header`**: Header validation logic
  - Context-free validation (PoW check)
  - Chain-context validation (previous hash, checkpoints)
  - Checkpoint conflict detection with detailed logging

- **`broadcaster`**: Template-based async channel broadcaster
  - Manages multiple subscribers with thread-safe operations
  - Uses `asio::experimental::concurrent_channel`

- **`system_memory`**: Cross-platform memory estimation utilities
  - `get_available_system_memory()` for cache sizing decisions

### Infrastructure Changes
- Deprecated legacy callback-based utilities (`dispatcher`, `sequencer`, `subscriber`, etc.)
- Moved legacy network code to `deprecated/` folders
- Added `block_chain::organize_headers_batch()` for batch header storage

## Current Issues / Known Problems

### Headers-First Sync
1. **Checkpoint validation failing**: Headers sync stops at checkpoint heights with hash mismatch
   - Need to investigate if checkpoints are correct for the network being tested
   - Added detailed logging to `validate_header::is_checkpoint_conflict()`

2. **Cache flush on destruction**: Warning appears when organizer is destroyed with unflushed headers
   - This is expected behavior when sync fails, but should flush on graceful shutdown

3. **Single-peer sync**: Currently syncs from one peer only
   - Future: parallel downloads from multiple peers

### Block Sync
- Block download phase (`sync_blocks`) is implemented but untested
- `block_chain::organize()` integration needs validation

## TODO / Next Steps

### High Priority
1. **Fix checkpoint validation**
   - Verify checkpoint hashes match the target network (mainnet vs testnet vs chipnet)
   - Add network-specific checkpoint loading

2. **Test block download phase**
   - Validate `getdata` / `block` message exchange
   - Test `block_chain::organize()` integration

3. **Graceful shutdown**
   - Ensure `header_organizer::flush()` is called on clean shutdown
   - Add proper cancellation to `sync_session::run()`

### Medium Priority
4. **Parallel block downloads**
   - Request blocks from multiple peers simultaneously
   - Implement download scheduling/prioritization

5. **Sync progress reporting**
   - Add callbacks/channels for sync progress updates
   - Integrate with TUI dashboard

6. **Error recovery**
   - Handle peer disconnection during sync
   - Automatic peer rotation on failure

### Low Priority
7. **Performance optimization**
   - Profile header validation performance
   - Optimize memory allocation patterns

8. **Delete deprecated code**
   - Remove `deprecated/` folders after full validation
   - Clean up unused callback-based infrastructure

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                         full_node                                │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐ │
│  │  p2p_node   │  │ block_chain │  │     sync_session        │ │
│  │             │  │             │  │  ┌─────────────────────┐│ │
│  │ peer_manager│  │ organize()  │◄─┼──│  header_organizer   ││ │
│  │             │  │ organize_   │  │  │  ┌────────────────┐ ││ │
│  │peer_sessions│  │ headers_    │  │  │  │validate_header │ ││ │
│  │             │  │ batch()     │  │  │  └────────────────┘ ││ │
│  └─────────────┘  └─────────────┘  │  └─────────────────────┘│ │
│         │                          │           │              │ │
│         └──────────────────────────┼───────────┘              │ │
│              peer_session::        │                          │ │
│              send/receive          │                          │ │
└─────────────────────────────────────────────────────────────────┘
```

## File Inventory

### New Files (this PR)
- `src/blockchain/include/kth/blockchain/pools/header_organizer.hpp`
- `src/blockchain/src/pools/header_organizer.cpp`
- `src/blockchain/include/kth/blockchain/validate/validate_header.hpp`
- `src/blockchain/src/validate/validate_header.cpp`
- `src/node/include/kth/node/sync_session.hpp`
- `src/node/src/sync_session.cpp`
- `src/infrastructure/include/kth/infrastructure/utility/broadcaster.hpp`
- `src/infrastructure/include/kth/infrastructure/utility/system_memory.hpp`
- `src/infrastructure/src/utility/system_memory.cpp`

### Modified Files (key changes)
- `src/node/src/full_node.cpp` - Integrated `sync_from_best_peer()`
- `src/blockchain/src/interface/block_chain.cpp` - Added `organize_headers_batch()`
- `src/blockchain/include/kth/blockchain.hpp` - Export new headers

## Testing Notes

### Manual Testing Performed
- Compilation: OK
- Node startup: OK
- Peer connections: OK (connects to mainnet peers)
- Headers reception: OK (receives 2000 headers per batch)
- Headers validation: Partial (fails at checkpoints, needs investigation)
- Block download: Not tested

### Automated Tests Needed
- Unit tests for `validate_header`
- Unit tests for `header_organizer`
- Integration tests for `sync_session`
