// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_FLAT_FILE_POS_HPP
#define KTH_DATABASE_FLAT_FILE_POS_HPP

#include <cstdint>
#include <string>

#include <fmt/format.h>

namespace kth::database {

/// Position within a flat file sequence (blk*.dat or rev*.dat).
/// Equivalent to BCHN's FlatFilePos.
struct flat_file_pos {
    int32_t file{-1};    // File number (-1 = null/invalid)
    uint32_t pos{0};     // Byte offset within the file

    constexpr flat_file_pos() = default;

    constexpr flat_file_pos(int32_t file_num, uint32_t position)
        : file(file_num), pos(position)
    {}

    [[nodiscard]]
    constexpr bool is_null() const noexcept {
        return file == -1;
    }

    constexpr void set_null() noexcept {
        file = -1;
        pos = 0;
    }

    [[nodiscard]]
    constexpr bool operator==(flat_file_pos const& other) const noexcept {
        return file == other.file && pos == other.pos;
    }

    [[nodiscard]]
    constexpr bool operator!=(flat_file_pos const& other) const noexcept {
        return !(*this == other);
    }

    // For sorting by file then position (useful for sequential reads)
    [[nodiscard]]
    constexpr bool operator<(flat_file_pos const& other) const noexcept {
        if (file != other.file) {
            return file < other.file;
        }
        return pos < other.pos;
    }

    [[nodiscard]]
    std::string to_string() const {
        return fmt::format("flat_file_pos(file={}, pos={})", file, pos);
    }
};

/// Metadata about a single block/undo file pair.
struct block_file_info {
    uint32_t blocks{0};        // Number of blocks stored in file
    uint32_t size{0};          // Used bytes in block file (blk*.dat)
    uint32_t undo_size{0};     // Used bytes in undo file (rev*.dat)
    uint32_t height_first{0};  // Lowest block height in file
    uint32_t height_last{0};   // Highest block height in file
    uint64_t time_first{0};    // Earliest block timestamp
    uint64_t time_last{0};     // Latest block timestamp

    constexpr block_file_info() = default;

    void add_block(uint32_t height, uint64_t time) noexcept {
        if (blocks == 0 || height < height_first) {
            height_first = height;
        }
        if (blocks == 0 || time < time_first) {
            time_first = time;
        }
        ++blocks;
        if (height > height_last) {
            height_last = height;
        }
        if (time > time_last) {
            time_last = time;
        }
    }

    [[nodiscard]]
    std::string to_string() const {
        return fmt::format(
            "block_file_info(blocks={}, size={}, undo_size={}, heights={}...{}, time={}...{})",
            blocks, size, undo_size, height_first, height_last, time_first, time_last
        );
    }
};

} // namespace kth::database

#endif // KTH_DATABASE_FLAT_FILE_POS_HPP
