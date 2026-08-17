// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_BLOCK_STORE_HPP
#define KTH_DATABASE_BLOCK_STORE_HPP

#include <array>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <kth/database/define.hpp>
#include <kth/database/block_undo.hpp>
#include <kth/database/flat_file_pos.hpp>
#include <kth/database/flat_file_seq.hpp>
#include <kth/database/native_file.hpp>
#include <kth/domain.hpp>

namespace kth::database {

/// High-level API for storing blocks and undo data in flat files.
/// NOT internally synchronised. `file_info_`, the append cursors and the
/// adoption state below are plain members: the store is driven from one thread
/// at a time by its owner, which is what block_chain does — `initialize()` and
/// both walks run during start(), and every writer begins after start() returns.
///
/// Said plainly because two comments here used to claim a mutex that this class
/// does not have.
struct KD_API block_store {
    using magic_t = std::array<uint8_t, 4>;

    /// Construct a block store.
    /// @param blocks_dir Directory for block and undo files.
    /// @param magic Network magic bytes (4 bytes).
    explicit 
    block_store(std::filesystem::path blocks_dir, magic_t magic);

    ~block_store() = default;

    // Non-copyable, non-movable
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
    /// Called from the owner's thread, like everything else here; see the note
    /// on the class.
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

    /// Write a block's undo record into the rev file matching `file_num`.
    /// `block_hash` identifies the owning block and is stored in the record —
    /// nothing else on disk can attribute it (#603). `prev_hash` seeds the
    /// checksum and is not stored.
    /// @return Position of the payload (just past the header), or null on error.
    [[nodiscard]]
    flat_file_pos write_undo(block_undo const& undo, int32_t file_num,
                             hash_digest const& block_hash, hash_digest const& prev_hash);

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

    /// Read a block's undo record. `block_hash` is the block the caller believes
    /// owns the record, and the record must agree — a wrong position would
    /// otherwise return some other block's undo, and the checksum cannot catch
    /// the sibling case, since siblings share its seed. `prev_hash` seeds the
    /// checksum, as at write time.
    [[nodiscard]] std::expected<block_undo, result_code>
    read_undo(flat_file_pos const& pos, hash_digest const& block_hash,
              hash_digest const& prev_hash) const;

    /// Scan all block files and invoke callback for each block found.
    /// Used on startup to rebuild in-memory block position index.
    /// Callback receives (file_number, data_position, block_hash).
    /// @return Number of blocks scanned.
    using block_position_callback = std::function<void(int32_t, uint32_t, hash_digest const&)>;

    /// One recovered undo record: where it is, and which block owns it.
    struct undo_location {
        int32_t file_number;
        uint32_t position;
        hash_digest block_hash;
    };

    /// Why a scan stopped. Only `clean_eof` means the files were whole; every
    /// other value has to keep the database from looking healthy, which is why
    /// they are named apart rather than folded into one failure.
    enum class undo_scan_status {
        clean_eof,          ///< Every record read; nothing left but empty space.
        legacy_format,      ///< A record written before it carried its block's hash.
        invalid_marker,     ///< A marker that is neither this format's nor the old one's.
        truncated_record,   ///< A header or payload that does not fit the file.
        invalid_size,       ///< A payload size that cannot be right.
        io_error,           ///< A seek or read failed.

        invalid_checksum,   ///< A record whose contents do not match its checksum.
        duplicate_record,   ///< Two records claiming the same block.
    };

    struct undo_scan_result {
        undo_scan_status status;
        std::vector<undo_location> found;   ///< Only meaningful when clean_eof.

        /// Records naming a block the index does not hold. Not an error: a
        /// restart rebuilds the index from the active chain only, so a branch
        /// that lost a reorganization is forgotten while its undo records stay
        /// in the files. They are counted rather than refused, because nothing
        /// on disk distinguishes that from a genuine disagreement — and the
        /// first is ordinary.
        size_t unattributed{0};
        int32_t file_number{-1};            ///< Where it stopped, for diagnosis.
        uint32_t position{0};
        hash_digest block_hash{};
    };

    /// Resolves a record's owning block to the hash its checksum is seeded with,
    /// so the scan can verify a record rather than trust its header. Returns
    /// nullopt when the block is not in the index, which is ordinary after a
    /// reorganization: see undo_scan_result::unattributed.
    using undo_parent_lookup = std::function<std::optional<hash_digest>(hash_digest const&)>;

    /// Why a walk of the blk files stopped, and where.
    ///
    /// It used to be a `size_t` count with a silent `break` at every anomaly, and
    /// that was tolerable only while nothing depended on where it stopped. It
    /// governs the write cursor now, so "it ended" and "it stopped understanding"
    /// have to be different answers: taking the second for the first would put
    /// the next block on top of data nobody could read (#668).
    enum class block_scan_status {
        clean_eof,          ///< Every record read, right up to the end of the file.
        clean_padding,      ///< Every record read; what follows is reserved space, all zero.
        open_failed,        ///< A file that exists could not be opened.
        truncated_header,   ///< Not enough room left for a record header.
        bad_magic,          ///< Four bytes that are neither this network's magic nor zeroes.
        invalid_size,       ///< A payload size that cannot be right.
        record_beyond_file, ///< A record that claims more bytes than the file holds.
        short_read,         ///< A read returned less than it was asked for.
        seek_failed,        ///< A seek over a payload failed.
    };

    struct block_scan_result {
        block_scan_status status{block_scan_status::clean_eof};
        size_t found{0};                ///< Records read and reported. Only meaningful when clean.
        int32_t file_number{-1};        ///< Where it stopped, for diagnosis.
        uint32_t position{0};

