// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_58.hpp>

namespace kth::detail {

std::string encode_base58_runtime(byte_span input) {
    return encode_base58_scalar(input);
}

std::expected<size_t, base58_errc> decode_base58_runtime(std::string_view input, std::span<uint8_t> output) noexcept {
    return decode_base58_scalar(input, output);
}

std::expected<data_chunk, base58_errc> decode_base58_runtime(std::string_view input) {
    return decode_base58_scalar(input);
}

} // namespace kth::detail
