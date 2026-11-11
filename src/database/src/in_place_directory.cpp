// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/database/in_place_directory.hpp>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/vfs.h>
#include <unistd.h>
#endif

namespace kth::database {

namespace {

std::error_code last_error() {
#if defined(__linux__)
    return std::error_code(errno, std::system_category());
#else
    return std::make_error_code(std::errc::not_supported);
#endif
}

} // namespace

std::expected<in_place_directory, std::error_code>
prepare_in_place_directory(std::filesystem::path const& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return std::unexpected(ec);

#if defined(__linux__)
    // BTRFS_SUPER_MAGIC, from <linux/magic.h>.
    constexpr decltype(statfs::f_type) btrfs_magic = 0x9123683E;
    struct statfs fs{};
    if (::statfs(directory.c_str(), &fs) != 0) return std::unexpected(last_error());
    if (fs.f_type != btrfs_magic) return in_place_directory::not_btrfs;

    auto const empty = std::filesystem::is_empty(directory, ec);
    if (ec) return std::unexpected(ec);

    int const fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return std::unexpected(last_error());
    int flags = 0;
    if (::ioctl(fd, FS_IOC_GETFLAGS, &flags) != 0) {
        auto const error = last_error();
        ::close(fd);
        return std::unexpected(error);
    }
    if ((flags & FS_NOCOW_FL) != 0) {
        ::close(fd);
        return in_place_directory::nocow_already;
    }
    if ( ! empty) {
        ::close(fd);
        return in_place_directory::not_empty;
    }
    flags |= FS_NOCOW_FL;
    if (::ioctl(fd, FS_IOC_SETFLAGS, &flags) != 0) {
        auto const error = last_error();
        ::close(fd);
        return std::unexpected(error);
    }
    ::close(fd);
    return in_place_directory::nocow_set;
#else
    return in_place_directory::unsupported;
#endif
}

char const* in_place_directory_name(in_place_directory value) noexcept {
    switch (value) {
        case in_place_directory::nocow_set: return "set NOCOW (btrfs)";
        case in_place_directory::nocow_already: return "already NOCOW (btrfs)";
        case in_place_directory::not_btrfs: return "not btrfs";
        case in_place_directory::not_empty: return "not empty, left as it is";
        case in_place_directory::unsupported: return "unsupported on this platform";
    }
    return "unknown";
}

} // namespace kth::database
