// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_DATABASE_NATIVE_FILE_HPP
#define KTH_DATABASE_NATIVE_FILE_HPP

#include <cstdio>
#include <filesystem>

#ifdef _WIN32
#include <cstring>
#include <string>
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

} // namespace kth::database

#endif // KTH_DATABASE_NATIVE_FILE_HPP
