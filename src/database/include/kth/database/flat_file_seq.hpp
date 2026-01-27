// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_FLAT_FILE_SEQ_HPP
#define KTH_DATABASE_FLAT_FILE_SEQ_HPP

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>

#include <kth/database/flat_file_pos.hpp>

namespace kth::database {

/// Pre-allocation chunk size for block files (16 MiB)
inline constexpr size_t BLOCKFILE_CHUNK_SIZE = 0x1000000;

/// Pre-allocation chunk size for undo files (1 MiB)
inline constexpr size_t UNDOFILE_CHUNK_SIZE = 0x100000;

/// Maximum size of a single block file (128 MiB)
inline constexpr size_t MAX_BLOCKFILE_SIZE = 0x8000000;

/// Manages a sequence of numbered flat files (blk00000.dat, blk00001.dat, etc.)
/// Equivalent to BCHN's FlatFileSeq.
class flat_file_seq {
public:
    /// Construct a flat file sequence.
    /// @param dir Base directory for files.
    /// @param prefix File prefix (e.g., "blk" for blk00000.dat).
    /// @param chunk_size Pre-allocation chunk size in bytes.
    /// @throws std::invalid_argument if chunk_size is 0.
    flat_file_seq(std::filesystem::path dir, std::string prefix, size_t chunk_size);

    ~flat_file_seq() = default;

    // Non-copyable, movable
    flat_file_seq(flat_file_seq const&) = delete;
    flat_file_seq& operator=(flat_file_seq const&) = delete;
    flat_file_seq(flat_file_seq&&) = default;
    flat_file_seq& operator=(flat_file_seq&&) = default;

    /// Get the filename for a given position.
    /// @param pos Position (uses pos.file for file number).
    /// @return Full path, e.g., "/path/to/blocks/blk00001.dat"
    [[nodiscard]]
    std::filesystem::path file_name(flat_file_pos const& pos) const;

    /// Open a file at the given position.
    /// @param pos Position to open (seeks to pos.pos if non-zero).
    /// @param read_only True for read-only access.
    /// @return File handle or nullptr on failure.
    [[nodiscard]]
    FILE* open(flat_file_pos const& pos, bool read_only = false) const;

    /// Pre-allocate space in a file.
    /// @param pos Starting position.
    /// @param add_size Minimum bytes to allocate.
    /// @param out_of_space Set to true if allocation failed due to disk space.
    /// @return Number of bytes allocated.
    size_t allocate(flat_file_pos const& pos, size_t add_size, bool& out_of_space) const;

    /// Flush and optionally truncate a file.
    /// @param pos Position indicating the file and used size.
    /// @param finalize True to truncate to pos.pos bytes.
    /// @return True on success.
    bool flush(flat_file_pos const& pos, bool finalize = false) const;

    /// Get the base directory.
    [[nodiscard]]
    std::filesystem::path const& directory() const noexcept { return dir_; }

    /// Get the file prefix.
    [[nodiscard]]
    std::string const& prefix() const noexcept { return prefix_; }

    /// Get the chunk size.
    [[nodiscard]]
    size_t chunk_size() const noexcept { return chunk_size_; }

private:
    std::filesystem::path dir_;
    std::string prefix_;
    size_t chunk_size_;
};

/// Check if there's enough disk space.
/// @param path Path to check.
/// @param additional_size Required additional bytes.
/// @return True if enough space available.
[[nodiscard]]
bool check_disk_space(std::filesystem::path const& path, size_t additional_size);

/// Pre-allocate file space (platform-specific).
/// @param file File handle (must be open for writing).
/// @param offset Starting offset.
/// @param length Bytes to allocate.
/// @return True on success.
bool allocate_file_range(FILE* file, size_t offset, size_t length);

/// Truncate file to specified size.
/// @param file File handle.
/// @param size Target size.
/// @return True on success.
bool truncate_file(FILE* file, size_t size);

/// Commit file to disk (fsync).
/// @param file File handle.
/// @return True on success.
bool file_commit(FILE* file);

} // namespace kth::database

#endif // KTH_DATABASE_FLAT_FILE_SEQ_HPP
