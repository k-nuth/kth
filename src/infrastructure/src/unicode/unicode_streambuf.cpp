// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/unicode/unicode_streambuf.hpp>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <streambuf>

#include <kth/infrastructure/constants.hpp>
#include <kth/infrastructure/unicode/unicode.hpp>
#include <kth/infrastructure/utility/assert.hpp>

namespace kth {

// Local definition for max number of bytes in a utf8 character.
constexpr size_t utf8_max_character_size = 4;

unicode_streambuf::unicode_streambuf(std::wstreambuf* wide_buffer, size_t size)
    : wide_size_(size), narrow_size_(wide_size_ * utf8_max_character_size)
    , narrow_(new char[narrow_size_]), wide_(new wchar_t[narrow_size_])
    , wide_buffer_(wide_buffer)
{
    if (wide_size_ > (kth::max_uint64 / utf8_max_character_size)) {
        throw std::ios_base::failure("Wide buffer must be no more than one fourth of max uint64.");
    }

    // Input buffer is not yet populated, reflect zero length buffer here.
    setg(narrow_, narrow_, narrow_);

    // Output buffer is underexposed by 1 byte to accomodate the overflow byte.
    setp(narrow_, &narrow_[narrow_size_ - 1]);
}

unicode_streambuf::~unicode_streambuf() {
    sync();
    delete[] wide_;
    delete[] narrow_;
}

// Read characters from input buffer.
// This invokes wide_buffer_.xsgetn() which requires a patch for
// console (keyboard) input on Windows, so ensure this class is
// initialized with a patched std::wcin when std::wcin is used.
std::streambuf::int_type unicode_streambuf::underflow() {
    // streamsize is signed.
    KTH_ASSERT(wide_size_ > 0 && wide_size_ <= kth::max_int64);
    auto const size = static_cast<std::streamsize>(wide_size_);

    // Read from the wide input buffer.
    auto const read = static_cast<size_t>(wide_buffer_->sgetn(wide_, size));

    // Handle read termination.
    if (read == 0) {
        return traits_type::eof();
    }

    // Convert utf16 to utf8, returning bytes written.
    auto const bytes = to_utf8(narrow_, narrow_size_, wide_, read);

    // Reset gptr and egptr, eback never changes.
    setg(narrow_, narrow_, &narrow_[bytes]);

    // Return the first character in the input sequence.
    return traits_type::to_int_type(*gptr());
}

// Write characters to output buffer.
// We compensate for character-splitting. This is necessary because
// MSVC does not support a UTF8 locale and as such streams interpret
// narrow characters in the default locale. This implementation
// assumes the stream will treat each byte of a multibyte narrow
// chracter as an individual single byte character.
std::streambuf::int_type unicode_streambuf::overflow(std::streambuf::int_type character) {
    // Add a single explicitly read byte to the buffer.
    // The narrow buffer is underexposed by 1 byte to accomodate this.
    if (character != traits_type::eof()) {
        *pptr() = static_cast<char>(character);
        pbump(sizeof(char));
    }

    // This will be in the range 0..4, indicating the number of bytes that were
    // not written in the conversion. A nonzero value results when the buffer
    // terminates within a utf8 multiple byte character.
    uint8_t unwritten = 0;

    // Get the number of bytes in the buffer to convert.
    auto const write = pptr() - pbase();

    if (write > 0) {
        // Convert utf8 to utf16, returning chars written and bytes unread.
        auto const chars = to_utf16(wide_, narrow_size_, narrow_, write, unwritten);

        // Write to the wide output buffer.
        auto const written = wide_buffer_->sputn(wide_, chars);

        // Handle write failure as an EOF.
        if (written != chars) {
            return traits_type::eof();
        }
    }

    // Copy the fractional character to the beginning of the buffer.
    memcpy(narrow_, &narrow_[write - unwritten], unwritten);

    // Reset the pptr to the buffer start, leave pbase and epptr.
    // We could use just pbump for this if it wasn't limited to 'int' width.
    setp(narrow_, &narrow_[narrow_size_ - 1]);

    // Reset pptr just after the fractional character.
    pbump(unwritten);

    // Return the overflow byte or EOF sentinel.
    return character;
}

// Flush our output sequence.
int unicode_streambuf::sync() {
    int const success = 0;
    int const failure = -1;

    // We expect EOF to be returned, because we passed it.
    if (overflow(traits_type::eof()) == traits_type::eof()) {
        return success;
    }

    return failure;
}

} // namespace kth
