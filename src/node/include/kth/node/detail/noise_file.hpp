// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_NODE_DETAIL_NOISE_FILE_HPP
#define KTH_NODE_DETAIL_NOISE_FILE_HPP

#include <kth/node/sv2/feature.hpp>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <random>
#include <string>
#include <utility>

#if ! defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <kth/infrastructure/hash_define.hpp>
#include <kth/infrastructure/utility/data.hpp>
#include <kth/infrastructure/utility/pseudo_random.hpp>
#include <kth/node/detail/scope_action.hpp>

// The key and certificate files of the Noise transport. Every system call goes
// through an Operations object that belongs to the call, so a test can make
// one fail at any step without replacing a process-wide function while other
// tests run.

namespace kth::node::detail {

struct noise_file_status {
    bool regular = false;
    bool owned = false;
    bool private_file = false;
    uint64_t size = 0;
};

inline std::string noise_file_error(std::string const& what, std::filesystem::path const& path,
    int code) {
    return what + " " + path.string() + ": " + std::strerror(code);
}

// The `Size` bytes of an open file, which must be a regular file of exactly
// that size and, when `owner_only`, belong to this user with no access for
// group or others. Takes the descriptor and closes it.
template <size_t Size, typename Operations>
[[nodiscard]]
std::expected<byte_array<Size>, std::string> read_noise_file(Operations& operations,
    int fd, std::filesystem::path const& path, bool owner_only) {
    scope_action close([&] { operations.close(fd); }, true);
    auto const status = operations.inspect(fd);
    if ( ! status) return std::unexpected(noise_file_error("cannot inspect", path, status.error()));
    if ( ! status->regular) return std::unexpected(path.string() + " is not a regular file");
    if (owner_only && ! status->owned) {
        return std::unexpected(path.string() + " belongs to another user");
    }
    if (owner_only && ! status->private_file) {
        return std::unexpected(path.string() + " can be read by group or others; it must be 0600");
    }
    if (status->size != Size) {
        return std::unexpected(path.string() + " holds " + std::to_string(status->size)
            + " bytes, not " + std::to_string(Size));
    }
    byte_array<Size> bytes{};
    scope_action erase([&] { pseudo_random::wipe(bytes); }, true);
    size_t received = 0;
    while (received < bytes.size()) {
        auto const count = operations.read(fd, byte_span_mut{bytes}.subspan(received));
        if (count == -1 && operations.error() == EINTR) continue;
        if (count == -1) return std::unexpected(noise_file_error("cannot read", path, operations.error()));
        if (count == 0) return std::unexpected(path.string() + " ended early");
        received += size_t(count);
    }
    return bytes;
}

// Writes a new secret to `path` only while no file is there. The secret goes
// to a temporary file beside it, created owner-only, written in full and
// flushed to disk; then the temporary file is linked to `path`, which fails
// if a file appeared there meanwhile, and the directory is flushed. A rename
// would replace such a file. The temporary name is removed on every path: a
// failed write leaves nothing at `path`, so the next start creates the key
// again instead of refusing a short file. False when a file was already at
// `path`, which is then left as it is.
template <typename Operations>
[[nodiscard]]
std::expected<bool, std::string> create_noise_secret(Operations& operations,
    std::filesystem::path const& path, hash_digest const& secret) {
    auto const temporary = operations.create_temporary(path);
    if ( ! temporary) return std::unexpected(noise_file_error("cannot create a temporary file beside",
        path, temporary.error()));
    int const fd = temporary->first;
    std::filesystem::path const name = temporary->second;
    bool closed = false;
    scope_action remove([&] {
        if ( ! closed) operations.close(fd);
        operations.unlink(name);
    }, true);
    if (operations.secure(fd) != 0) {
        return std::unexpected(noise_file_error("cannot make owner-only", name, operations.error()));
    }
    size_t written = 0;
    while (written < secret.size()) {
        auto const count = operations.write(fd, byte_span{secret}.subspan(written));
        if (count == -1 && operations.error() == EINTR) continue;
        if (count <= 0) {
            return std::unexpected(noise_file_error("cannot write", name,
                count == 0 ? EIO : operations.error()));
        }
        written += size_t(count);
    }
    if (operations.sync(fd) != 0) {
        return std::unexpected(noise_file_error("cannot flush", name, operations.error()));
    }
    closed = true;
    if (operations.close(fd) != 0) {
        return std::unexpected(noise_file_error("cannot close", name, operations.error()));
    }
    if (operations.link(name, path) != 0) {
        auto const code = operations.error();
        if (code == EEXIST) return false;
        return std::unexpected(noise_file_error("cannot create", path, code));
    }
    if (operations.sync_directory(path) != 0) {
        return std::unexpected(noise_file_error("cannot flush the directory of", path, operations.error()));
    }
    return true;
}

// The 32-byte secret at `path`, created from `generate()` when no file is
// there. An existing file that cannot be used is refused and never replaced.
template <typename Operations, typename Generate>
[[nodiscard]]
std::expected<hash_digest, std::string> load_noise_secret(Operations& operations,
    std::filesystem::path const& path, Generate generate) {
    auto fd = operations.open_read(path);
    if (fd == -1 && operations.error() == ENOENT) {
        auto secret = generate();
        scope_action erase([&] { pseudo_random::wipe(secret); }, true);
        auto const created = create_noise_secret(operations, path, secret);
        if ( ! created) return std::unexpected(created.error());
        if (*created) return secret;
        // Another process created it first; that one is the key.
        fd = operations.open_read(path);
    }
    if (fd == -1) return std::unexpected(noise_file_error("cannot open", path, operations.error()));
    return read_noise_file<32>(operations, fd, path, true);
}

#if ! defined(_WIN32)

// The node's own file operations, on POSIX systems.
struct posix_noise_file_operations {
    // O_NOFOLLOW refuses a symbolic link as the file itself; the directories
    // on its path may be links.
    int open_read(std::filesystem::path const& path) const {
        return ::open(path.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    }
    // A fresh name beside the key, so that linking it is atomic. O_EXCL and
    // mode 0600 protect the secret from its first byte, without changing the
    // umask while other node tasks create files.
    std::expected<std::pair<int, std::filesystem::path>, int> create_temporary(
        std::filesystem::path const& path) const {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto temporary = path;
            temporary += ".tmp." + std::to_string(random());
            auto const fd = ::open(temporary.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            if (fd != -1) return std::pair{fd, temporary};
            if (errno != EEXIST) return std::unexpected(errno);
        }
        return std::unexpected(EEXIST);
    }
    int secure(int fd) const { return ::fchmod(fd, 0600); }
    std::expected<noise_file_status, int> inspect(int fd) const {
        struct stat status{};
        if (::fstat(fd, &status) != 0) return std::unexpected(errno);
        return noise_file_status{bool(S_ISREG(status.st_mode)), status.st_uid == ::geteuid(),
            (status.st_mode & 0077) == 0, uint64_t(status.st_size)};
    }
    std::ptrdiff_t read(int fd, byte_span_mut bytes) const {
        return ::read(fd, bytes.data(), bytes.size());
    }
    std::ptrdiff_t write(int fd, byte_span bytes) const {
        return ::write(fd, bytes.data(), bytes.size());
    }
    int sync(int fd) const { return ::fsync(fd); }
    int close(int fd) const { return ::close(fd); }
    int link(std::filesystem::path const& from, std::filesystem::path const& to) const {
        return ::link(from.c_str(), to.c_str());
    }
    int unlink(std::filesystem::path const& path) const { return ::unlink(path.c_str()); }
    // The new name is durable only once its directory is flushed too.
    int sync_directory(std::filesystem::path const& path) const {
        auto directory = path.parent_path();
        if (directory.empty()) directory = ".";
        auto const fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (fd == -1) return -1;
        auto const result = ::fsync(fd);
        auto const code = errno;
        ::close(fd);
        errno = code;
        return result;
    }
    int error() const { return errno; }
};

#endif

} // namespace kth::node::detail

#endif // KTH_NODE_DETAIL_NOISE_FILE_HPP
