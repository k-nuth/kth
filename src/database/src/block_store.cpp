// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/block_store.hpp>

#include <limits>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

#include <boost/unordered/unordered_flat_set.hpp>

#include <spdlog/spdlog.h>
#include <kth/database/native_file.hpp>
#include <kth/infrastructure/utility/stats.hpp>

namespace kth::database {

namespace {

// Size of block file header: magic (4) + size (4)
constexpr size_t block_header_size = 8;

// The undo record's own marker, and it has to be its own rather than the network
// magic the blocks use. Records written before issue #603 begin with that same
// network magic followed by a size, so a reader expecting the current layout
// would take those four size bytes for the first of a hash and misread
// everything after. Two formats sharing a marker are two formats that cannot be
// told apart. Finding the network magic where this is expected is therefore not
// a corrupt record: it is a database written by an older build, and it is
// reported as such.
constexpr std::array<uint8_t, 4> undo_magic_v2{{'K', 'U', 'N', '2'}};

// undo marker (4) + owning block hash (32) + payload size (4). The hash is what
// makes a record self-identifying. Nothing already on disk could stand in for
// it: order cannot, because a rev file holds undo for a subset of its blk file's
// blocks in build order with no gap to mark the ones that never got any; and the
// checksum cannot, because it is seeded with the parent hash, so every sibling
// validates the same record — and two siblings built from the same transactions
// with different coinbases produce byte-identical undo data.
constexpr size_t undo_header_size = 40;

// Size of undo checksum (SHA256)
constexpr size_t undo_checksum_size = 32;

// A payload larger than this is not a record, it is a misread header. Undo data
// holds one entry per output a block spends, so this is far above anything a
// block could produce and only rules out nonsense.
constexpr uint32_t max_undo_size = 256u * 1024u * 1024u;

// The largest block record a blk file can legitimately hold.
//
// Derived, not chosen: `find_block_pos` starts a new file as soon as
// `size + add_size >= MAX_BLOCKFILE_SIZE`, so no record this store ever wrote
// can span more than one file's worth of bytes. `MAX_BLOCKFILE_SIZE` is the
// writer's own limit, which is why it is the reader's.
//
// Without it a corrupt size seeks past the end — which is legal — and the next
// iteration finds no room for a header and reports a clean ending. Corruption
// wearing the shape of EOF is the one outcome a cursor must never be built on.
constexpr uint64_t max_block_record = MAX_BLOCKFILE_SIZE - block_header_size;

// The record's checksum: the owning block's parent hash, then the payload. The
// parent seeds it and is not stored, which is why it cannot serve as identity —
// every sibling produces the same value.
// Whether everything from `offset` to `file_size` is zero. Reserved space a
// preallocated file never used looks exactly like that, and it is the only tail
// that may be taken for a clean end: anything non-zero after the records is
// either an interrupted write or damage, and treating it as the end would let it
// hide whatever follows.
// Three answers, because a caller that has to report WHY needs them apart: the
// tail is reserved space, the tail holds data, or the tail could not be read.
// The last two are both fail-closed, and collapsing them only makes the
// diagnosis worse.
//
// Read in fixed 64 KiB blocks from a buffer that does not grow with the padding:
// the last file of a real database carries a hundred-odd megabytes of reserved
// space, and neither mapping it nor allocating for it is warranted to answer a
// yes/no question.
// The file's size RIGHT NOW, not as some earlier pass remembered it. A snapshot
// taken at initialize() is stale the moment anything appends, and a walk bounded
// by a stale size stops early and calls it an ending.
std::optional<uint32_t> physical_size_of(std::filesystem::path const& path) {
    std::error_code ec;
    auto const size = std::filesystem::file_size(path, ec);
    if (ec) return std::nullopt;
    // Every offset in these files is a uint32_t, so a file larger than one can
    // address is not a file this reader can bound. Truncating it would hand the
    // walk a limit far inside the data and call whatever followed an ending.
    if (size > std::numeric_limits<uint32_t>::max()) return std::nullopt;
    // And by what the walk can SEEK to. `std::fseek` takes a `long`, which is
    // 32-bit on Windows: a size above LONG_MAX converts to a negative offset, the
    // seek does not fail in any useful way, and the walk reports an ending from
    // inside written data. A bound the reader cannot act on is not a bound.
    if (size > static_cast<uintmax_t>(std::numeric_limits<long>::max())) return std::nullopt;
    return static_cast<uint32_t>(size);
}

enum class tail_check { all_zero, has_data, unreadable };

tail_check check_tail(FILE* file, uint32_t offset, uint32_t file_size) {
    if (offset > file_size) return tail_check::unreadable;
    if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
        return tail_check::unreadable;
    }

