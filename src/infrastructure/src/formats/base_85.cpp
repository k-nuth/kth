// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This implementation is based on:
//  --------------------------------------------------------------------------
//  Reference implementation for rfc.zeromq.org/spec:32/Z85
//
//  This implementation provides a Z85 codec as an easy-to-reuse C class
//  designed to be easy to port into other languages.

//  --------------------------------------------------------------------------
//  Copyright (c) 2010-2013 iMatix Corporation and Contributors
//
//  Permission is hereby granted, free of charge, to any person obtaining a
//  copy of this software and associated documentation files (the "Software"),
//  to deal in the Software without restriction, including without limitation
//  the rights to use, copy, modify, merge, publish, distribute, sublicense,
//  and/or sell copies of the Software, and to permit persons to whom the
//  Software is furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
//  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//  DEALINGS IN THE SOFTWARE.
//  --------------------------------------------------------------------------

#include <kth/infrastructure/formats/base_85.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

#include <kth/infrastructure/utility/assert.hpp>
#include <kth/infrastructure/utility/data.hpp>

namespace kth {

// Maps binary to base 85.
static char encoder[85 + 1] =
{
    "0123456789"
    "abcdefghij"
    "klmnopqrst"
    "uvwxyzABCD"
    "EFGHIJKLMN"
    "OPQRSTUVWX"
    "YZ.-:+=^!/"
    "*?&<>()[]{"
    "}@%$#"
};

// Maps base 85 to binary.
static uint8_t decoder[96] =
{
    0x00, 0x44, 0x00, 0x54, 0x53, 0x52, 0x48, 0x00,
    0x4B, 0x4C, 0x46, 0x41, 0x00, 0x3F, 0x3E, 0x45,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x40, 0x00, 0x49, 0x42, 0x4A, 0x47,
    0x51, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A,
    0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
    0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A,
    0x3B, 0x3C, 0x3D, 0x4D, 0x00, 0x4E, 0x43, 0x00,
    0x00, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20,
    0x21, 0x22, 0x23, 0x4F, 0x00, 0x50, 0x00, 0x00
};

// Accepts only byte arrays bounded to 4 bytes.
bool encode_base85(std::string& out, data_slice in)
{
    size_t const size = in.size();
    if (size % 4 != 0) {
        return false;
}

    size_t const encoded_size = size * 5 / 4;
    std::string encoded;
    encoded.reserve(encoded_size + 1);
    size_t byte_index = 0;
    uint32_t accumulator = 0;

    for (uint8_t const unencoded_byte: in) {
        accumulator = accumulator * 256 + unencoded_byte;
        if (++byte_index % 4 == 0)
        {
            for (uint32_t divise = 85 * 85 * 85 * 85; divise > 0; divise /= 85) {
                encoded.push_back(encoder[accumulator / divise % 85]);
}

            accumulator = 0;
        }
    }

    out.assign(encoded.begin(), encoded.end());
    KTH_ASSERT(out.size() == encoded_size);
    return true;
}

// Accepts only strings bounded to 5 characters.
bool decode_base85(data_chunk& out, std::string_view in)
{
    size_t const length = in.size();
    if (length % 5 != 0) {
        return false;
}

    size_t const decoded_size = length * 4 / 5;
    data_chunk decoded;
    decoded.reserve(decoded_size);
    size_t char_index = 0;
    uint32_t accumulator = 0;

    for (uint8_t const encoded_character: in) {
        auto const position = encoded_character - 32;
        if (position < 0 || position > 96) {
            return false;
}

        accumulator = accumulator * 85 + decoder[position];
        if (++char_index % 5 == 0)
        {
            for (uint32_t divise = 256 * 256 * 256; divise > 0; divise /= 256) {
                decoded.push_back(accumulator / divise % 256);
}

            accumulator = 0;
        }
    }

    out.assign(decoded.begin(), decoded.end());
    KTH_ASSERT(out.size() == decoded_size);
    return true;
}

} // namespace kth
