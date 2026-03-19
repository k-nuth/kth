// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_STORE_HPP
#define KTH_DATABASE_BLOCK_STORE_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <vector>

#include <kth/database/define.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/flat_file_pos.hpp>
#include <kth/database/flat_file_seq.hpp>
#include <kth/domain.hpp>

namespace kth::database {

/// High-level API for storing blocks and undo data in flat files.
/// Thread-safe.
struct KD_API block_store {
    using magic_t = std::array<uint8_t, 4>;

    /// Construct a block store.
    /// @param blocks_dir Directory for block and undo files.
    /// @param magic Network magic bytes (4 bytes).
    explicit 
    block_store(std::filesystem::path blocks_dir, magic_t magic);

    ~block_store() = default;

    // Non-copyable, non-movable (contains mutex)
    block_store(block_store const&) = delete;
    block_store& operator=(block_store const&) = delete;
    block_store(block_store&&) = delete;
    block_store& operator=(block_store&&) = delete;

    // =========================================================================
    // Initialization
    // =========================================================================

    /// Initialize the block store.
    /// Scans existing files and restores file_info state.
    /// @return True on success.
    [[nodiscard]]
    bool initialize();

    // =========================================================================
    // Write Operations
    // =========================================================================

    /// Save a block to disk.
    /// @param block The block to save.
    /// @param height Block height.
    /// @return Position where block was saved, or null position on error.
    [[nodiscard]]
    flat_file_pos save_block(domain::chain::block const& block, uint32_t height);

    /// Save raw block data to disk.
    /// @param raw_block Serialized block data.
    /// @param height Block height.
    /// @param timestamp Block timestamp.
    /// @return Position where block was saved, or null position on error.
    [[nodiscard]]
    flat_file_pos save_block_raw(data_chunk const& raw_block, uint32_t height, uint64_t timestamp);

    /// Allocate space for a raw block without writing data.
    /// Thread-safe: serializes access to allocation state via internal mutex.
    /// @param raw_block_size Size of the raw block data (excluding header).
    /// @param height Block height.
    /// @param timestamp Block timestamp.
    /// @return Position of the allocated region (header start), or null on error.
    [[nodiscard]]
    flat_file_pos allocate_block_space(uint32_t raw_block_size, uint32_t height, uint64_t timestamp);

    /// Write a raw block at a pre-allocated position.
    /// Thread-safe for non-overlapping positions (each call opens its own FILE*).
    /// @param raw_block Serialized block data.
    /// @param header_pos Position returned by allocate_block_space (header start).
    /// @return Position of block data (after header), or null on error.
    [[nodiscard]]
    flat_file_pos write_block_at(data_chunk const& raw_block, flat_file_pos header_pos);

    /// Write undo data for a block.
    /// @param undo The undo data.
    /// @param file_num File number where the block is stored.
    /// @param prev_hash Hash of the previous block (for checksum).
    /// @return Position where undo was saved, or null position on error.
    [[nodiscard]]
    flat_file_pos write_undo(block_undo const& undo, int32_t file_num, hash_digest const& prev_hash);

    // =========================================================================
    // Read Operations
    // =========================================================================

    /// Read and deserialize a block from disk.
    /// @param pos Position of the block.
    /// @return Block or error.
    [[nodiscard]]
    std::expected<domain::chain::block, result_code>
    read_block(flat_file_pos const& pos) const;

    /// Read raw block data from disk.
    /// @param pos Position of the block.
    /// @return Raw block data or error.
    [[nodiscard]]
    std::expected<data_chunk, result_code>
    read_block_raw(flat_file_pos const& pos) const;

    /// Read a raw transaction from a flat file at a known absolute offset.
    /// Parses the tx incrementally (version, inputs, outputs, locktime) to
    /// determine its total size, then returns the raw bytes.
    /// @param pos Position of the transaction start (file number + byte offset).
    /// @return Raw transaction bytes or error.
    [[nodiscard]]
    std::expected<data_chunk, result_code>
    read_tx_raw(flat_file_pos const& pos) const;

    /// Read block size from disk.
    /// @param pos Position of the block.
    /// @return Block size or error.
    [[nodiscard]]
    std::expected<uint32_t, result_code>
    read_block_size(flat_file_pos const& pos) const;

    /// Read multiple blocks as raw data (for sequential IBD).
    /// Positions should be sorted by file/offset for best performance.
    /// @param positions Positions of blocks to read.
    /// @return Vector of raw block data or error.
    [[nodiscard]]
    std::expected<std::vector<data_chunk>, result_code>
    read_blocks_raw(std::vector<flat_file_pos> const& positions) const;

    /// Read undo data from disk.
    /// @param pos Position of the undo data.
    /// @param prev_hash Hash of the previous block (for checksum verification).
    /// @return Undo data or error.
    [[nodiscard]]
    std::expected<block_undo, result_code>
    read_undo(flat_file_pos const& pos, hash_digest const& prev_hash) const;

    /// Scan all block files and invoke callback for each block found.
    /// Used on startup to rebuild in-memory block position index.
    /// Callback receives (file_number, data_position, block_hash).
    /// @return Number of blocks scanned.
    using block_position_callback = std::function<void(int32_t, uint32_t, hash_digest const&)>;

    [[nodiscard]]
    size_t scan_block_positions(block_position_callback const& callback) const;

    // =========================================================================
    // Maintenance
    // =========================================================================

    /// Flush all pending writes to disk.
    /// @param finalize True to truncate files to actual size.
    void flush(bool finalize = false);

    /// Calculate total disk usage.
    /// @return Total bytes used by block and undo files.
    [[nodiscard]]
    uint64_t calculate_disk_usage() const;

    /// Get the number of block files.
    [[nodiscard]]
    size_t file_count() const;

    /// Get info for a specific file.
    [[nodiscard]]
    block_file_info const& file_info(size_t index) const;

    /// Get the blocks directory.
    [[nodiscard]]
    std::filesystem::path const& directory() const { return blocks_dir_; }

private:
    /// Find position for a new block.
    [[nodiscard]]
    flat_file_pos find_block_pos(uint32_t add_size, uint32_t height, uint64_t time);

    /// Find position for undo data.
    [[nodiscard]]
    flat_file_pos find_undo_pos(int32_t file_num, uint32_t add_size);

    /// Write block to disk at position.
    [[nodiscard]]
    bool write_block_to_disk(data_chunk const& raw_block, flat_file_pos& pos);

    /// Write undo to disk at position.
    [[nodiscard]]
    bool write_undo_to_disk(block_undo const& undo, flat_file_pos& pos, hash_digest const& prev_hash);

    std::filesystem::path blocks_dir_;
    magic_t magic_;
    flat_file_seq block_files_;
    flat_file_seq undo_files_;

    std::vector<block_file_info> file_info_;
    int32_t last_block_file_{0};

    // Thread safety:
    // - allocate_block_space() must be called serially (single coroutine / single pool thread)
    // - write_block_at() is safe for concurrent calls to non-overlapping positions
    // - Reads don't modify internal state
};

} // namespace kth::database

#endif // KTH_DATABASE_BLOCK_STORE_HPP