    std::array<uint8_t, 64 * 1024> buffer{};
    uint32_t remaining = file_size - offset;
    while (remaining > 0) {
        auto const want = std::min<size_t>(remaining, buffer.size());
        if (std::fread(buffer.data(), 1, want, file) != want) {
            return tail_check::unreadable;
        }
        if (std::any_of(buffer.begin(), buffer.begin() + want,
                        [](uint8_t byte) { return byte != 0; })) {
            return tail_check::has_data;
        }
        remaining -= static_cast<uint32_t>(want);
    }
    return tail_check::all_zero;
}

bool tail_is_all_zero(FILE* file, uint32_t offset, uint32_t file_size) {
    return check_tail(file, offset, file_size) == tail_check::all_zero;
}

hash_digest undo_checksum(hash_digest const& prev_hash, data_chunk const& undo_data) {
    data_chunk input;
    input.reserve(prev_hash.size() + undo_data.size());
    input.insert(input.end(), prev_hash.begin(), prev_hash.end());
    input.insert(input.end(), undo_data.begin(), undo_data.end());
    return bitcoin_hash(input);
}

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

    // FIRST, and before any path that can return. Both cursors are unavailable
    // until a walk earns them, and that has to hold for a re-initialize as much
    // as for a fresh instance — otherwise a second call would inherit the last
    // run's answer about files it has not read yet.
    //
    // This is also why there is no close(): the store holds no handle between
    // calls — every read and write opens and closes its own FILE* — so the only
    // thing a close could do is set these two, which is what this already does,
    // on the one path that precedes reading anything.
    block_extents_adopted_ = false;
    undo_extents_adopted_ = false;

    // Create directory if it doesn't exist
    std::error_code ec;
    std::filesystem::create_directories(blocks_dir_, ec);
    if (ec) {
        spdlog::error("block_store::initialize: Failed to create directory {}: {}",
                     blocks_dir_.string(), ec.message());
        return false;
    }

    // Scan existing files and restore their sizes
    int file_num = 0;
    while (true) {
        flat_file_pos pos{file_num, 0};
        auto path = block_files_.file_name(pos);
        // With an error_code: the throwing overload turns an unreadable directory
        // into an exception out of initialize(), where every other failure here
        // is a `return false`. An error is not "the file is not there" either —
        // it is not knowing, which is a refusal.
        std::error_code exists_ec;
        auto const present = std::filesystem::exists(path, exists_ec);
        if (exists_ec) {
            spdlog::critical("block_store::initialize: {} could not be examined ({}); refusing "
                "to open on a directory this process cannot read",
                path.string(), exists_ec.message());
            return false;
        }
        if ( ! present) {
            break;
        }

        // Get actual file size to know where to continue writing
        std::error_code size_ec;
        auto file_size = std::filesystem::file_size(path, size_ec);
        if (size_ec) {
            spdlog::warn("block_store::initialize: Failed to get size of {}: {}",
                        path.string(), size_ec.message());
            file_size = 0;
        }

        // Ensure file_info_ has space for this file
        if (file_info_.size() <= static_cast<size_t>(file_num)) {
            file_info_.resize(file_num + 1);
        }
        // The size is NOT recorded here. It would be stale by the time anything
        // asked, and it must never become a write cursor: a file is preallocated
        // in whole chunks, so it is almost always larger than what was written
        // into it, and appending at it is exactly the defect this replaces
        // (#668). What initialize() establishes is only WHICH files exist; each
        // walk measures the one it is reading.
        (void)file_size;

        ++file_num;
    }

    // The numbering stopped at `file_num`. That is the end only if nothing is
    // numbered above it: a blk file missing BETWEEN two that exist is a file that
    // was lost, and stopping there would report a clean read of a chain with a
    // piece cut out — every block in the files beyond would simply be invisible,
    // with a cursor handed out over what was left.
    {
        // Driven with the non-throwing increment, and refusing when the listing
        // cannot be read. The previous shape had two holes: `list_ec` was set
        // only by the constructor, so a failure left `highest` at -1 and the gap
        // check below was skipped while initialize() returned success; and the
        // range-for's `operator++` throws, so an error during iteration escaped
        // this function instead of returning false.
        std::error_code list_ec;
        std::filesystem::directory_iterator it(blocks_dir_, list_ec);
        if (list_ec) {
            spdlog::critical("block_store::initialize: the blocks directory {} could not be "
                "listed ({}); refusing to open, because a lost file between the ones that do "
                "exist would go unnoticed", blocks_dir_.string(), list_ec.message());
            return false;
        }

        int32_t highest = -1;
        std::filesystem::directory_iterator const done;
        for (; it != done; it.increment(list_ec)) {
            if (list_ec) {
                spdlog::critical("block_store::initialize: listing {} failed part way ({}); "
                    "refusing to open on a directory that could not be read in full",
                    blocks_dir_.string(), list_ec.message());
                return false;
            }
            auto const& entry = *it;
            auto const name = entry.path().filename().string();
            if (name.size() != 12 || name.compare(0, 3, "blk") != 0
                || name.compare(8, 4, ".dat") != 0) {
                continue;
            }
            auto const digits = name.substr(3, 5);
            if (digits.find_first_not_of("0123456789") != std::string::npos) {
                continue;
            }
            highest = std::max(highest, static_cast<int32_t>(std::stol(digits)));
        }

        if (highest >= file_num) {
            spdlog::critical("block_store::initialize: blk{:05d}.dat is missing while "
                "blk{:05d}.dat exists. A gap in the numbering is a lost file, not the end of "
                "the chain; refusing to open rather than reporting a clean read of what is left",
                file_num, highest);
            return false;
        }
    }

