// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/unicode.hpp>

#include <cstddef>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <mutex>
#include <limits>
#include <stdexcept>
#include <string>

#include "utf.hpp"

#ifdef WITH_ICU
#include <unicode/unorm2.h>
#endif

#include <kth/infrastructure/define.hpp>
// #include <kth/infrastructure/math/limits.hpp>
#include <kth/infrastructure/unicode/console_streambuf.hpp>
#include <kth/infrastructure/unicode/unicode_istream.hpp>
#include <kth/infrastructure/unicode/unicode_ostream.hpp>
#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>


#ifdef _MSC_VER
    #include <fcntl.h>
    #include <io.h>
#endif

namespace kth {


// The width of utf16 stdio buffers.
constexpr size_t utf16_buffer_size = 256;

// Local definition for max number of bytes in a utf8 character.
constexpr size_t utf8_max_character_size = 4;

// Ensure console_streambuf::initialize is called only once.
static std::once_flag io_mutex;

// Static initializer for kth::cin.
static std::istream& cin_stream()
{
    std::call_once(io_mutex, console_streambuf::initialize, utf16_buffer_size);
    static unicode_istream input(std::cin, std::wcin, utf16_buffer_size);
    return input;
}

// Static initializer for kth::cout.
static std::ostream& cout_stream()
{
    std::call_once(io_mutex, console_streambuf::initialize, utf16_buffer_size);
    static unicode_ostream output(std::cout, std::wcout, utf16_buffer_size);
    return output;
}

// Static initializer for kth::cerr.
static std::ostream& cerr_stream()
{
    std::call_once(io_mutex, console_streambuf::initialize, utf16_buffer_size);
    static unicode_ostream error(std::cerr, std::wcerr, utf16_buffer_size);
    return error;
}

// Use kth::cin in place of std::cin, etc.
std::istream& cin = cin_stream();
std::ostream& cout = cout_stream();
std::ostream& cerr = cerr_stream();

#ifdef WITH_ICU

// ICU is only required for the optional wallet normalization features.
static std::expected<std::string, unicode_error> normal_form(std::string const& value, UNormalizer2 const* normalizer) {
    auto const input = detail::utf_from_utf8<char16_t>(value);
    if (!input) return std::unexpected(input.error());
    if (input->empty()) return std::string{};
    if (input->size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
        return std::unexpected(unicode_error::input_too_large);
    std::vector<UChar> const source(input->begin(), input->end());
    UErrorCode status = U_ZERO_ERROR;
    auto const length = static_cast<int32_t>(source.size());
    auto const needed = unorm2_normalize(normalizer, source.data(), length, nullptr, 0, &status);
    if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        return std::unexpected(unicode_error::normalization_failed);
    status = U_ZERO_ERROR;
    std::vector<UChar> output(static_cast<size_t>(needed));
    auto const written = unorm2_normalize(normalizer, source.data(), length, output.data(), needed, &status);
    if (U_FAILURE(status)) return std::unexpected(unicode_error::normalization_failed);
    return detail::utf_to_utf8(std::u16string(output.begin(), output.begin() + written));
}

std::expected<std::string, unicode_error> to_normal_nfc_form(std::string const& value) {
    UErrorCode status = U_ZERO_ERROR;
    auto const normalizer = unorm2_getNFCInstance(&status);
    if (U_FAILURE(status)) return std::unexpected(unicode_error::normalization_failed);
    return normal_form(value, normalizer);
}

std::expected<std::string, unicode_error> to_normal_nfkd_form(std::string const& value) {
    UErrorCode status = U_ZERO_ERROR;
    auto const normalizer = unorm2_getNFKDInstance(&status);
    if (U_FAILURE(status)) return std::unexpected(unicode_error::normalization_failed);
    return normal_form(value, normalizer);
}

#endif

namespace {
std::expected<size_t, unicode_error> checked_size_add(size_t left, size_t right) {
    if (left > std::numeric_limits<size_t>::max() - right)
        return std::unexpected(unicode_error::input_too_large);
    return left + right;
}
} // namespace

// Convert wmain environment to utf8 main environment.
std::expected<data_chunk, unicode_error> to_utf8(wchar_t* environment[])
{
    if (environment == nullptr) return std::unexpected(unicode_error::invalid_argument);
    int count;
    for (count = 0; environment[count] != nullptr; ++count) {
        if (count == std::numeric_limits<int>::max())
            return std::unexpected(unicode_error::input_too_large);
    }

    return to_utf8(count, environment);
}

// Convert wmain parameters to utf8 main parameters.
std::expected<data_chunk, unicode_error> to_utf8(int argc, wchar_t* argv[]) {
    if (argc < 0 || (argc != 0 && argv == nullptr)) return std::unexpected(unicode_error::invalid_argument);
    auto const arg_count = static_cast<size_t>(argc);

    // Convert each arg and determine the payload size.
    size_t payload_size = 0;
    std::vector<std::string> collection(arg_count + 1);

    for (size_t arg = 0; arg < arg_count; arg++) {
        if (argv[arg] == nullptr) return std::unexpected(unicode_error::invalid_argument);
        auto converted = to_utf8(argv[arg]);
        if (!converted) return std::unexpected(converted.error());
        collection[arg] = std::move(*converted);
        auto const size = checked_size_add(collection[arg].size(), size_t{1});
        if (!size) return std::unexpected(unicode_error::input_too_large);
        auto const total = checked_size_add(payload_size, *size);
        if (!total) return std::unexpected(unicode_error::input_too_large);
        payload_size = *total;
    }

    // Determine the index size.
    auto const arg_count_p1_ex = checked_size_add(arg_count, size_t{1});
    if ( ! arg_count_p1_ex) {
        return std::unexpected(unicode_error::input_too_large);
    }
    if (*arg_count_p1_ex > std::numeric_limits<size_t>::max() / sizeof(void*))
        return std::unexpected(unicode_error::input_too_large);
    auto const index_size = *arg_count_p1_ex * sizeof(void*);

    // Allocate the new buffer.
    auto const buffer_size_ex = checked_size_add(index_size, payload_size);
    if ( ! buffer_size_ex) {
        return std::unexpected(unicode_error::input_too_large);
    }
    auto const buffer_size = *buffer_size_ex;
    data_chunk buffer(buffer_size, 0x00);
    buffer.resize(buffer_size);

    // Set pointers into index and payload buffer sections.
    auto index = reinterpret_cast<char**>(&buffer[0]);
    auto arguments = reinterpret_cast<char*>(&buffer[index_size]);

    // Clone the converted collection into the new narrow argv.
    for (size_t arg = 0; arg < arg_count; arg++) {
        index[arg] = arguments;
        auto const size = collection[arg].size();
        std::copy_n(collection[arg].begin(), size, index[arg]);
        auto const size_p1 = checked_size_add(size, size_t{ 1 });
        if ( ! size_p1) {
            return std::unexpected(unicode_error::input_too_large);
        }
        arguments += *size_p1;
    }

    return buffer;
}

// Convert wstring to utf8 string.
std::expected<std::string, unicode_error> to_utf8(const std::wstring& wide)
{
    return detail::utf_to_utf8(wide);
}

// Convert wchar_t buffer to utf8 char buffer.
std::expected<size_t, unicode_error> to_utf8(char out[], size_t out_bytes, const wchar_t in[],
    size_t in_chars)
{
    if ((in == nullptr && in_chars != 0) || (out == nullptr && out_bytes != 0))
        return std::unexpected(unicode_error::invalid_argument);
    if (in_chars == 0) return size_t{0};
    auto const narrow = to_utf8(std::wstring(in, in_chars));
    if (!narrow) return std::unexpected(narrow.error());
    if (narrow->size() > out_bytes) return std::unexpected(unicode_error::buffer_too_small);
    memcpy(out, narrow->data(), narrow->size());
    return narrow->size();
}

// All non-leading bytes of utf8 have the same two bit prefix.
static bool is_utf8_trailing_byte(char byte)
{
    // 10xxxxxx
    return ((0xC0 & byte) == 0x80);
}

// Determine if the full sequence is a valid utf8 character.
static bool is_utf8_character_sequence(char const sequence[], uint8_t bytes)
{
    KTH_ASSERT(bytes <= utf8_max_character_size);

    // See tools.ietf.org/html/rfc3629#section-3 for definition.
    switch (bytes) {
        case 1:
            // 0xxxxxxx
            return
                ((0x80 & sequence[0]) == 0x00);
        case 2:
            // 110xxxxx 10xxxxxx
            return
                ((0xE0 & sequence[0]) == 0xC0) &&
                is_utf8_trailing_byte(sequence[1]);
        case 3:
            // 1110xxxx 10xxxxxx 10xxxxxx
            return
                ((0xF0 & sequence[0]) == 0xE0) &&
                is_utf8_trailing_byte(sequence[1]) &&
                is_utf8_trailing_byte(sequence[2]);
        case 4:
            // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
            return
                ((0xF8 & sequence[0]) == 0xF0) &&
                is_utf8_trailing_byte(sequence[1]) &&
                is_utf8_trailing_byte(sequence[2]) &&
                is_utf8_trailing_byte(sequence[3]);
        default:;
    }

    return false;
}

// Determine if the text is terminated by a valid utf8 character.
static bool is_terminal_utf8_character(char const text[], size_t size)
{
    KTH_ASSERT(text != nullptr);

    // Walk back up to the max length of a utf8 character.
    for (uint8_t length = 1; length <= utf8_max_character_size &&
        length < size; length++) {
        auto const start = size - length;
        auto const sequence = &text[start];
        if (is_utf8_character_sequence(sequence, length)) {
            return true;
}
    }

    return false;
}

// This optimizes character split detection by taking advantage of utf8
// character recognition so we don't have to convert in full up to 3 times.
// This does not guaratee that the entire string is valid as utf8, just that a
// returned offset follows the last byte of a utf8 terminal char if it exists.
static uint8_t offset_to_terminal_utf8_character(char const text[], size_t size)
{
    KTH_ASSERT(text != nullptr);

    // Walk back up to the max length of a utf8 character.
    for (uint8_t unread = 0; unread < utf8_max_character_size &&
        unread < size; unread++) {
        auto const length = size - unread;
        if (is_terminal_utf8_character(text, length)) {
            return unread;
}
    }

    return 0;
}

// Convert utf8 char buffer to wchar_t buffer, with truncation handling.
std::expected<size_t, unicode_error> to_utf16(wchar_t out[], size_t out_chars, char const in[],
    size_t in_bytes, uint8_t& truncated)
{
    truncated = 0;
    if ((in == nullptr && in_bytes != 0) || (out == nullptr && out_chars != 0))
        return std::unexpected(unicode_error::invalid_argument);
    if (in_bytes == 0) return size_t{0};
    truncated = offset_to_terminal_utf8_character(in, in_bytes);
    auto const wide = to_utf16(std::string(in, in_bytes - truncated));
    if (!wide) return std::unexpected(wide.error());
    if (wide->size() > out_chars) return std::unexpected(unicode_error::buffer_too_small);
    if (!wide->empty()) wmemcpy(out, wide->data(), wide->size());
    return wide->size();
}

// Convert utf8 string to wstring.
std::expected<std::wstring, unicode_error> to_utf16(std::string const& narrow)
{
    return detail::utf_from_utf8<wchar_t>(narrow);
}

#ifdef _MSC_VER
namespace {
std::expected<void, unicode_error> set_stdio_mode(FILE* file, int mode) {
    auto const descriptor = _fileno(file);
    if (descriptor < 0) return std::unexpected(unicode_error::stdio_failed);
    // _O_BINARY clears text translation but can retain the CRT Unicode mode.
    // Reset that mode to ANSI first, or flushing narrow bytes may abort later.
    if (mode == _O_BINARY && _setmode(descriptor, _O_TEXT) == -1)
        return std::unexpected(unicode_error::stdio_failed);
    if (_setmode(descriptor, mode) == -1)
        return std::unexpected(unicode_error::stdio_failed);
    return {};
}
} // namespace
#endif

std::expected<void, unicode_error> set_utf8_stdio() {
    auto const input = set_utf8_stdin();
    if (!input) return input;
    auto const output = set_utf8_stdout();
    if (!output) return output;
    return set_utf8_stderr();
}

std::expected<void, unicode_error> set_utf8_stdin() {
#ifdef _MSC_VER
    return set_stdio_mode(stdin, _O_U8TEXT);
#else
    return {};
#endif
}

std::expected<void, unicode_error> set_utf8_stdout() {
#ifdef _MSC_VER
    return set_stdio_mode(stdout, _O_U8TEXT);
#else
    return {};
#endif
}

std::expected<void, unicode_error> set_utf8_stderr() {
#ifdef _MSC_VER
    return set_stdio_mode(stderr, _O_U8TEXT);
#else
    return {};
#endif
}

std::expected<void, unicode_error> set_binary_stdin() {
#ifdef _MSC_VER
    return set_stdio_mode(stdin, _O_BINARY);
#else
    return {};
#endif
}

std::expected<void, unicode_error> set_binary_stdout() {
#ifdef _MSC_VER
    return set_stdio_mode(stdout, _O_BINARY);
#else
    return {};
#endif
}

} // namespace kth
