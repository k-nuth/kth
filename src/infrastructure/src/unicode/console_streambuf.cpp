// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/console_streambuf.hpp>

#include <cstddef>
#include <iostream>
#include <new>
#include <streambuf>

// #include <kth/infrastructure/math/limits.hpp>
#include <kth/infrastructure/utility/limits.hpp>

#ifdef _MSC_VER
#include <windows.h>

// Get Windows input handle.
static
LPVOID get_input_handle() {
    auto handle = GetStdHandle(STD_INPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr)
        throw std::ios_base::failure("Failed to get input handle.");

    return handle;
}
#endif

namespace kth {

// This class/mathod is a no-op on non-windows platforms.
// When working in Windows console set font to "Lucida Console".
// This is the factory method to privately instantiate a singleton class.
void console_streambuf::initialize(size_t size) {
#ifdef _MSC_VER
    // Set the console to operate in UTF-8 for this process.
    if (SetConsoleCP(CP_UTF8) == FALSE) {
        throw std::ios_base::failure("Failed to set console to utf8.");
    }

    DWORD console_mode;
    if (GetConsoleMode(get_input_handle(), &console_mode) != FALSE) {
        // Hack for faulty std::wcin translation of non-ASCII keyboard input.
        static console_streambuf buffer(*std::wcin.rdbuf(), size);
        std::wcin.rdbuf(&buffer);
    }
#endif
}

console_streambuf::console_streambuf(std::wstreambuf const& stream_buffer, size_t size)
#ifdef _MSC_VER
    : buffer_size_(size), buffer_(new wchar_t[buffer_size_])
    , std::wstreambuf(stream_buffer)
#else
    : buffer_size_(0), buffer_(nullptr)
#endif
{
}

#ifdef _MSC_VER
console_streambuf::~console_streambuf() {
    delete[] buffer_;
}
#endif

std::streamsize console_streambuf::xsgetn(wchar_t* buffer, std::streamsize size) {
    std::streamsize read_size = 0;

#ifdef _MSC_VER
    DWORD read_bytes;
    auto const result = ReadConsoleW(get_input_handle(), buffer,
        static_cast<DWORD>(size), &read_bytes, nullptr);

    if (result == FALSE) {
        throw std::iostream::failure("Failed to read from console.");
    }

    read_size = static_cast<std::streamsize>(read_bytes);
#endif

    return read_size;
}

std::wstreambuf::int_type console_streambuf::underflow() {
#ifdef _MSC_VER
    if (gptr() == nullptr || gptr() >= egptr()) {
        auto const length = xsgetn(buffer_, buffer_size_);
        if (length > 0) {
            setg(buffer_, buffer_, &buffer_[length]);
        }
    }

    if (gptr() == nullptr || gptr() >= egptr()) {
        return traits_type::eof();
    }
#endif

    return traits_type::to_int_type(*gptr());
}

} // namespace kth