    if (file_num > 0) {
        last_block_file_ = file_num - 1;
        spdlog::info("block_store: discovered {} block files; write cursors stay unavailable "
                    "until both scans complete", file_num);
    } else {
        spdlog::info("block_store: initialized with no existing files");
    }

    return true;
}

// =============================================================================
// Write Operations
// =============================================================================

flat_file_pos block_store::save_block(domain::chain::block const& block, uint32_t height) {
    return save_block_raw(kth::to_data_chunk(block), height, block.header().timestamp());
}

flat_file_pos block_store::save_block_raw(data_chunk const& raw_block, uint32_t height, uint64_t timestamp) {

    auto const block_size = static_cast<uint32_t>(raw_block.size());
    auto pos = find_block_pos(block_size + block_header_size, height, timestamp);

    if (pos.is_null()) {
        spdlog::error("block_store::save_block_raw: Failed to find position");
        return {};
    }

    if (!write_block_to_disk(raw_block, pos)) {
        return {};
    }

    return pos;
}

flat_file_pos block_store::allocate_block_space(uint32_t raw_block_size, uint32_t height, uint64_t timestamp) {
    return find_block_pos(raw_block_size + block_header_size, height, timestamp);
}

flat_file_pos block_store::write_block_at(data_chunk const& raw_block, flat_file_pos header_pos) {
    if (!write_block_to_disk(raw_block, header_pos)) {
        return {};
    }
    // header_pos.pos was modified by write_block_to_disk to point to data start
    return header_pos;
}

