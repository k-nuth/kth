// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_PATH_HPP_
#define KTH_INFRASTRUCTURE_PATH_HPP_

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace kth {

//Note(fernando): std::filesystem::u8path will be deprecated in C++20
// https://stackoverflow.com/questions/54004000/why-is-stdfilesystemu8path-deprecated-in-c20

using path = std::filesystem::path;

/// Sum regular-file lengths in one directory, including preallocated space,
/// of the files whose name `include` accepts. Symlinks are followed.
/// Nonrecursive and best-effort: inaccessible entries are skipped and an
/// iteration error returns the accumulated prefix. This is not a snapshot.
template <typename Include>
[[nodiscard]]
uint64_t files_size(std::filesystem::path const& dir, Include const& include) {
    uint64_t total = 0;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, ec);
    for (std::filesystem::directory_iterator const end; ! ec && it != end; it.increment(ec)) {
        if ( ! include(std::string_view{it->path().filename().string()})) {
            continue;
        }
        std::error_code entry_ec;
        if (it->is_regular_file(entry_ec)) {
            auto const size = it->file_size(entry_ec);
            if ( ! entry_ec) {
                total += size;
            }
        }
    }
    return total;
}

/// Every regular file in `dir`.
[[nodiscard]] inline
uint64_t files_size(std::filesystem::path const& dir) {
    return files_size(dir, [](std::string_view) { return true; });
}

} // namespace kth

#endif // KTH_INFRASTRUCTURE_PATH_HPP_
