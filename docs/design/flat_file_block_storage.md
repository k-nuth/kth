# Flat File Block Storage Design

## Overview

Replace LMDB block storage with sequential flat files for improved I/O performance during IBD.
Based on BCHN's implementation with adaptations for Knuth's architecture.

## Problem

LMDB's B+ tree structure causes random I/O when reading blocks sequentially:
- Sequential read: ~2 GB/s
- Random read (LMDB pattern): ~100 MB/s
- Result: 20x slower than optimal for IBD

## Solution

Store blocks in append-only flat files (`blk*.dat`), enabling sequential I/O during IBD.

## File Structure

```
blocks/
  blk00000.dat   # Blocks, max 128 MiB each
  blk00001.dat
  ...
  rev00000.dat   # Undo data for blk00000
  rev00001.dat
  ...
```

## On-Disk Format

### Block File (blk*.dat)

```
+--------+------+------------+--------+------+------------+
| magic  | size | block_data | magic  | size | block_data | ...
+--------+------+------------+--------+------+------------+
  4 bytes  4 bytes  variable    4 bytes  4 bytes  variable
```

- **magic**: Network magic bytes (e.g., `0xE3E1F3E8` for BCH mainnet)
- **size**: Block serialized size (uint32_t, little-endian)
- **block_data**: Raw serialized block

### Undo File (rev*.dat)

```
+--------+------+-----------+----------+
| magic  | size | undo_data | checksum |
+--------+------+-----------+----------+
  4 bytes  4 bytes  variable   32 bytes
```

- **undo_data**: For each non-coinbase tx input, the spent UTXO:
  - height * 2 + is_coinbase (varint)
  - CTxOut (compressed)
- **checksum**: SHA256(prev_block_hash || undo_data)

## Constants

```cpp
constexpr uint32_t MAX_BLOCKFILE_SIZE = 0x8000000;    // 128 MiB
constexpr uint32_t BLOCKFILE_CHUNK_SIZE = 0x1000000;  // 16 MiB (pre-allocation)
constexpr uint32_t UNDOFILE_CHUNK_SIZE = 0x100000;    // 1 MiB
```

## New Types

### flat_file_pos

```cpp
struct flat_file_pos {
    int32_t file{-1};      // File number (-1 = null)
    uint32_t pos{0};       // Byte offset within file

    bool is_null() const { return file == -1; }
    void set_null() { file = -1; pos = 0; }
};
```

### flat_file_seq

```cpp
class flat_file_seq {
public:
    flat_file_seq(std::filesystem::path dir, std::string_view prefix, size_t chunk_size);

    std::filesystem::path file_name(flat_file_pos const& pos) const;
    FILE* open(flat_file_pos const& pos, bool read_only = false);
    size_t allocate(flat_file_pos const& pos, size_t add_size, bool& out_of_space);
    bool flush(flat_file_pos const& pos, bool finalize = false);

private:
    std::filesystem::path dir_;
    std::string prefix_;
    size_t chunk_size_;
};
```

### block_file_info

```cpp
struct block_file_info {
    uint32_t blocks{0};        // Number of blocks in file
    uint32_t size{0};          // Used bytes in block file
    uint32_t undo_size{0};     // Used bytes in undo file
    uint32_t height_first{0};  // Lowest block height
    uint32_t height_last{0};   // Highest block height
    uint64_t time_first{0};    // Earliest block time
    uint64_t time_last{0};     // Latest block time
};
```

## header_index Extension

Add new fields to track block location:

```cpp
// Existing fields in header_index...

// NEW: Block file location
std::vector<int16_t> file_numbers_;      // Which blk*.dat file (-1 = no data)
std::vector<uint32_t> data_positions_;   // Byte offset in blk file
std::vector<uint32_t> undo_positions_;   // Byte offset in rev file (0 = no undo)
```

New methods:

```cpp
// Setters (called when block is stored)
void set_block_pos(index_t idx, int16_t file, uint32_t pos);
void set_undo_pos(index_t idx, uint32_t pos);

// Getters
flat_file_pos get_block_pos(index_t idx) const;
flat_file_pos get_undo_pos(index_t idx) const;
bool has_block_data(index_t idx) const;
bool has_undo_data(index_t idx) const;
```

## block_store Class

High-level API for block storage:

```cpp
class block_store {
public:
    explicit block_store(std::filesystem::path blocks_dir,
                         std::array<uint8_t, 4> magic);

    // Write operations
    flat_file_pos save_block(domain::chain::block const& block,
                             uint32_t height);

    bool write_undo_data(block_undo const& undo,
                         uint32_t height,
                         hash_digest const& prev_hash);

    // Read operations
    std::expected<domain::chain::block, error_code>
    read_block(flat_file_pos const& pos) const;

    std::expected<data_chunk, error_code>
    read_block_raw(flat_file_pos const& pos) const;

    std::expected<block_undo, error_code>
    read_undo(flat_file_pos const& pos,
              hash_digest const& prev_hash) const;

    // Batch read (sequential, for IBD)
    std::expected<std::vector<data_chunk>, error_code>
    read_blocks_raw(std::vector<flat_file_pos> const& positions) const;

    // Maintenance
    void flush(bool finalize = false);
    uint64_t calculate_disk_usage() const;

private:
    std::filesystem::path blocks_dir_;
    std::array<uint8_t, 4> magic_;
    flat_file_seq block_files_;
    flat_file_seq undo_files_;

    std::vector<block_file_info> file_info_;
    int32_t last_block_file_{0};

    mutable std::mutex mutex_;

    flat_file_pos find_block_pos(uint32_t add_size, uint32_t height, uint64_t time);
    flat_file_pos find_undo_pos(int32_t file, uint32_t add_size);
};
```

## block_undo Structure

```cpp
struct tx_undo {
    std::vector<utxo_entry> prev_outputs;  // Spent UTXOs for this tx
};

struct block_undo {
    std::vector<tx_undo> tx_undos;  // One per tx (except coinbase)

    // Serialization
    data_chunk to_data() const;
    static std::expected<block_undo, error_code> from_data(byte_reader& reader);
};
```

## IBD Flow

### Writing (during sync)

```cpp
// 1. Download block
auto block = download_block(hash);

// 2. Save to flat file
auto pos = block_store.save_block(block, height);

// 3. Update header_index
header_index.set_block_pos(idx, pos.file, pos.pos);
header_index.add_status(idx, header_status::have_data);

// 4. When connecting block, generate and save undo
auto undo = generate_undo(block, utxo_view);
block_store.write_undo_data(undo, height, prev_hash);
header_index.set_undo_pos(idx, undo_pos.pos);
header_index.add_status(idx, header_status::have_undo);
```

### Reading (batch for UTXO building)

```cpp
// 1. Get positions for range
std::vector<flat_file_pos> positions;
for (uint32_t h = from; h <= to; ++h) {
    auto idx = header_index.find_by_height(h);
    positions.push_back(header_index.get_block_pos(idx));
}

// 2. Read sequentially (positions are sorted by file/offset for IBD)
auto raw_blocks = block_store.read_blocks_raw(positions);

// 3. Deserialize outside of I/O
for (auto& raw : raw_blocks) {
    auto block = domain::chain::block::from_data(raw);
    // process...
}
```

## Reorg Handling

```cpp
// Disconnect block
auto idx = header_index.find(block_hash);
auto undo_pos = header_index.get_undo_pos(idx);
auto prev_hash = header_index.get_prev_block_hash(idx);

auto undo = block_store.read_undo(undo_pos, prev_hash);
apply_undo(undo, utxo_set);  // Restore spent UTXOs
```

## Migration Strategy

1. **Phase 1**: Implement flat file system alongside LMDB
2. **Phase 2**: New syncs use flat files for blocks
3. **Phase 3**: Migration tool for existing LMDB databases
4. **Phase 4**: Remove LMDB block storage (keep for UTXO if needed)

## Files to Create

1. `src/database/include/kth/database/flat_file_pos.hpp`
2. `src/database/include/kth/database/flat_file_seq.hpp`
3. `src/database/include/kth/database/block_store.hpp`
4. `src/database/include/kth/database/block_undo.hpp`
5. `src/database/src/flat_file_seq.cpp`
6. `src/database/src/block_store.cpp`
7. `src/database/src/block_undo.cpp`

## Memory Estimation

For 1M blocks:
- file_numbers_: 2 MB (1M × 2 bytes)
- data_positions_: 4 MB (1M × 4 bytes)
- undo_positions_: 4 MB (1M × 4 bytes)
- **Total addition to header_index**: ~10 MB

## Performance Expectations

| Operation | LMDB | Flat Files |
|-----------|------|------------|
| Write block | Random I/O | Sequential append |
| Read 1000 blocks | ~10 sec | ~0.5 sec |
| IBD (930k blocks) | Hours | Minutes |
