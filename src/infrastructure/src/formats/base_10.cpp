// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/formats/base_10.hpp>

namespace kth::detail {

std::expected<uint64_t, base10_errc> decode_base10_runtime(
    std::string_view amount, uint8_t places, bool strict) noexcept {
    return decode_base10_impl(amount, places, strict);
}

std::string encode_base10_runtime(uint64_t amount, uint8_t places) {
    return encode_base10_impl(amount, places);
}
} // namespace kth::detail
