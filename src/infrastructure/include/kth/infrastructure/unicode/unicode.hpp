// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KTH_INFRASTRUCTURE_UNICODE_HPP
#define KTH_INFRASTRUCTURE_UNICODE_HPP

#include <cstddef>
#include <iostream>
#include <string>

#include <kth/infrastructure/define.hpp>
#include <kth/infrastructure/utility/data.hpp>

// Regarding Unicode design for Windows:
//
// Windows and other environments, such as Java, that supported Unicode prior
// to the advent of utf8 utilize 16 bit characters. These are typically encoded
// as wchar_t in C++. Unicode no longer fits in 16 bits and as such these
// implementations now require variable length character encoding just as utf8.
//
// We embrace the "utf8 everywhere" design: http://utf8everywhere.org
// The objective is to use utf8 as the canonical string encoding, pushing
// wchar_t translation to the edge (stdio, argv, O/S and external API calls).
// The macro KTH_USE_MAIN does most of the heavy lifting to ensure
// that stdio and argv is configured for utf8. The 'to_utf' functions are
// provided for API translations.

// Regarding Unicode source files in VC++ builds:
//
// Don't use #pragma execution_character_set("utf-8") in Windows.
// This instruction causes sources to be explicitly interpreted as UTF8.
// However this technique improperly encodes literals ("フ" for example).
// Instead use non-BOM UTF encoded files. To do this use "save as..."
// utf8 without a "signature" (BOM) in the Visual Studio save dialog.
// This is the typical file format for C++ sources in other environments.

// Regarding Unicode in console applications:
//
// KTH_USE_MAIN should be declared prior to kth::main() in a console
// application. This enables Unicode argument and environment processing in
// Windows. This macro implements main() and forwards to kth::main(), which
// should be implemented as if it was main() with the expectation that argv
// is utf8.
//
// Do not use std::cout|std::cerr|std::cin (aborts on assertion):
// std::cout << "text";
// std::cerr << "text";
// std::string text;
// std::cin >> text;
//
// Do not use the L qualifier when source is UTF-8 w/out BOM (mangles output):
// auto const utf16 = L"acción.кошка.日本国";
// std::wcout << utf16;

// Regarding use of boost:
//
// When working with boost and utf8 narrow characters on Windows the thread
// must be configured for utf8. When working with std::filesystem::path the
// static path object must be imbued with the utf8 locale or paths will be
// incorrectly translated.

#define KI_LOCALE_BACKEND "icu"
#define KI_LOCALE_UTF8 "en_US.UTF8"

#ifdef _MSC_VER
    #include <filesystem>
    #include <locale>
    #include <boost/locale.hpp>
    #include <windows.h>

    #define KTH_USE_MAIN \
        namespace kth { \
        int main(int argc, char* argv[]); \
        } \
        \
        int wmain(int argc, wchar_t* argv[]) \
        { \
            using namespace kth; \
            boost::locale::generator locale; \
            std::locale::global(locale(KI_LOCALE_UTF8)); \
            auto variables = to_utf8(_wenviron); \
            environ = reinterpret_cast<char**>(variables.data()); \
            auto arguments = to_utf8(argc, argv); \
            auto args = reinterpret_cast<char**>(arguments.data()); \
            return kth::main(argc, args); \
        }
#else
    #define KTH_USE_MAIN \
        namespace kth { \
        int main(int argc, char* argv[]); \
        } \
        \
        int main(int argc, char* argv[]) \
        { \
            return kth::main(argc, argv); \
        }
#endif

