// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_NATIVE_FILE_HPP
#define KTH_DATABASE_NATIVE_FILE_HPP

#include <cstdio>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#include <cstring>
#include <string>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace kth::database {

/// Open a path with the C stdio API, in the character type the platform stores
/// paths in.
///
/// A std::filesystem::path holds wchar_t on Windows and char elsewhere, while
/// std::fopen only ever takes char const*. Narrowing the path with .string()
/// would compile, but it converts through the active code page, so a path
/// holding any character that page cannot represent would stop opening at all —
/// a data directory under a user's name is enough. Each platform is therefore
/// handed the call that takes its own path verbatim. The mode is always an
/// ASCII literal, so widening it is a copy.
inline
FILE* open_native(std::filesystem::path const& path, char const* mode) {
#ifdef _WIN32
    std::wstring const wide_mode(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), wide_mode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

/// What a platform can promise about publishing a new file's name. Mirrors the
/// shape UTXO-Z exposes, because KTH's own guarantee is the weakest of the
/// barriers it depends on and the two have to be comparable to be combined.
enum class directory_barrier {
    available,      ///< The directory entry can be made durable.
    unsupported,    ///< This platform exposes no such barrier.
};

/// The outcome of asking for one. Three answers, not two: a barrier the
/// platform does not have and a barrier that was attempted and failed are
/// different facts about the machine, and only one of them is a defect.
struct directory_sync_result {
    directory_barrier barrier;
    bool ok;    ///< Meaningless when `barrier` is `unsupported`: nothing ran.
};

/// Make the directory entries in `dir` durable.
///
/// Making a file's CONTENTS durable does not make the name that reaches them
/// durable. A newly created rev*.dat can have every byte on the platter and
/// still not exist after a power cut, because the directory entry that names it
/// was never written. fsync on the directory is what publishes the name, and it
/// is a separate barrier from the one on the file.
///
/// Windows exposes no equivalent: a directory cannot be opened for the flush,
/// so the ordering between a file's creation and the data it publishes is weaker
/// there. That is reported as `unsupported` rather than as success — a caller
/// deciding what it may claim needs the difference, and answering "done" would
/// be a promise this cannot keep.
[[nodiscard]]
inline
directory_sync_result sync_directory(std::filesystem::path const& dir) {
#ifdef _WIN32
    (void)dir;
    return {directory_barrier::unsupported, true};
#else
    // EINTR is not a durability failure, and here it would be read as one. A
    // false from this call becomes an undo_flush_error, which is fatal: the
    // batch is refused and its transition record is left for the next start to
    // refuse on too. So a signal arriving at the wrong instant would cost a
    // rebuild over a barrier that had nothing wrong with it. fsync is permitted
    // to return EINTR and does so in practice on network filesystems, which is
    // where an IBD is most likely to be pointed at a slow mount.
    int fd = -1;
    do {
        fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    } while (fd < 0 && errno == EINTR);

    if (fd < 0) {
        return {directory_barrier::available, false};
    }

    int rc = 0;
    do {
        rc = ::fsync(fd);
    } while (rc != 0 && errno == EINTR);

    // Retried only on EINTR: every other error is the barrier genuinely failing,
    // and a loop over those would spin rather than report.
    bool const ok = rc == 0;
    ::close(fd);
    return {directory_barrier::available, ok};
#endif
}

} // namespace kth::database

#endif // KTH_DATABASE_NATIVE_FILE_HPP
