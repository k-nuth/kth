// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_16.hpp>

namespace kth {

namespace detail {

std::string encode_base16_runtime(byte_span input) {
    std::string out;
    out.resize_and_overwrite(input.size() * 2, [input](char *bytes, size_t) noexcept {
        auto const size = input.size() * 2;
        encode_base16_into(input, std::span(bytes, size));
        return size;
    });
    return out;
}

std::expected<size_t, base16_errc> decode_base16_runtime(std::string_view in, std::span<uint8_t> out) noexcept {
    return decode_base16_scalar(in, out);
}

std::expected<data_chunk, base16_errc> decode_base16_runtime(std::string_view in) {
    return decode_base16_scalar(in);
}
} // namespace detail
} // namespace kth