flat_file_pos block_store::write_undo(block_undo const& undo, int32_t file_num,
                                     hash_digest const& block_hash, hash_digest const& prev_hash) {

    auto const undo_size = static_cast<uint32_t>(undo.serialized_size());
    auto pos = find_undo_pos(file_num, undo_size + undo_header_size + undo_checksum_size);

    if (pos.is_null()) {
        spdlog::error("block_store::write_undo: Failed to find position");
        return {};
    }

    if (!write_undo_to_disk(undo, pos, block_hash, prev_hash)) {
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

    auto block = kth::from_data_chunk<domain::chain::block>(*raw_result);
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
    if (std::fseek(file, -static_cast<long>(block_header_size), SEEK_CUR) != 0) {
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

std::expected<data_chunk, result_code>
block_store::read_tx_raw(flat_file_pos const& pos) const {
    if (pos.is_null()) {
        return std::unexpected(result_code::key_not_found);
    }

    // Open file at the absolute offset (pos.pos is the tx start within the file)
    FILE* file = block_files_.open(pos, true);
    if (!file) {
        spdlog::error("block_store::read_tx_raw: Failed to open file for {}", pos.to_string());
        return std::unexpected(result_code::other);
    }

    // Record start position to compute total tx size later
    long const tx_start = std::ftell(file);
    if (tx_start < 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Helper: read a compact-size (varint) integer
    auto read_varint = [&file]() -> std::expected<uint64_t, result_code> {
        uint8_t first;
        if (std::fread(&first, 1, 1, file) != 1)
            return std::unexpected(result_code::other);
        if (first < 0xFD) return uint64_t(first);
        if (first == 0xFD) {
            uint16_t v;
            if (std::fread(&v, 2, 1, file) != 1) return std::unexpected(result_code::other);
            return uint64_t(v);
        }
        if (first == 0xFE) {
            uint32_t v;
            if (std::fread(&v, 4, 1, file) != 1) return std::unexpected(result_code::other);
            return uint64_t(v);
        }
        uint64_t v;
        if (std::fread(&v, 8, 1, file) != 1) return std::unexpected(result_code::other);
        return v;
    };

    // Helper: skip N bytes
    auto skip = [&file](long n) -> bool {
        return std::fseek(file, n, SEEK_CUR) == 0;
    };

    // Parse tx structure to determine its size:
    // version (4) + vin_count + [inputs] + vout_count + [outputs] + locktime (4)

    // 1. version (4 bytes)
    if (!skip(4)) { std::fclose(file); return std::unexpected(result_code::other); }

    // 2. vin_count
    auto vin_count = read_varint();
    if (!vin_count) { std::fclose(file); return std::unexpected(vin_count.error()); }

    // 3. inputs: each = prev_hash(32) + prev_index(4) + script_len(varint) + script + sequence(4)
    for (uint64_t i = 0; i < *vin_count; ++i) {
        if (!skip(36)) { std::fclose(file); return std::unexpected(result_code::other); }  // prev_hash + prev_index
        auto script_len = read_varint();
        if (!script_len) { std::fclose(file); return std::unexpected(script_len.error()); }
        if (!skip(static_cast<long>(*script_len) + 4)) { std::fclose(file); return std::unexpected(result_code::other); }  // script + sequence
    }

    // 4. vout_count
    auto vout_count = read_varint();
    if (!vout_count) { std::fclose(file); return std::unexpected(vout_count.error()); }

    // 5. outputs: each = value(8) + script_len(varint) + script
    for (uint64_t i = 0; i < *vout_count; ++i) {
        if (!skip(8)) { std::fclose(file); return std::unexpected(result_code::other); }  // value
        auto script_len = read_varint();
        if (!script_len) { std::fclose(file); return std::unexpected(script_len.error()); }
        if (!skip(static_cast<long>(*script_len))) { std::fclose(file); return std::unexpected(result_code::other); }  // script
    }

    // 6. locktime (4 bytes)
    if (!skip(4)) { std::fclose(file); return std::unexpected(result_code::other); }

    // Now we know the total tx size
    long const tx_end = std::ftell(file);
    if (tx_end < 0) { std::fclose(file); return std::unexpected(result_code::other); }

    auto const tx_size = static_cast<size_t>(tx_end - tx_start);

    // Seek back and read the whole tx
    if (std::fseek(file, tx_start, SEEK_SET) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    data_chunk data(tx_size);
    if (std::fread(data.data(), 1, tx_size, file) != tx_size) {
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
    if (std::fseek(file, -static_cast<long>(block_header_size), SEEK_CUR) != 0) {
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

    // Create indexed positions to track original order
    struct indexed_pos {
        size_t original_idx;
        flat_file_pos pos;
    };
    std::vector<indexed_pos> indexed;
    indexed.reserve(positions.size());
    for (size_t i = 0; i < positions.size(); ++i) {
        indexed.push_back({i, positions[i]});
    }

    // Sort by file number, then by position within file (for sequential I/O)
    std::sort(indexed.begin(), indexed.end(), [](auto const& a, auto const& b) {
        if (a.pos.file != b.pos.file) return a.pos.file < b.pos.file;
        return a.pos.pos < b.pos.pos;
    });

    // Results in original order
    std::vector<data_chunk> results(positions.size());

    // Read blocks grouped by file
    int32_t current_file = -1;
    FILE* file = nullptr;

    for (auto const& item : indexed) {
        // Open new file if needed
        if (item.pos.file != current_file) {
            if (file) {
                std::fclose(file);
            }
            file = block_files_.open(item.pos, true);
            if (!file) {
                spdlog::error("block_store::read_blocks_raw: Failed to open file {}", item.pos.file);
                return std::unexpected(result_code::other);
            }
            current_file = item.pos.file;
        } else {
            // Seek to position within same file
            if (std::fseek(file, static_cast<long>(item.pos.pos), SEEK_SET) != 0) {
                std::fclose(file);
                return std::unexpected(result_code::other);
            }
        }

        // Seek back to read the header (pos points to block data, after header)
        if (std::fseek(file, -static_cast<long>(block_header_size), SEEK_CUR) != 0) {
            std::fclose(file);
            return std::unexpected(result_code::other);
        }

        // Read header: magic + size
        std::array<uint8_t, 4> file_magic;
        uint32_t block_size;

        if (std::fread(file_magic.data(), 1, 4, file) != 4 ||
            std::fread(&block_size, sizeof(block_size), 1, file) != 1) {
            std::fclose(file);
            return std::unexpected(result_code::other);
        }

        // Verify magic
        if (file_magic != magic_) {
            spdlog::error("block_store::read_blocks_raw: Magic mismatch at {}", item.pos.to_string());
            std::fclose(file);
            return std::unexpected(result_code::other);
        }

        // Read block data
        data_chunk data(block_size);
        if (std::fread(data.data(), 1, block_size, file) != block_size) {
            std::fclose(file);
            return std::unexpected(result_code::other);
        }

        // Store in original order
        results[item.original_idx] = std::move(data);
    }

    if (file) {
        std::fclose(file);
    }

    return results;
}

block_store::undo_scan_result
block_store::scan_undo_positions(undo_parent_lookup const& parent_of) {
    // Revoked first, for the same reason as the block walk.
    undo_extents_adopted_ = false;

    std::vector<uint32_t> extents(file_info_.size(), 0);
    undo_scan_result result;
    result.status = undo_scan_status::clean_eof;

    // Collected, not published. A caller applies these only once every file has
    // been read without incident; a record rejected in the last file must not
    // leave the ones before it already in the index.
    boost::unordered_flat_set<hash_digest> seen;

    auto fail = [&result](undo_scan_status status, int32_t file_num, uint32_t offset,
                          hash_digest const& hash) {
        result.status = status;
        result.file_number = file_num;
        result.position = offset;
        result.block_hash = hash;
        result.found.clear();
        return result;
    };

    for (int32_t file_num = 0; file_num <= last_block_file_; ++file_num) {
        auto path = undo_files_.file_name(flat_file_pos{file_num, 0});

        FILE* file = open_native(path, "rb");
        if (file == nullptr) {
            // A file that is not there is a file that was never written. One that
            // is there and will not open is a failure, and skipping it would
            // start the node without undo it actually has.
            std::error_code ec;
            auto const present = std::filesystem::exists(path, ec);
            // Not being able to ask whether it exists is its own failure, and
            // taking it for "not there" would be the same mistake once more.
            if (ec) return fail(undo_scan_status::io_error, file_num, 0, {});
            if ( ! present) continue;
            return fail(undo_scan_status::io_error, file_num, 0, {});
        }

        // Physical, for the same reason the block walk uses it: the cursor is
        // what this walk produces, so it cannot also bound it.
        auto const measured = physical_size_of(path);
        if ( ! measured) {
            std::fclose(file);
            return fail(undo_scan_status::io_error, file_num, 0, hash_digest{});
        }
        auto const file_size = *measured;
        uint32_t offset = 0;

        while (true) {
            // No room for another header. That is the end only if what remains
            // is the zeroes of unused reserved space; between one and thirty-nine
            // non-zero bytes are an interrupted write, not an ending.
            if (offset + undo_header_size > file_size) {
                if ( ! tail_is_all_zero(file, offset, file_size)) {
                    std::fclose(file);
                    return fail(undo_scan_status::truncated_record, file_num, offset, {});
                }
                break;
            }

            if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, {});
            }

            std::array<uint8_t, 4> marker;
            if (std::fread(marker.data(), 1, marker.size(), file) != marker.size()) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, {});
            }

            if (marker != undo_magic_v2) {
                // All zeroes is space the file reserved and never used: rev files
                // are preallocated, and a restart takes their size from the file
                // system rather than from how far writing got. That is the normal
                // end of the records, not damage.
                if (marker == std::array<uint8_t, 4>{}) {
                    // Four zero bytes are the start of unused space — but only if
                    // everything after them is unused too. Four zeroes in the
                    // middle of a file would otherwise end the scan and hide every
                    // record beyond, reporting a clean read of a damaged file.
                    if ( ! tail_is_all_zero(file, offset, file_size)) {
                        std::fclose(file);
                        return fail(undo_scan_status::truncated_record, file_num, offset, {});
                    }
                    break;
                }

                std::fclose(file);
                // Three different things, and they read as three. The network
                // magic is a record from before undo records carried their
                // block's hash, which is what lets a caller ask for a rebuild
                // rather than guess. Anything else is a marker that should not be
                // here at all — not a record cut short, which is what
                // truncated_record means and is a different diagnosis.
                auto const status = (marker == magic_)
                    ? undo_scan_status::legacy_format
                    : undo_scan_status::invalid_marker;
                return fail(status, file_num, offset, {});
            }

            hash_digest block_hash;
            if (std::fread(block_hash.data(), 1, block_hash.size(), file) != block_hash.size()) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, {});
            }

            uint32_t undo_size = 0;
            if (std::fread(&undo_size, sizeof(undo_size), 1, file) != 1) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, block_hash);
            }

            if (undo_size == 0 || undo_size > max_undo_size) {
                std::fclose(file);
                return fail(undo_scan_status::invalid_size, file_num, offset, block_hash);
            }

            auto const record_size = undo_header_size + undo_size + undo_checksum_size;
            if (offset + record_size > file_size) {
                std::fclose(file);
                return fail(undo_scan_status::truncated_record, file_num, offset, block_hash);
            }

            // Uniqueness first, because it does not depend on knowing the
            // block: two records claiming one block are wrong whether or not the
            // index still holds it, and checking after the owner is resolved
            // would let a duplicate pair from a forgotten branch pass as two
            // ordinary unattributed records.
            if ( ! seen.insert(block_hash).second) {
                std::fclose(file);
                return fail(undo_scan_status::duplicate_record, file_num, offset, block_hash);
            }

            // The block this claims to belong to gives the parent hash the
            // checksum was seeded with. Not having it is not a failure: a
            // restart rebuilds the index from the active chain, so a branch that
            // lost a reorganization is gone while its undo records remain. Skip
            // the record — its size is known, so the walk continues — and count
            // it, rather than refuse a database that is merely older than its
            // last reorganization.
            auto const prev_hash = parent_of(block_hash);
            if ( ! prev_hash) {
                ++result.unattributed;
                offset += static_cast<uint32_t>(record_size);
                continue;
            }

            data_chunk undo_data(undo_size);
            if (std::fread(undo_data.data(), 1, undo_size, file) != undo_size) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, block_hash);
            }

            hash_digest stored_checksum;
            if (std::fread(stored_checksum.data(), 1, stored_checksum.size(), file)
                    != stored_checksum.size()) {
                std::fclose(file);
                return fail(undo_scan_status::io_error, file_num, offset, block_hash);
            }

            if (undo_checksum(*prev_hash, undo_data) != stored_checksum) {
                std::fclose(file);
                return fail(undo_scan_status::invalid_checksum, file_num, offset, block_hash);
            }

            // The position handed back is what read_undo expects: just past the
            // header, since it seeks backwards from there.
            result.found.push_back({file_num, offset + static_cast<uint32_t>(undo_header_size),
                block_hash});

            offset += static_cast<uint32_t>(record_size);
        }

        std::fclose(file);

        // Measured, not published. Every `return fail(...)` above leaves this
        // vector on the floor, which is the point: a family is adopted whole or
        // not at all.
        if (static_cast<size_t>(file_num) >= extents.size()) {
            return fail(undo_scan_status::io_error, file_num, offset, hash_digest{});
        }
        extents[file_num] = offset;
    }

    // Every file read cleanly. Publish the extents, all at once, and only now.
    for (size_t i = 0; i < extents.size() && i < file_info_.size(); ++i) {
        file_info_[i].undo_size = extents[i];
    }
    undo_extents_adopted_ = true;

    return result;
}

