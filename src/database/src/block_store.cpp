// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/block_store.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/container_sink.hpp>
#include <kth/infrastructure/utility/ostream_writer.hpp>

namespace kth::database {

namespace {

// Size of block file header: magic (4) + size (4)
constexpr size_t BLOCK_HEADER_SIZE = 8;

// Size of undo file header: magic (4) + size (4)
constexpr size_t UNDO_HEADER_SIZE = 8;

// Size of undo checksum (SHA256)
constexpr size_t UNDO_CHECKSUM_SIZE = 32;

} // anonymous namespace

block_store::block_store(std::filesystem::path blocks_dir, magic_t magic)
    : blocks_dir_(std::move(blocks_dir))
    , magic_(magic)
    , block_files_(blocks_dir_, "blk", BLOCKFILE_CHUNK_SIZE)
    , undo_files_(blocks_dir_, "rev", UNDOFILE_CHUNK_SIZE)
{
    file_info_.resize(1);  // Start with at least one file info entry
}

bool block_store::initialize() {
    std::lock_guard lock(mutex_);

    // Create directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(blocks_dir_, ec);
    if (ec) {
        spdlog::error("block_store::initialize: Failed to create directory {}: {}",
                     blocks_dir_.string(), ec.message());
        return false;
    }

    // Scan existing files to restore state
    // TODO: Load file_info_ from LevelDB or similar persistent index
    // For now, just detect the last file number
    int file_num = 0;
    while (true) {
        flat_file_pos pos{file_num, 0};
        auto path = block_files_.file_name(pos);
        if (!std::filesystem::exists(path)) {
            break;
        }
        ++file_num;
    }

    if (file_num > 0) {
        last_block_file_ = file_num - 1;
        file_info_.resize(file_num);
    }

    spdlog::info("block_store: initialized with {} block files", file_info_.size());
    return true;
}

// =============================================================================
// Write Operations
// =============================================================================

flat_file_pos block_store::save_block(domain::chain::block const& block, uint32_t height) {
    auto const raw = block.to_data(true);  // wire=true
    return save_block_raw(raw, height, block.header().timestamp());
}

flat_file_pos block_store::save_block_raw(data_chunk const& raw_block, uint32_t height, uint64_t timestamp) {
    std::lock_guard lock(mutex_);

    auto const block_size = static_cast<uint32_t>(raw_block.size());
    auto pos = find_block_pos(block_size + BLOCK_HEADER_SIZE, height, timestamp);

    if (pos.is_null()) {
        spdlog::error("block_store::save_block_raw: Failed to find position");
        return {};
    }

    if (!write_block_to_disk(raw_block, pos)) {
        return {};
    }

    return pos;
}

flat_file_pos block_store::write_undo(block_undo const& undo, int32_t file_num, hash_digest const& prev_hash) {
    std::lock_guard lock(mutex_);

    auto const undo_size = static_cast<uint32_t>(undo.serialized_size());
    auto pos = find_undo_pos(file_num, undo_size + UNDO_HEADER_SIZE + UNDO_CHECKSUM_SIZE);

    if (pos.is_null()) {
        spdlog::error("block_store::write_undo: Failed to find position");
        return {};
    }

    if (!write_undo_to_disk(undo, pos, prev_hash)) {
        return {};
    }

    return pos;
}

// =============================================================================
// Read Operations
// =============================================================================

std::expected<domain::chain::block, result_code>
block_store::read_block(flat_file_pos const& pos) const {
    auto raw_result = read_block_raw(pos);
    if (!raw_result) {
        return std::unexpected(raw_result.error());
    }

    byte_reader reader(*raw_result);
    auto block = domain::chain::block::from_data(reader, true);  // wire=true
    if (!block) {
        return std::unexpected(result_code::other);
    }

    return std::move(*block);
}

std::expected<data_chunk, result_code>
block_store::read_block_raw(flat_file_pos const& pos) const {
    if (pos.is_null()) {
        return std::unexpected(result_code::key_not_found);
    }

    // Open file and seek to position
    FILE* file = block_files_.open(pos, true);
    if (!file) {
        spdlog::error("block_store::read_block_raw: Failed to open file for {}", pos.to_string());
        return std::unexpected(result_code::other);
    }

    // Seek back to read the header (we're positioned at block data)
    if (std::fseek(file, -static_cast<long>(BLOCK_HEADER_SIZE), SEEK_CUR) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Read header: magic + size
    std::array<uint8_t, 4> file_magic;
    uint32_t block_size;

    if (std::fread(file_magic.data(), 1, 4, file) != 4) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    if (std::fread(&block_size, sizeof(block_size), 1, file) != 1) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Verify magic
    if (file_magic != magic_) {
        spdlog::error("block_store::read_block_raw: Magic mismatch at {}", pos.to_string());
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Read block data
    data_chunk data(block_size);
    if (std::fread(data.data(), 1, block_size, file) != block_size) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    std::fclose(file);
    return data;
}

std::expected<uint32_t, result_code>
block_store::read_block_size(flat_file_pos const& pos) const {
    if (pos.is_null()) {
        return std::unexpected(result_code::key_not_found);
    }

    FILE* file = block_files_.open(pos, true);
    if (!file) {
        return std::unexpected(result_code::other);
    }

    // Seek back to read the header
    if (std::fseek(file, -static_cast<long>(BLOCK_HEADER_SIZE), SEEK_CUR) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Skip magic, read size
    if (std::fseek(file, 4, SEEK_CUR) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    uint32_t block_size;
    if (std::fread(&block_size, sizeof(block_size), 1, file) != 1) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    std::fclose(file);
    return block_size;
}

std::expected<std::vector<data_chunk>, result_code>
block_store::read_blocks_raw(std::vector<flat_file_pos> const& positions) const {
    if (positions.empty()) {
        return std::vector<data_chunk>{};
    }

    std::vector<data_chunk> results;
    results.reserve(positions.size());

    // Group by file for sequential I/O
    // For now, simple implementation - read each block individually
    // TODO: Optimize by grouping positions by file and reading sequentially

    for (auto const& pos : positions) {
        auto result = read_block_raw(pos);
        if (!result) {
            return std::unexpected(result.error());
        }
        results.push_back(std::move(*result));
    }

    return results;
}

std::expected<block_undo, result_code>
block_store::read_undo(flat_file_pos const& pos, hash_digest const& prev_hash) const {
    if (pos.is_null()) {
        return std::unexpected(result_code::key_not_found);
    }

    // Open undo file
    FILE* file = undo_files_.open(pos, true);
    if (!file) {
        spdlog::error("block_store::read_undo: Failed to open file for {}", pos.to_string());
        return std::unexpected(result_code::other);
    }

    // Read header: magic + size
    std::array<uint8_t, 4> file_magic;
    uint32_t undo_size;

    // Seek back to header position
    if (std::fseek(file, -static_cast<long>(UNDO_HEADER_SIZE), SEEK_CUR) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    if (std::fread(file_magic.data(), 1, 4, file) != 4) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    if (std::fread(&undo_size, sizeof(undo_size), 1, file) != 1) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Verify magic
    if (file_magic != magic_) {
        spdlog::error("block_store::read_undo: Magic mismatch at {}", pos.to_string());
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Read undo data + checksum
    data_chunk data(undo_size + UNDO_CHECKSUM_SIZE);
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }
    std::fclose(file);

    // Extract undo data and checksum
    data_chunk undo_data(data.begin(), data.begin() + undo_size);
    hash_digest stored_checksum;
    std::copy(data.begin() + undo_size, data.end(), stored_checksum.begin());

    // Verify checksum: SHA256(prev_hash || undo_data)
    data_chunk checksum_input;
    checksum_input.reserve(prev_hash.size() + undo_data.size());
    checksum_input.insert(checksum_input.end(), prev_hash.begin(), prev_hash.end());
    checksum_input.insert(checksum_input.end(), undo_data.begin(), undo_data.end());

    auto computed_checksum = bitcoin_hash(checksum_input);
    if (computed_checksum != stored_checksum) {
        spdlog::error("block_store::read_undo: Checksum mismatch at {}", pos.to_string());
        return std::unexpected(result_code::other);
    }

    // Deserialize undo data
    byte_reader reader(undo_data);
    return block_undo::from_data(reader);
}

// =============================================================================
// Maintenance
// =============================================================================

void block_store::flush(bool finalize) {
    std::lock_guard lock(mutex_);

    flat_file_pos block_pos{last_block_file_, file_info_[last_block_file_].size};
    flat_file_pos undo_pos{last_block_file_, file_info_[last_block_file_].undo_size};

    block_files_.flush(block_pos, finalize);
    undo_files_.flush(undo_pos, finalize);
}

uint64_t block_store::calculate_disk_usage() const {
    std::lock_guard lock(mutex_);

    uint64_t total = 0;
    for (auto const& info : file_info_) {
        total += info.size + info.undo_size;
    }
    return total;
}

size_t block_store::file_count() const {
    std::lock_guard lock(mutex_);
    return file_info_.size();
}

block_file_info const& block_store::file_info(size_t index) const {
    std::lock_guard lock(mutex_);
    return file_info_.at(index);
}

// =============================================================================
// Private Methods
// =============================================================================

flat_file_pos block_store::find_block_pos(uint32_t add_size, uint32_t height, uint64_t time) {
    // Check if current file has space
    auto file_num = static_cast<uint32_t>(last_block_file_);

    if (file_info_.size() <= file_num) {
        file_info_.resize(file_num + 1);
    }

    // If current file would exceed max size, create new file
    while (file_info_[file_num].size > 0 &&
           file_info_[file_num].size + add_size >= MAX_BLOCKFILE_SIZE) {
        ++file_num;
        if (file_info_.size() <= file_num) {
            file_info_.resize(file_num + 1);
        }
    }

    flat_file_pos pos{static_cast<int32_t>(file_num), file_info_[file_num].size};

    // Update file info
    if (static_cast<int32_t>(file_num) != last_block_file_) {
        spdlog::info("Leaving block file {}: {}", last_block_file_,
                    file_info_[last_block_file_].to_string());
        // Flush old file
        flat_file_pos old_pos{last_block_file_, file_info_[last_block_file_].size};
        block_files_.flush(old_pos, true);
        last_block_file_ = static_cast<int32_t>(file_num);
    }

    file_info_[file_num].add_block(height, time);
    file_info_[file_num].size += add_size;

    // Pre-allocate space
    bool out_of_space = false;
    block_files_.allocate(pos, add_size, out_of_space);
    if (out_of_space) {
        spdlog::error("block_store::find_block_pos: Disk space is low!");
        return {};
    }

    return pos;
}

flat_file_pos block_store::find_undo_pos(int32_t file_num, uint32_t add_size) {
    if (file_num < 0 || static_cast<size_t>(file_num) >= file_info_.size()) {
        return {};
    }

    flat_file_pos pos{file_num, file_info_[file_num].undo_size};
    file_info_[file_num].undo_size += add_size;

    // Pre-allocate space
    bool out_of_space = false;
    undo_files_.allocate(pos, add_size, out_of_space);
    if (out_of_space) {
        spdlog::error("block_store::find_undo_pos: Disk space is low!");
        return {};
    }

    return pos;
}

bool block_store::write_block_to_disk(data_chunk const& raw_block, flat_file_pos& pos) {
    FILE* file = block_files_.open(pos, false);
    if (!file) {
        spdlog::error("block_store::write_block_to_disk: Failed to open file");
        return false;
    }

    // Write header: magic + size
    auto const block_size = static_cast<uint32_t>(raw_block.size());

    if (std::fwrite(magic_.data(), 1, magic_.size(), file) != magic_.size()) {
        std::fclose(file);
        return false;
    }

    if (std::fwrite(&block_size, sizeof(block_size), 1, file) != 1) {
        std::fclose(file);
        return false;
    }

    // Update pos to point to actual block data (after header)
    long data_pos = std::ftell(file);
    if (data_pos < 0) {
        std::fclose(file);
        return false;
    }
    pos.pos = static_cast<uint32_t>(data_pos);

    // Write block data
    if (std::fwrite(raw_block.data(), 1, raw_block.size(), file) != raw_block.size()) {
        std::fclose(file);
        return false;
    }

    std::fclose(file);
    return true;
}

bool block_store::write_undo_to_disk(block_undo const& undo, flat_file_pos& pos, hash_digest const& prev_hash) {
    FILE* file = undo_files_.open(pos, false);
    if (!file) {
        spdlog::error("block_store::write_undo_to_disk: Failed to open file");
        return false;
    }

    auto const undo_data = undo.to_data();
    auto const undo_size = static_cast<uint32_t>(undo_data.size());

    // Write header: magic + size
    if (std::fwrite(magic_.data(), 1, magic_.size(), file) != magic_.size()) {
        std::fclose(file);
        return false;
    }

    if (std::fwrite(&undo_size, sizeof(undo_size), 1, file) != 1) {
        std::fclose(file);
        return false;
    }

    // Update pos to point to actual undo data (after header)
    long data_pos = std::ftell(file);
    if (data_pos < 0) {
        std::fclose(file);
        return false;
    }
    pos.pos = static_cast<uint32_t>(data_pos);

    // Write undo data
    if (std::fwrite(undo_data.data(), 1, undo_data.size(), file) != undo_data.size()) {
        std::fclose(file);
        return false;
    }

    // Calculate checksum: SHA256(prev_hash || undo_data)
    data_chunk checksum_input;
    checksum_input.reserve(prev_hash.size() + undo_data.size());
    checksum_input.insert(checksum_input.end(), prev_hash.begin(), prev_hash.end());
    checksum_input.insert(checksum_input.end(), undo_data.begin(), undo_data.end());

    auto checksum = bitcoin_hash(checksum_input);

    // Write checksum
    if (std::fwrite(checksum.data(), 1, checksum.size(), file) != checksum.size()) {
        std::fclose(file);
        return false;
    }

    std::fclose(file);
    return true;
}

} // namespace kth::database
