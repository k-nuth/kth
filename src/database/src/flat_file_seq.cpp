// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/flat_file_seq.hpp>

#include <cerrno>
#include <cstdio>
#include <stdexcept>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <kth/infrastructure/utility/stats.hpp>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace kth::database {

flat_file_seq::flat_file_seq(std::filesystem::path dir, std::string prefix, size_t chunk_size)
    : dir_(std::move(dir))
    , prefix_(std::move(prefix))
    , chunk_size_(chunk_size)
{
    if (chunk_size_ == 0) {
        throw std::invalid_argument("flat_file_seq: chunk_size must be positive");
    }
}

std::filesystem::path flat_file_seq::file_name(flat_file_pos const& pos) const {
    return dir_ / fmt::format("{}{:05}.dat", prefix_, pos.file);
}

FILE* flat_file_seq::open(flat_file_pos const& pos, bool read_only) const {
    KTH_STATS_TIME_START(file_open);

    if (pos.is_null()) {
        return nullptr;
    }

    auto const path = file_name(pos);

    // Try to open existing file
    FILE* file = std::fopen(path.c_str(), read_only ? "rb" : "rb+");

    if (!file) {
        // Create directory if needed
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
    }

    // For write mode, create if doesn't exist
    if (!file && !read_only) {
        file = std::fopen(path.c_str(), "wb+");
    }

    if (!file) {
        spdlog::error("flat_file_seq::open: Unable to open file {}", path.string());
        return nullptr;
    }

    // Seek to position if needed
    if (pos.pos != 0) {
        if (std::fseek(file, static_cast<long>(pos.pos), SEEK_SET) != 0) {
            spdlog::error("flat_file_seq::open: Unable to seek to position {} in {}",
                         pos.pos, path.string());
            std::fclose(file);
            return nullptr;
        }
    }

    KTH_STATS_TIME_END(kth::global_sync_stats(), file_open, file_open_time_ns, file_open_calls);
    return file;
}

size_t flat_file_seq::allocate(flat_file_pos const& pos, size_t add_size, bool& out_of_space) const {
    out_of_space = false;

    auto const n_old_chunks = (pos.pos + chunk_size_ - 1) / chunk_size_;
    auto const n_new_chunks = (pos.pos + add_size + chunk_size_ - 1) / chunk_size_;

    if (n_new_chunks > n_old_chunks) {
        auto const old_size = pos.pos;
        auto const new_size = n_new_chunks * chunk_size_;
        auto const inc_size = new_size - old_size;

        if (check_disk_space(dir_, inc_size)) {
            FILE* file = open(pos, false);
            if (file) {
                spdlog::info("Pre-allocating up to position {:#x} in {}{:05}.dat",
                            new_size, prefix_, pos.file);

                KTH_STATS_TIME_START(allocate);
                allocate_file_range(file, pos.pos, inc_size);
                KTH_STATS_TIME_END(kth::global_sync_stats(), allocate, allocate_time_ns, allocate_calls);
                KTH_STATS_ADD(kth::global_sync_stats(), allocate_bytes, inc_size);

                std::fclose(file);
                return inc_size;
            }
        } else {
            out_of_space = true;
        }
    }

    return 0;
}

bool flat_file_seq::flush(flat_file_pos const& pos, bool finalize) const {
    // Open at position 0 to avoid seek issues
    flat_file_pos start_pos{pos.file, 0};
    FILE* file = open(start_pos, false);
    if (!file) {
        spdlog::error("flat_file_seq::flush: failed to open file {}", pos.file);
        return false;
    }

    if (finalize && !truncate_file(file, pos.pos)) {
        std::fclose(file);
        spdlog::error("flat_file_seq::flush: failed to truncate file {}", pos.file);
        return false;
    }

    if (!file_commit(file)) {
        std::fclose(file);
        spdlog::error("flat_file_seq::flush: failed to commit file {}", pos.file);
        return false;
    }

    std::fclose(file);
    return true;
}

// =============================================================================
// Platform-specific utilities
// =============================================================================

bool check_disk_space(std::filesystem::path const& path, size_t additional_size) {
    std::error_code ec;
    auto const space_info = std::filesystem::space(path, ec);
    if (ec) {
        // If we can't check, assume it's OK (will fail later if not)
        return true;
    }

    // Require at least additional_size + 50MB buffer
    constexpr size_t buffer = 50 * 1024 * 1024;
    return space_info.available > additional_size + buffer;
}

bool allocate_file_range(FILE* file, size_t offset, size_t length) {
    if (!file || length == 0) {
        return true;
    }

#ifdef _WIN32
    // Windows: Seek to end and write zeros
    if (_fseeki64(file, static_cast<__int64>(offset + length - 1), SEEK_SET) != 0) {
        return false;
    }
    if (std::fputc(0, file) == EOF) {
        return false;
    }
    return std::fflush(file) == 0;

#elif defined(__linux__)
    // Linux: Use posix_fallocate for efficient pre-allocation
    int fd = fileno(file);
    if (fd < 0) {
        return false;
    }
    int ret = posix_fallocate(fd, static_cast<off_t>(offset), static_cast<off_t>(length));
    return ret == 0;

#elif defined(__APPLE__)
    // macOS: Use fcntl with F_PREALLOCATE
    int fd = fileno(file);
    if (fd < 0) {
        return false;
    }

    fstore_t store;
    store.fst_flags = F_ALLOCATECONTIG;  // Try contiguous first
    store.fst_posmode = F_PEOFPOSMODE;
    store.fst_offset = 0;
    store.fst_length = static_cast<off_t>(offset + length);
    store.fst_bytesalloc = 0;

    if (fcntl(fd, F_PREALLOCATE, &store) == -1) {
        // Fall back to non-contiguous
        store.fst_flags = F_ALLOCATEALL;
        if (fcntl(fd, F_PREALLOCATE, &store) == -1) {
            return false;
        }
    }

    // Set the file size
    return ftruncate(fd, static_cast<off_t>(offset + length)) == 0;

#else
    // Generic fallback: seek and write
    if (std::fseek(file, static_cast<long>(offset + length - 1), SEEK_SET) != 0) {
        return false;
    }
    if (std::fputc(0, file) == EOF) {
        return false;
    }
    return std::fflush(file) == 0;
#endif
}

bool truncate_file(FILE* file, size_t size) {
    if (!file) {
        return false;
    }

#ifdef _WIN32
    int fd = _fileno(file);
    if (fd < 0) {
        return false;
    }
    return _chsize_s(fd, static_cast<__int64>(size)) == 0;
#else
    int fd = fileno(file);
    if (fd < 0) {
        return false;
    }
    return ftruncate(fd, static_cast<off_t>(size)) == 0;
#endif
}

bool file_commit(FILE* file) {
    if (!file) {
        return false;
    }

    // First flush the C library buffers
    if (std::fflush(file) != 0) {
        return false;
    }

#ifdef _WIN32
    int fd = _fileno(file);
    if (fd < 0) {
        return false;
    }
    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    return FlushFileBuffers(handle) != 0;
#else
    int fd = fileno(file);
    if (fd < 0) {
        return false;
    }

#if defined(__APPLE__)
    // macOS: F_FULLFSYNC for full durability
    return fcntl(fd, F_FULLFSYNC, 0) == 0;
#else
    // Linux/others: fdatasync (faster than fsync, only syncs data, not metadata)
    return fdatasync(fd) == 0;
#endif

#endif
}

} // namespace kth::database