block_store::block_scan_result
block_store::scan_block_positions(block_position_callback const& callback) {
    constexpr size_t bitcoin_header_bytes = 80;

    auto fail = [](block_scan_status status, int32_t file_num, uint32_t position) {
        return block_scan_result{status, 0, file_num, position};
    };

    // Measured here, published only if every file reads cleanly. A partial walk
    // must leave nothing behind: half a set of cursors is worse than none,
    // because the files it reached would accept appends and the rest would not.
    // Revoked first. A walk that is about to run is a walk whose answer is not
    // known yet, and granting on success while relying on initialize() to clear
    // left a failing RESCAN with the previous pair still authorised — the store
    // would have gone on appending at cursors measured before whatever just
    // stopped being readable.
    block_extents_adopted_ = false;

    std::vector<uint32_t> extents(file_info_.size(), 0);
    size_t found = 0;

    // The last file's answer is the walk's answer. `clean_padding` and
    // `clean_eof` are both clean and they are not the same fact: one says the
    // file ends exactly at its last record, the other that reserved space
    // follows. Reporting only the first would make the second unreachable.
    auto last_status = block_scan_status::clean_eof;

    for (int32_t file_num = 0; file_num <= last_block_file_; ++file_num) {
        auto path = block_files_.file_name(flat_file_pos{file_num, 0});

        // A file that is not there was never written: its extent is zero and
        // there is nothing to read. A file that IS there and will not open is a
        // different thing entirely — its extent is unknown, and an unknown extent
        // must never become a place to write, so skipping it (as this used to)
        // is not available any more.
        std::error_code exists_ec;
        auto const present = std::filesystem::exists(path, exists_ec);
        if (exists_ec) {
            return fail(block_scan_status::open_failed, file_num, 0);
        }
        if ( ! present) {
            continue;
        }

        FILE* file = open_native(path, "rb");
        if (file == nullptr) {
            spdlog::error("block_store: blk file {} exists and could not be opened for scanning",
                file_num);
            return fail(block_scan_status::open_failed, file_num, 0);
        }

        // The PHYSICAL size bounds the walk. Bounding it by the write cursor
        // would be circular — the cursor is what this walk exists to produce.
        auto const measured = physical_size_of(path);
        if ( ! measured) {
            std::fclose(file);
            return fail(block_scan_status::open_failed, file_num, 0);
        }
        auto const file_size = *measured;
        uint32_t offset = 0;
        auto status = block_scan_status::clean_eof;

        while (true) {
            if (uint64_t(offset) + block_header_size > file_size) {
                // No room for another header. That is an ending only if what is
                // left is the zeroes of reserved space; anything else is a write
                // that was cut short.
                auto const tail = check_tail(file, offset, static_cast<uint32_t>(file_size));
                std::fclose(file);
                if (tail == tail_check::unreadable) {
                    return fail(block_scan_status::short_read, file_num, offset);
                }
                if (tail == tail_check::has_data) {
                    return fail(block_scan_status::truncated_header, file_num, offset);
                }
                status = (offset == file_size)
                    ? block_scan_status::clean_eof
                    : block_scan_status::clean_padding;
                file = nullptr;
                break;   // status travels out with the loop; see `last_status`
            }

            if (std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) {
                std::fclose(file);
                return fail(block_scan_status::seek_failed, file_num, offset);
            }

            std::array<uint8_t, 4> file_magic{};
            if (std::fread(file_magic.data(), 1, file_magic.size(), file) != file_magic.size()) {
                std::fclose(file);
                return fail(block_scan_status::short_read, file_num, offset);
            }

            if (file_magic != magic_) {
                // Four zeroes are the start of reserved space — but only if
                // everything after them is reserved too. Four zeroes with data
                // beyond would otherwise end the walk here and hide every record
                // past them, and the next append would land on top of those.
                if (file_magic == std::array<uint8_t, 4>{}) {
                    auto const tail = check_tail(file, offset, static_cast<uint32_t>(file_size));
                    std::fclose(file);
                    if (tail == tail_check::unreadable) {
                        return fail(block_scan_status::short_read, file_num, offset);
                    }
                    if (tail == tail_check::has_data) {
                        spdlog::critical("block_store: blk file {} holds data beyond a run of "
                            "zeroes starting at offset {}. This is not the end of the file's "
                            "records: something follows that the walk cannot account for, so the "
                            "database has to be rebuilt or reindexed rather than appended to",
                            file_num, offset);
                        return fail(block_scan_status::bad_magic, file_num, offset);
                    }
                    status = block_scan_status::clean_padding;
                    file = nullptr;
                    break;
                }

                std::fclose(file);
                return fail(block_scan_status::bad_magic, file_num, offset);
            }

            uint32_t block_size = 0;
            if (std::fread(&block_size, sizeof(block_size), 1, file) != 1) {
                std::fclose(file);
                return fail(block_scan_status::short_read, file_num, offset);
            }

            // Both ends of the size, in 64-bit arithmetic so neither the bound
            // nor the sum below can wrap.
            if (block_size < bitcoin_header_bytes || uint64_t(block_size) > max_block_record) {
                std::fclose(file);
                return fail(block_scan_status::invalid_size, file_num, offset);
            }

            auto const record_end = uint64_t(offset) + block_header_size + block_size;
            if (record_end > file_size) {
                std::fclose(file);
                return fail(block_scan_status::record_beyond_file, file_num, offset);
            }

            auto const data_pos = static_cast<uint32_t>(offset + block_header_size);

            std::array<uint8_t, bitcoin_header_bytes> hdr{};
            if (std::fread(hdr.data(), 1, hdr.size(), file) != hdr.size()) {
                std::fclose(file);
                return fail(block_scan_status::short_read, file_num, offset);
            }

            callback(file_num, data_pos, bitcoin_hash(byte_span{hdr.data(), hdr.size()}));
            ++found;

            offset = static_cast<uint32_t>(record_end);
        }

        if (file != nullptr) {
            std::fclose(file);
        }

        // Out of range means this walk measured a file the store does not have a
        // slot for, which is a disagreement about how many files exist. Skipping
        // the assignment would publish a zero extent for it — an invitation to
        // append at the start of a file that already holds records.
        if (static_cast<size_t>(file_num) >= extents.size()) {
            return fail(block_scan_status::open_failed, file_num, offset);
        }
        extents[file_num] = offset;
        last_status = status;
    }

    // Clean on every file: publish, all at once. Nothing above touched a cursor.
    for (size_t i = 0; i < extents.size() && i < file_info_.size(); ++i) {
        file_info_[i].size = extents[i];
    }
    block_extents_adopted_ = true;

    return block_scan_result{last_status, found, -1, 0};
}

