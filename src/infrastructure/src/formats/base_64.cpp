// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_64.hpp>

#include <cstdint>
#include <string>

#include <kth/infrastructure/utility/data.hpp>

#ifndef __EMSCRIPTEN__
// Use SIMD-optimized simdutf library when not compiling for WebAssembly
#include <simdutf.h>

namespace kth {

std::string encode_base64(byte_span unencoded) {
    auto const input = reinterpret_cast<char const*>(unencoded.data());
    auto const size = unencoded.size();
    auto const output_size = simdutf::base64_length_from_binary(size);

    std::string result(output_size, '\0');
    simdutf::binary_to_base64(input, size, result.data());
    return result;
}

bool decode_base64(data_chunk& out, std::string_view in) {
    auto const max_size = simdutf::maximal_binary_length_from_base64(in.data(), in.size());
    data_chunk decoded(max_size);

    auto result = simdutf::base64_to_binary(
        in.data(),
        in.size(),
        reinterpret_cast<char*>(decoded.data())
    );

    if (result.error != simdutf::error_code::SUCCESS) {
        return false;
    }

    decoded.resize(result.count);
    out = std::move(decoded);
    return true;
}

} // namespace kth

#else
// Fallback implementation for WebAssembly
// This implementation derived from public domain:
// en.wikibooks.org/wiki/Algorithm_Implementation/Miscellaneous/Base64

namespace kth {

constexpr char pad = '=';

constexpr char table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode_base64(byte_span unencoded)
{
    std::string encoded;
    auto const size = unencoded.size();
    encoded.reserve(((size / 3) + static_cast<unsigned long>(size % 3 > 0)) * 4);

    uint32_t value;
    auto cursor = unencoded.begin();
    for (size_t position = 0; position < size / 3; position++) {
        // Convert to big endian.
        value = (*cursor++) << 16;

        value += (*cursor++) << 8;
        value += (*cursor++);
        encoded.append(1, table[(value & 0x00FC0000) >> 18]);
        encoded.append(1, table[(value & 0x0003F000) >> 12]);
        encoded.append(1, table[(value & 0x00000FC0) >> 6]);
        encoded.append(1, table[(value & 0x0000003F) >> 0]);
    }

    switch (size % 3) {
        case 1:
            // Convert to big endian.
            value = (*cursor++) << 16;

            encoded.append(1, table[(value & 0x00FC0000) >> 18]);
            encoded.append(1, table[(value & 0x0003F000) >> 12]);
            encoded.append(2, pad);
            break;
        case 2:
            // Convert to big endian.
            value = (*cursor++) << 16;

            value += (*cursor++) << 8;
            encoded.append(1, table[(value & 0x00FC0000) >> 18]);
            encoded.append(1, table[(value & 0x0003F000) >> 12]);
            encoded.append(1, table[(value & 0x00000FC0) >> 6]);
            encoded.append(1, pad);
            break;
    }

    return encoded;
}

bool decode_base64(data_chunk& out, std::string_view in)
{
    constexpr uint32_t mask = 0x000000FF;

    auto const length = in.length();
    if ((length % 4) != 0) {
        return false;
}

    size_t padding = 0;
    if (length > 0) {
        if (in[length - 1] == pad) {
            padding++;
}
        if (in[length - 2] == pad) {
            padding++;
}
    }

    data_chunk decoded;
    decoded.reserve(((length / 4) * 3) - padding);

    uint32_t value = 0;
    for (auto cursor = in.begin(); cursor < in.end();) {
        for (size_t position = 0; position < 4; position++)
        {
            value <<= 6;
            if (*cursor >= 0x41 && *cursor <= 0x5A) {
                value |= *cursor - 0x41;
            } else if (*cursor >= 0x61 && *cursor <= 0x7A) {
                value |= *cursor - 0x47;
            } else if (*cursor >= 0x30 && *cursor <= 0x39) {
                value |= *cursor + 0x04;
            } else if (*cursor == 0x2B) {
                value |= 0x3E;
            } else if (*cursor == 0x2F) {
                value |= 0x3F;
            } else if (*cursor == pad)
            {
                // Handle 1 or 2 pad characters.
                switch (in.end() - cursor)
                {
                    case 1:
                        decoded.push_back((value >> 16) & mask);
                        decoded.push_back((value >> 8) & mask);
                        out = decoded;
                        return true;
                    case 2:
                        decoded.push_back((value >> 10) & mask);
                        out = decoded;
                        return true;
                    default:
                        return false;
                }
            }
            else {
                return false;
}

            cursor++;
        }

        decoded.push_back((value >> 16) & mask);
        decoded.push_back((value >> 8) & mask);
        decoded.push_back((value >> 0) & mask);
    }

    out = decoded;
    return true;
}

} // namespace kth

#endif // __EMSCRIPTEN__
