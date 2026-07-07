// Copyright (c) 2016-2025 Knuth Project developers.
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <kth/domain/config/input.hpp>

#include <string>
#include <string_view>

#include <fmt/core.h>

#include <kth/domain/chain/input.hpp>
#include <kth/domain/chain/input_point.hpp>
#include <kth/domain/config/point.hpp>
#include <kth/infrastructure/error.hpp>
#include <kth/infrastructure/utility/string.hpp>

namespace kth::domain::config {

// static
expect<input> input::parse_from(std::string_view text) {
    // Wire format: `<txhash>:<index>[:<sequence>]`.
    auto const tokens = split(text, point::delimeter);
    if (tokens.size() != 2 && tokens.size() != 3) {
        return std::unexpected(error::illegal_value);
    }

    auto pt_res = point::parse_from(tokens[0] + ":" + tokens[1]);
    if ( ! pt_res) {
        return std::unexpected(error::illegal_value);
    }

    auto const sequence = (tokens.size() == 3)
        ? deserialize<uint32_t>(tokens[2], true)
        : max_input_sequence;

    return input{
        chain::input{
            chain::output_point{pt_res->value()},
            chain::script{},
            sequence
        }
    };
}

std::string input::to_string() const {
    return fmt::format("{}{}{}",
        point{value_.previous_output()}.to_string(),
        point::delimeter,
        value_.sequence());
}

} // namespace kth::domain::config