bool block_store::append_enabled() const {
    return block_extents_adopted_ && undo_extents_adopted_;
}

std::expected<block_undo, result_code>
block_store::read_undo(flat_file_pos const& pos, hash_digest const& block_hash,
                       hash_digest const& prev_hash) const {
    if (pos.is_null()) {
        return std::unexpected(result_code::key_not_found);
    }

    // Open undo file
    FILE* file = undo_files_.open(pos, true);
    if (!file) {
        spdlog::error("block_store::read_undo: Failed to open file for {}", pos.to_string());
        return std::unexpected(result_code::other);
    }

    // Read header: magic + owning block hash + size
    std::array<uint8_t, 4> file_magic;
    hash_digest record_block_hash;
    uint32_t undo_size;

    // Seek back to header position
    if (std::fseek(file, -static_cast<long>(undo_header_size), SEEK_CUR) != 0) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    if (std::fread(file_magic.data(), 1, 4, file) != 4) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Before anything after it is read, let alone compared. In the old layout
    // the next thirty-two bytes are a size and the start of a payload, not a
    // hash — comparing them would report a record belonging to another block,
    // which is both the wrong diagnosis and the wrong error.
    if (file_magic != undo_magic_v2) {
        if (file_magic == magic_) {
            spdlog::error("block_store::read_undo: undo record at {} predates the format that "
                "identifies its block; this database cannot serve undo data", pos.to_string());
        } else {
            spdlog::error("block_store::read_undo: marker mismatch at {}", pos.to_string());
        }
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    if (std::fread(record_block_hash.data(), 1, record_block_hash.size(), file)
            != record_block_hash.size()) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // The record names its owner, so a position that points at some other
    // block's record is caught here rather than by the checksum — which cannot
    // catch it for a sibling, whose checksum this record also satisfies.
    if (record_block_hash != block_hash) {
        // Corruption, not absence: something pointed at a record belonging to
        // another block. Reporting it as "no undo here" would dress a damaged
        // database as an ordinary missing one, which is the confusion this record
        // format exists to end.
        spdlog::error("block_store::read_undo: record at {} belongs to another block",
            pos.to_string());
        std::fclose(file);
        return std::unexpected(result_code::db_corrupt);
    }

    if (std::fread(&undo_size, sizeof(undo_size), 1, file) != 1) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }

    // Bound it before it is used as a length. A corrupted size would otherwise
    // be handed straight to an allocation — several gigabytes of it — and the
    // addition below would overflow first. The scanner checks this on the way
    // in; a record can be damaged after that, so the read checks it too.
    if (undo_size == 0 || undo_size > max_undo_size) {
        spdlog::error("block_store::read_undo: implausible payload size {} at {}",
            undo_size, pos.to_string());
        std::fclose(file);
        return std::unexpected(result_code::db_corrupt);
    }

    // Read undo data + checksum
    data_chunk data(static_cast<size_t>(undo_size) + undo_checksum_size);
    if (std::fread(data.data(), 1, data.size(), file) != data.size()) {
        std::fclose(file);
        return std::unexpected(result_code::other);
    }
    std::fclose(file);

    // Extract undo data and checksum
    data_chunk undo_data(data.begin(), data.begin() + static_cast<ptrdiff_t>(undo_size));
    hash_digest stored_checksum;
    std::copy(data.begin() + static_cast<ptrdiff_t>(undo_size), data.end(),
        stored_checksum.begin());

    if (undo_checksum(prev_hash, undo_data) != stored_checksum) {
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

std::expected<void, block_store::undo_flush_error>
block_store::flush_undo(std::span<int32_t const> file_numbers) {
    // One number per block arrives here, so a thousand-block batch inside one
    // file asks for a thousand barriers on it. Normalized once, not at every
    // call site — and sorted, so the order the files are synced in does not
    // depend on the order the blocks happened to arrive.
    std::vector<int32_t> files(file_numbers.begin(), file_numbers.end());
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    for (auto const file : files) {
        if (file < 0 || size_t(file) >= file_info_.size()) {
            return std::unexpected(undo_flush_error{result_code::other, file});
        }

        flat_file_pos const pos{file, file_info_[size_t(file)].undo_size};
        if ( ! undo_files_.flush(pos, /*finalize*/ false)) {
            // Stop here. The caller cannot act on "some of it reached the disk"
            // any differently than on "none of it did", and continuing would
            // only replace the first failure's file number with the last one's.
            return std::unexpected(undo_flush_error{result_code::other, file});
        }
    }

    // The names, not the contents. A rev file whose every byte is on the platter
    // still does not exist if the directory entry that reaches it was never
    // written.
    auto const [barrier, ok] = sync_directory(blocks_dir_);
    if (barrier == directory_barrier::available && ! ok) {
        return std::unexpected(undo_flush_error{result_code::other, -1});
    }

    return {};
}

directory_barrier block_store::directory_durability() const {
#ifdef _WIN32
    return directory_barrier::unsupported;
#else
    return directory_barrier::available;
#endif
}

uint64_t block_store::calculate_disk_usage() const {

    uint64_t total = 0;
    for (auto const& info : file_info_) {
        total += info.size + info.undo_size;
    }
    return total;
}

size_t block_store::file_count() const {
    return file_info_.size();
}

block_file_info const& block_store::file_info(size_t index) const {
    return file_info_.at(index);
}

// =============================================================================
// Private Methods
// =============================================================================

flat_file_pos block_store::find_block_pos(uint32_t add_size, uint32_t height, uint64_t time) {
    // Refused, not guessed. Until both walks have finished cleanly this store
    // does not know where its files really end, and the one thing it must never
    // do is fall back to the file's size — that is the defect (#668). A value,
    // not an assertion: the caller sees it in every build.
    if ( ! append_enabled()) {
        spdlog::error("block_store::{}: the write cursor is unavailable. Both the block and "
            "the undo scans have to complete cleanly before this store can append; a database "
            "whose files could not be read end to end is not one to write to", "find_block_pos");
        return {};
    }

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
    // Refused, not guessed. Until both walks have finished cleanly this store
    // does not know where its files really end, and the one thing it must never
    // do is fall back to the file's size — that is the defect (#668). A value,
    // not an assertion: the caller sees it in every build.
    if ( ! append_enabled()) {
        spdlog::error("block_store::{}: the write cursor is unavailable. Both the block and "
            "the undo scans have to complete cleanly before this store can append; a database "
            "whose files could not be read end to end is not one to write to", "find_undo_pos");
        return {};
    }

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

    KTH_STATS_TIME_START(write_block);

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

    KTH_STATS_TIME_END(global_sync_stats(), write_block, write_block_time_ns, write_block_calls);
    KTH_STATS_ADD(global_sync_stats(), write_block_bytes, raw_block.size());

    std::fclose(file);
    return true;
}

bool block_store::write_undo_to_disk(block_undo const& undo, flat_file_pos& pos,
                                     hash_digest const& block_hash, hash_digest const& prev_hash) {
    FILE* file = undo_files_.open(pos, false);
    if (!file) {
        spdlog::error("block_store::write_undo_to_disk: Failed to open file");
        return false;
    }

    auto const undo_data = undo.to_data();
    auto const undo_size = static_cast<uint32_t>(undo_data.size());

    // Write header: undo marker + owning block hash + size
    if (std::fwrite(undo_magic_v2.data(), 1, undo_magic_v2.size(), file) != undo_magic_v2.size()) {
        std::fclose(file);
        return false;
    }

    if (std::fwrite(block_hash.data(), 1, block_hash.size(), file) != block_hash.size()) {
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
