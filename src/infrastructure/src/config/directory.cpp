// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/directory.hpp>

#include <string>

#include <kth/infrastructure/unicode/unicode.hpp>

#ifdef _MSC_VER
    #include <shlobj.h>
    #include <windows.h>
#endif

namespace kth::infrastructure::config {

// Returns empty string if unable to retrieve (including when not in Windows).
std::string windows_config_directory()
{
#ifdef _MSC_VER
    wchar_t directory[MAX_PATH];
    auto const result = SHGetFolderPathW(NULL, CSIDL_COMMON_APPDATA, NULL,
        SHGFP_TYPE_CURRENT, directory);

    if (SUCCEEDED(result))
        return to_utf8(directory);
#endif
    return "";
}

} // namespace kth::infrastructure::config
