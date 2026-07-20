// Copyright (c) 2016-present Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/point.hpp>

#include <algorithm>
#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/chain/output_point.hpp>
#include <kth/infrastructure/config/hash256.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/math/hash.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

using infrastructure::config::hash256;

std::string_view const point::delimeter = ":";

// static
expect<point> point::parse_from(std::string_view text) {
    // Wire: `<txhash>:<index>`.
    auto const tokens = split(text, point::delimeter);
    if (tokens.size() != 2) {
        return std::unexpected(kth::error::illegal_value);
    }

    auto digest_res = hash256::parse_from(tokens[0]);
    if ( ! digest_res) {
        return std::unexpected(kth::error::illegal_value);
    }

    return point{chain::output_point{
        digest_res->value(),
        deserialize<uint32_t>(tokens[1], true),
    }};
}

std::string point::to_string() const {
    return fmt::format("{}{}{}",
        hash256{value_.hash()}.to_string(),
        point::delimeter,
        value_.index());
}

} // namespace kth::domain::config
