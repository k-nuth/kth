// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/header.hpp>

#include <string>
#include <string_view>
#include <utility>

#include <kth/domain/chain/header.hpp>
#include <kth/infrastructure/config/base16.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/formats/base_16.hpp>

namespace kth::domain::config {

using namespace infrastructure::config;

// static
expect<header> header::parse_from(std::string_view text) noexcept {
    auto const bytes_result = base16::parse_from(text);
    if ( ! bytes_result) {
        return std::unexpected(kth::error::illegal_value);
    }
    data_chunk const& bytes = bytes_result->value();
    byte_reader reader(bytes);
    auto header_exp = chain::header::from_data(reader);
    if ( ! header_exp) {
        return std::unexpected(kth::error::illegal_value);
    }
    return header{std::move(*header_exp)};
}

std::string header::to_string() const {
    return kth::encode_base16(kth::to_data_chunk(value_, true));
}

} // namespace kth::domain::config