namespace kth {

/**
 * Use kth::cin in place of std::cin.
 */
extern std::istream& cin;

/**
 * Use kth::cout in place of std::cout.
 */
extern std::ostream& cout;

/**
 * Use kth::cerr in place of std::cerr.
 */
extern std::ostream& cerr;

#ifdef WITH_ICU

/**
 * Normalize a string value using nfc normalization.
 * This function requires the ICU dependency.
 * @param[in]  value  The value to normalize.
 * @return            The normalized value.
 */
KI_API std::string to_normal_nfc_form(std::string const& value);

/**
 * Normalize a string value using nfkd normalization.
 * This function requires the ICU dependency.
 * @param[in]  value  The value to normalize.
 * @return            The normalized value.
 */
KI_API std::string to_normal_nfkd_form(std::string const& value);

#endif

/**
 * Convert wide environment vector to utf8 environment vector.
 * Caller should assign buffer and set result to environ as:
 * environ = reinterpret_cast<char**>(&buffer[0])
 * @param[in]  environment  The wide environment variables vector.
 * @return                  A buffer holding the narrow version of environment.
 */
KI_API data_chunk to_utf8(wchar_t* environment[]);

/**
 * Convert wide argument vector to utf8 argument vector.
 * Caller should assign buffer and reinterpret result as:
 * auto args = reinterpret_cast<char**>(&buffer[0])
 * @param[in]  argc  The number of elements in argv.
 * @param[in]  argv  The wide command line arguments.
 * @return           A buffer holding the narrow version of argv.
 */
KI_API data_chunk to_utf8(int argc, wchar_t* argv[]);

/**
 * Convert a wide (presumed UTF16) array to wide (UTF8/char).
 * @param[in]  out        The converted narrow array.
 * @param[in]  out_bytes  The allocated number of bytes in 'out'.
 * @param[in]  in         The wide character array to convert.
 * @param[in]  in_chars   The number of 'in' wide characters to convert.
 * @return                The number of bytes converted.
 */
KI_API size_t to_utf8(char out[], size_t out_bytes, const wchar_t in[],
    size_t in_chars);

/**
 * Convert a wide (presumed UTF16) string to narrow (UTF8/char).
 * @param[in]  wide  The utf16 string to convert.
 * @return           The resulting utf8 string.
 */
KI_API std::string to_utf8(const std::wstring& wide);

/**
 * Convert a narrow (presumed UTF8) array to wide (UTF16/wchar_t).
 * This is designed for buffering, where the narrow array may have been
 * truncated in the middle of a multiple byte character. The terminating
 * offset is returned in the 'unread' out parameter.
 * @param[in]  out        The converted wide array.
 * @param[in]  out_chars  The allocated number of wide characters in 'out'.
 * @param[in]  in         The narrow array to convert.
 * @param[in]  in_bytes   The number of 'in' bytes to convert.
 * @param[in]  truncated  The number of 'in' bytes [0..3] that were truncated.
 * @return                The number of characters converted.
 */
KI_API size_t to_utf16(wchar_t out[], size_t out_chars, char const in[],
    size_t in_bytes, uint8_t& truncated);

/**
 * Convert a narrow (presumed UTF8) string to wide (UTF16/wchar_t).
 * @param[in]  narrow  The utf8 string to convert.
 * @return             The resulting utf16 string.
 */
KI_API std::wstring to_utf16(std::string const& narrow);

/**
 * Initialize windows to use UTF8 for stdio. This cannot be uninitialized and
 * once set bc stdio must be used in place of std stdio.
 */
KI_API void set_utf8_stdio();

/**
 * Initialize windows to use UTF8 for stdin. This cannot be uninitialized and
 * once set kth::cin must be used in place of std::cin.
 */
KI_API void set_utf8_stdin();

/**
 * Initialize windows to use UTF8 for stdout. This cannot be uninitialized and
 * once set kth::cout must be used in place of std::cout.
 */
KI_API void set_utf8_stdout();

/**
 * Initialize windows to use UTF8 for stderr. This cannot be uninitialized and
 * once set kth::cerr must be used in place of std::cerr.
 */
KI_API void set_utf8_stderr();

/**
 * Initialize windows to use binary for stdin. This cannot be uninitialized.
 */
KI_API void set_binary_stdin();

/**
 * Initialize windows to use binary for stdout. This cannot be uninitialized.
 */
KI_API void set_binary_stdout();

} // namespace kth

#endif
