// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/infrastructure/config/base16.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::infrastructure::config {

// static
std::expected<base16, kth::code> base16::parse_from(std::string_view text) noexcept {
    auto result = decode_base16(text);
    if ( ! result) {
        return std::unexpected(kth::error::illegal_value);
    }
    return base16{std::move(*result)};
}

std::string base16::to_string() const {
    return encode_base16(value_);
}

} // namespace kth::infrastructure::config