        [[nodiscard]] bool clean() const {
            return status == block_scan_status::clean_eof
                || status == block_scan_status::clean_padding;
        }
    };

    /// Walk the blk files and report every block position.
    ///
    /// The extents it measures are NOT returned. A caller cannot hand this store
    /// a cursor: the walk publishes what it measured, itself, and only when it
    /// finished cleanly on every file. Anything else leaves the block cursor
    /// unavailable and this store refusing to append (see `append_enabled`).
    [[nodiscard]]
    block_scan_result scan_block_positions(block_position_callback const& callback);

    /// Walk the rev files and recover every undo record, so the header index can
    /// be rebuilt at startup the way block positions already are.
    ///
    /// Each record is validated structurally — marker, size, bounds — and each
    /// one whose block the index still holds is validated fully, against that
    /// block's parent. Records naming a forgotten block cannot be checked further
    /// and are counted rather than trusted; the coverage of the connected chain
    /// is what protects against one of those being an active record in disguise. Nothing is
    /// reported until every file has been read, so a failure late in the last
    /// file cannot leave earlier records already applied — a half-restored index
    /// is worse than none, because it looks complete.
    /// Same contract as `scan_block_positions`: the extents stay here, and they
    /// are published only when every file read cleanly.
    [[nodiscard]]
    undo_scan_result scan_undo_positions(undo_parent_lookup const& parent_of);

    // =========================================================================
    // Appending is earned, not assumed (#668)
    // =========================================================================
    //
    // A rev or blk file is preallocated in whole chunks, so its size on disk is
    // almost always larger than the bytes written into it. `initialize()` used to
    // take that size as the write cursor, and after a restart the next record
    // landed on the chunk boundary — leaving a hole, and leaving the start after
    // that refusing to open the database.
    //
    // So the cursor is no longer guessed from the file system. It comes from the
    // walk that reads the records, and until BOTH walks have finished cleanly
    // this store refuses to hand out a write position at all. There is no
    // fallback to the file size: an unavailable cursor is an error a caller sees,
    // in every build, not an assertion that disappears in Release.
    //
    // @par The contract for a direct caller
    // `initialize()` → `scan_block_positions(...)` → `scan_undo_positions(...)` →
    // append. The two walks need the header index, so they cannot run inside
    // `initialize()`; on a database with no files they are trivially clean and
    // cost nothing.
    //
    // There is no `close()`: this store is destroyed and rebuilt, which is what
    // block_chain does, so a reopen starts from `initialize()` and earns the
    // cursors again. `initialize()` also clears both, so calling it twice on one
    // instance does not inherit the last run's answer.

    /// Whether a write position can be issued. False until both walks have
    /// completed cleanly, and false again after another `initialize()`.
    [[nodiscard]]
    bool append_enabled() const;

    // =========================================================================
    // Maintenance
    // =========================================================================

    /// Why a durability barrier over the undo files failed, and where.
    ///
    /// The file number is carried rather than logged: a caller that has to
    /// decide whether the node can continue needs to name what did not reach
    /// the disk, and a log line is the one place that answer cannot be acted
    /// on. This is a value, not the root of a hierarchy.
    struct undo_flush_error {
        result_code code;
        int32_t file_number;    ///< -1 when the failure was the directory barrier.
    };

    /// Put the undo records in `file_numbers` on stable storage.
    ///
    /// Takes the set of files a transition touched, because a batch that
    /// crosses a rotation writes undo into more than one rev*.dat and syncing
    /// only the last one leaves the rest to the page cache. Duplicates are
    /// expected — one number per block — and are normalized away here rather
    /// than at every call site.
    ///
    /// Stops at the first failure. A partial barrier is not a weaker guarantee,
    /// it is no guarantee: the caller cannot act on "some of it reached the
    /// disk" any differently than on "none of it did".
    ///
    /// Then the directory, unconditionally. Whether a given rev file is new
    /// could be tracked through the write path, to save one fsync per batch;
    /// a barrier that is skipped because the tracking was wrong costs a
    /// database, and the saving does not buy that risk. Where the platform has
    /// no directory barrier this is reported through `directory_durability()`
    /// and is not a failure.
    ///
    /// This replaces a `flush` that returned void, discarded both results it
    /// got, covered only the last block file and had no caller anywhere in the
    /// repo. The machinery underneath was correct and simply disconnected.
    [[nodiscard]]
    std::expected<void, undo_flush_error>
    flush_undo(std::span<int32_t const> file_numbers);

    /// What this platform can promise about publishing a new file's name.
    [[nodiscard]]
    directory_barrier directory_durability() const;

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
    bool write_undo_to_disk(block_undo const& undo, flat_file_pos& pos,
                            hash_digest const& block_hash, hash_digest const& prev_hash);

    std::filesystem::path blocks_dir_;
    magic_t magic_;
    flat_file_seq block_files_;
    flat_file_seq undo_files_;

    std::vector<block_file_info> file_info_;

    int32_t last_block_file_{0};

    /// Set only by a walk that finished cleanly on every file, and cleared by
    /// `initialize()`. Both are required before a position is issued.
    bool block_extents_adopted_{false};
    bool undo_extents_adopted_{false};
    // Appended last on purpose: the two flags fit in the padding that already
    // followed `last_block_file_`, so no existing member changes offset and
    // sizeof is unchanged. Placing them earlier would have shifted that member
    // for no reason.

    // Thread safety:
    // - allocate_block_space() must be called serially (single coroutine / single pool thread)
    // - write_block_at() is safe for concurrent calls to non-overlapping positions
    // - Reads don't modify internal state
};

} // namespace kth::database

#endif // KTH_DATABASE_BLOCK_STORE_HPP
